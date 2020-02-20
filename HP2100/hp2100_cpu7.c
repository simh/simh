/* hp2100_cpu7.c: HP 1000 RTE-6/VM VMA microcode simulator

   Copyright (c) 2008, Holger Veit
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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU7         RTE-6/VM Virtual Memory Area instructions

   23-Aug-18    JDB     Changed STOP_HALT returns to SCPE_IERR if MP aborts fail
   02-Aug-18    JDB     Moved VMA dispatcher from hp2100_cpu5.c
                        Moved VIS dispatcher to hp2100_cpu5.c
   28-Jul-18    JDB     Renamed hp2100_fp1.h to hp2100_cpu_fp.h
   31-Jan-17    JDB     Revised to use tprintf and TRACE_OPND for debugging
   26-Jan-17    JDB     Removed debug parameters from cpu_ema_* routines
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Moved EMA helper declarations to hp2100_cpu1.h
   09-May-12    JDB     Separated assignments from conditional expressions
   06-Feb-12    JDB     Corrected "opsize" parameter type in vis_abs
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   30-Apr-08    JDB     Updated SIGNAL code from Holger
   24-Apr-08    HV      Implemented SIGNAL
   20-Apr-08    JDB     Updated comments
   26-Feb-08    HV      Implemented VIS

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


   The RTE-IV and RTE-IVB Extended Memory Array instructions and the RTE-6/VM
   Virtual Memory Area instructions were added to accelerate the logical-to-
   physical address translations and array subscript calculations of programs
   running under the RTE-IV (HP product number 92067A), RTE-IVB (92068A), and
   RTE-6/VM (92084A) operating systems.  Microcode was available for the E- and
   F-Series; the M-Series used software equivalents.

   Both EMA and VMA opcodes reside in the range 105240-105257, so only one or
   the other can be installed in a given system.  This does not present a
   difficulty, as VMA is a superset of EMA.  The VMA instruction encodings are:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   0   0 |  .PMAP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if page is mapped               :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .PMAP instruction maps the memory page whose physical page number is in
   the B-register into the map register specified by the A-register.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   0   1 |  $LOC
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 logical starting page of node                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |       relative page from partition start to node start        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |        relative page from partition start to base page        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                   current path word address                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       leaf node number                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        ordinal number                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $LOC instruction implements load-on-call for MLS/LOC programs..


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   1   0 |  vmtst
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :       return location if the firmware is not installed        :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :         return location if the firmware is installed          :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The vmtst instruction is used to determine programmatically if the VMA
   firmware has been installed.  It sets the X-register to the firmware revision
   code, sets Y to 1, sets S to 102077 (HLT 77B), and returns to P+2.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   1   1 |  [.SWP]
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .SWP instruction swaps the A- and B-register values.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 1   0   0 |  [.STAS]
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 1   0   1 |  [.LDAS]
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   These instructions are not implemented and will cause unimplemented
   instruction stops if enabled.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 1   1   1 |  [.UMPY]
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      multiplier address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .UMPY instruction performs an unsigned multiply-and-add.  The A-register
   contains the multiplicand, and the B-register contains the augend.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 0   0   0 |  .IMAP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    last subscript address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                              ...                              :
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first subscript address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .IMAP instruction resolves the address of a one-word array element and
   maps the element into the last two pages of logical memory.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 0   0   1 |  .IMAR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                address of array table address                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .IMAR instruction resolves the address of a one-word array element.  It
   does not map the element.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 0   1   0 |  .JMAP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    last subscript address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                              ...                              :
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first subscript address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .JMAP instruction resolves the address of a two-word array element and
   maps the element into the last two pages of logical memory.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 0   1   1 |  .JMAR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                address of array table address                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .JMAR instruction resolves the address of a two-word array element.  It
   does not map the element.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 1   0   0 |  .LPXR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        pointer address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        offset address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .LPXR instruction maps a one-word element addressed by a 32-bit pointer
   plus a 32-bit offset into logical memory and returns the logical address of
   the element in the B-register.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 1   0   1 |  .LPX
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        offset address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .LPX instruction maps a one-word element addressed by a 32-bit pointer
   contained in the A- and B-registers plus a 32-bit offset into logical memory
   and returns the logical address of the element in the B-register.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 1   0   0 |  .LBPR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        pointer address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .LBPR instruction maps a one-word element addressed by a 32-bit pointer
   into logical memory and returns the logical address of the element in the
   B-register.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 1   1   1 |  .LBP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .LBP instruction maps a one-word element addressed by a 32-bit pointer
   contained in the A- and B-registers into logical memory and returns the
   logical address of the element in the B-register and the page ID of the
   element in the A-register.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* Paging constants */

#define SUITMASK            0176000u
#define NILPAGE             0176000u
#define PAGEIDX             0001777u
#define RWPROT              0141777u


/* RTE-6/VM base page addresses */

static const HP_WORD idx     = 0001645u;
static const HP_WORD xmata   = 0001646u;
static const HP_WORD xi      = 0001647u;
static const HP_WORD xeqt    = 0001717u;
static const HP_WORD vswp    = 0001776u;
static const HP_WORD page30  = 0074000u;
static const HP_WORD page31  = 0076000u;
static const HP_WORD ptemiss = 0176000u;



/* VMA local utility routine declarations */

static t_stat cpu_vma_loc   (OPS op);
static t_bool cpu_vma_ptevl (uint32 pagid, uint32* physpg);
static t_stat cpu_vma_fault (uint32 x, uint32 y, int32 mapr, uint32 ptepg, uint32 ptr, uint32 faultpc);
static t_bool cpu_vma_mapte (uint32* ptepg);
static t_stat cpu_vma_lbp   (uint32 ptr, uint32 aoffset, uint32 faultpc);
static t_stat cpu_vma_pmap  (uint32 umapr, uint32 pagid);
static t_stat cpu_vma_ijmar (OPSIZE ij, uint32 dtbl, uint32 atbl, uint32* dimret);



/* Global instruction executors */


/* RTE-6/VM Virtual Memory Area instructions.

   RTE-6/VM (product number 92084A) introduced Virtual Memory Area (VMA)
   instructions -- a superset of the RTE-IV EMA instructions.  Different
   microcode was supplied with the operating system that replaced the microcode
   used with RTE-IV.  Microcode was limited to the E/F-Series; the M-Series used
   software equivalents.

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

   Implementation notes:

    1. The opcodes 105243-247 are undocumented and do not appear to be used in
       any HP software.

    2. The opcode list in the CE Handbook incorrectly shows 105246 as ".MYAD -
       multiply 2 signed integers."  The microcode listing shows that this
       instruction was deleted, and the opcode is now a NOP.

    3. RTE-IV EMA and RTE-6 VMA instructions shared the same address space, so a
       given machine could run one or the other, but not both.

   Additional references:
    - RTE-6/VM VMA/EMA Microcode Source (92084-18828, revision 3).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).
    - M/E/F-Series Computer Systems CE Handbook (5950-3767, Jul-1984).
*/

static const OP_PAT op_vma[16] = {
  OP_N,    OP_CCCACC, OP_N,    OP_N,                    /* .PMAP  $LOC   [test] .SWAP  */
  OP_N,    OP_N,      OP_N,    OP_K,                    /* .STAS  .LDAS  .MYAD  .UMPY  */
  OP_A,    OP_A,      OP_A,    OP_A,                    /* .IMAP  .IMAR  .JMAP  .JMAR  */
  OP_AA,   OP_A,      OP_A,    OP_N                     /* .LPXR  .LPX   .LBPR  .LBP   */
  };

t_stat cpu_rte_vma (void)
{
static const char * const no [2] = { "",     "no " };

t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
HP_WORD t16;
uint32 entry,t32,ndim;
uint32 dtbl,atbl;                                       /* descriptor table ptr, actual args ptr */
OP dop0,dop1;
uint32 pcsave = (PR+1) & LA_MASK;                       /* save P to check for redo in imap/jmap */

entry = IR & 017;                                       /* mask to entry point */
pattern = op_vma[entry];                                /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op);                     /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    case 000:                                           /* .PMAP 105240 (OP_N) */
        reason = cpu_vma_pmap (AR, BR);                 /* map pages */

        if (PR - err_PR <= 2)
            tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%serror)\n",
                     PR, IR, PR - err_PR, no [PR - err_PR - 1]);
        break;

    case 001:                                           /* $LOC  105241 (OP_CCCACC) */
        reason = cpu_vma_loc(op);                       /* handle the coroutine switch */
        break;

    case 002:                                           /* [test] 105242 (OP_N) */
        XR = 3;                                         /* refer to src code 92084-18828 rev 3 */
        SR = 0102077;                                   /* HLT 77 instruction */
        YR = 1;                                         /* ROMs correctly installed */
        PR = (PR+1) & LA_MASK;                          /* skip instr if VMA/EMA ROM installed */
        break;

    case 003:                                           /* [swap] 105243 (OP_N) */
        t16 = AR;                                       /* swap A and B registers */
        AR = BR;
        BR = t16;
        break;

    case 004:                                           /* [---] 105244 (OP_N) */
        reason = STOP (cpu_ss_unimpl);                  /* fragment of dead code */
        break;                                          /* in microrom */

    case 005:                                           /* [---] 105245 (OP_N) */
        reason = STOP (cpu_ss_unimpl);                  /* fragment of dead code */
        break;                                          /* in microrom */

    case 006:                                           /* [nop] 105246 (OP_N) */
        break;                                          /* do nothing */

    case 007:                                           /* [umpy] 105247 (OP_K) */
        t32 = AR * op[0].word;                          /* get multiplier */
        t32 += BR;                                      /* add B */
        AR = UPPER_WORD (t32);                          /* move result back to AB */
        BR = LOWER_WORD (t32);
        O = 0;                                          /* instr clears OV */
        break;

    case 010:                                           /* .IMAP 105250 (OP_A) */
        dtbl = op[0].word;
        atbl = PR;
        reason = cpu_vma_ijmar(in_s,dtbl,atbl,&ndim);   /* calc the virt address to AB */
        if (reason)
            return reason;
        t32 = TO_DWORD (AR, BR);
        reason = cpu_vma_lbp(t32,0,PR-2);
        if (reason)
            return reason;
        if (PR==pcsave)
            PR = (PR+ndim) & LA_MASK;                   /* adjust P: skip ndim subscript words */
        break;

    case 011:                                           /* .IMAR 105251 (OP_A) */
        dtbl = ReadW(op[0].word);
        atbl = (op[0].word+1) & LA_MASK;
        reason = cpu_vma_ijmar(in_s,dtbl,atbl,0);       /* calc the virt address to AB */
    break;

    case 012:                                           /* .JMAP 105252 (OP_A) */
        dtbl = op[0].word;
        atbl = PR;
        reason = cpu_vma_ijmar(in_d,dtbl,atbl,&ndim);   /* calc the virtual address to AB */
        if (reason)
            return reason;
        t32 = TO_DWORD (AR, BR);
        reason = cpu_vma_lbp(t32,0,PR-2);
        if (reason)
            return reason;
        if (PR==pcsave)
            PR = (PR + ndim) & LA_MASK;                 /* adjust P: skip ndim subscript dword ptr */
        break;

    case 013:                                           /* .JMAR 105253 (OP_A) */
        dtbl = ReadW(op[0].word);
        atbl = (op[0].word+1) & LA_MASK;
        reason = cpu_vma_ijmar(in_d,dtbl,atbl,0);       /* calc the virt address to AB */
        break;

    case 014:                                           /* .LPXR 105254 (OP_AA) */
        dop0 = ReadOp(op[0].word,in_d);                 /* get pointer from arg */
        dop1 = ReadOp(op[1].word,in_d);
        t32 = dop0.dword + dop1.dword;                  /* add offset to it */
        reason = cpu_vma_lbp(t32,0,PR-3);
        break;

    case 015:                                           /* .LPX  105255 (OP_A) */
        t32 = TO_DWORD (AR, BR);                        /* pointer in AB */
        dop0 = ReadOp(op[0].word,in_d);
        reason = cpu_vma_lbp(t32,dop0.dword,PR-2);
        break;

    case 016:                                           /* .LBPR 105256 (OP_A) */
        dop0 = ReadOp(op[0].word,in_d);                 /* get the pointer */
        reason = cpu_vma_lbp(dop0.dword,0,PR-2);
        break;

    case 017:                                           /* .LBP  105257 (OP_N) */
        t32 = TO_DWORD (AR, BR);
        reason = cpu_vma_lbp(t32,0,PR-1);
        break;
    }

return reason;
}



/* VMA local utility routines */


/* $LOC
   ASSEMBLER CALLING SEQUENCE:

   $MTHK NOP             RETURN ADDRESS OF CALL (REDONE AFTER THIS ROUTINE)
         JSB $LOC
   .DTAB OCT LGPG#       LOGICAL PAGE # AT WHICH THE NODE TO
  *                      BE MAPPED IN BELONGS  (0-31)
         OCT RELPG       RELATIVE PAGE OFFSET FROM BEGINING
  *                      OF PARTITION OF WHERE THAT NODE RESIDES.
  *                      (0 - 1023)
         OCT RELBP       RELATIVE PAGE OFFSET FROM BEGINING OF
  *                      PARTITION OF WHERE BASE PAGE RESIDES
  *                      (0 - 1023)
   CNODE DEF .CNOD       THIS IS THE ADDRESS OF CURRENT PATH # WORD
   .ORD  OCT XXXXX       THIS NODE'S LEAF # (IE PATH #)
   .NOD# OCT XXXXX       THIS NODE'S ORDINAL #
*/

static t_stat cpu_vma_loc(OPS op)
{
uint32  lstpg,fstpg,rotsz,lgpg,relpg,relbp,matloc,ptnpg,physpg,cnt,pgs,umapr;
HP_WORD eqt,mls,pnod;

eqt = ReadU (xeqt);                                     /* get ID segment */
mls = ReadS (eqt + 33);                                 /* get word33 of alternate map */
if ((mls & 0x8000) == 0) {                              /* this is not an MLS prog! */
    PR = err_PR;
    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, not an MLS program\n",
             PR, IR);
    mp_violation ();                                    /* MP abort */
    return SCPE_IERR;                                   /*   unless MP is off, which is impossible */
    }

pnod = mls & 01777;                                     /* get #pages of mem res nodes */
if (pnod == 0) {                                        /* no pages? FATAL! */
    PR = err_PR;
    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, no memory resident nodes\n",
             PR, IR);
    mp_violation ();                                    /* MP abort */
    return SCPE_IERR;                                   /*   unless MP is off, which is impossible */
    }

lstpg = (ReadS (eqt + 29) >> 10) - 1;                   /* last page# of code */
fstpg =  ReadS (eqt + 23) >> 10;                        /* index to 1st addr + mem nodes */
rotsz =  fstpg - (ReadS (eqt + 22) >> 10);              /* #pages in root */
lgpg = op[0].word;

/* lets do some consistency checks, CPU halt if they fail */
if (lstpg < lgpg || lgpg < fstpg) {                     /* assert LSTPG >= LGPG# >= FSTPG */
    PR = err_PR;
    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, failed check LSTPG >= LGPG# >= FSTPG\n",
             PR, IR);
    mp_violation ();                                    /* MP abort */
    return SCPE_IERR;                                   /*   unless MP is off, which is impossible */
    }

relpg = op[1].word;
if (pnod < relpg || relpg < (rotsz+1)) {                /* assert #PNOD >= RELPG >= ROTSZ+1 */
    PR = err_PR;
    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, failed check #PNOD >= RELPG >= ROTSZ + 1\n",
             PR, IR);
    mp_violation ();                                    /* MP abort */
    return SCPE_IERR;                                   /*   unless MP is off, which is impossible */
    }

relbp = op[2].word;
if (relbp != 0)                                         /* assert RELBP == 0 OR */
    if (pnod < relbp || relbp < (rotsz+1)) {            /* #PNOD >= RELBP >= ROTSZ+1 */
        PR = err_PR;
        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, failed check #PNOD >= RELBP >= ROTSZ + 1\n",
                 PR, IR);
        mp_violation ();                                /* MP abort */
        return SCPE_IERR;                               /*   unless MP is off, which is impossible */
    }

cnt = lstpg - lgpg + 1;                                 /* #pages to map */
pgs = pnod - relpg + 1;                                 /* #pages from start node to end of code */
if (pgs < cnt) cnt = pgs;                               /* ensure minimum, so not to map into EMA */

matloc = ReadU (xmata);                                 /* get MAT $LOC address */
ptnpg  = ReadS (matloc + 3) & 01777;                    /* index to start phys pg */
physpg = ptnpg + relpg;                                 /* phys pg # of node */
umapr  = lgpg;                                          /* map register to start */

/* do an XMS with AR=umapr,BR=physpg,XR=cnt */
tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  pages %u, physical page %u, map register %u\n",
         PR, IR, cnt, physpg, umapr);

while (cnt != 0) {
    meu_write_map (User_Map, umapr, physpg);            /* map pages of new overlay segment */
    cnt = (cnt - 1) & D16_MASK;
    umapr = (umapr + 1) & D16_MASK;
    physpg = (physpg + 1) & D16_MASK;
    }

meu_write_map(User_Map, 0, relbp+ptnpg);                /* map base page again */
WriteW(op[3].word,op[4].word);                          /* path# we are going to */

PR = (PR - 8) & R_MASK;                                 /* adjust P to return address */
                                                        /* word before the $LOC microinstruction */
PR = (ReadW(PR) - 1) & R_MASK;                          /* but the call has to be rerun, */
                                                        /* so must skip back to the original call */
                                                        /* which will now lead to the real routine */
tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  path %06o\n",
         PR, IR, op[4].word);
return SCPE_OK;
}


/* map pte into last page
   return FALSE if page fault, nil flag in PTE or suit mismatch
   return TRUE if suit match, physpg = physical page
                or page=0 -> last+1 page
*/
static t_bool cpu_vma_ptevl(uint32 pagid,uint32* physpg)
{
uint32 suit;
uint32 pteidx = pagid & 0001777;                        /* build index */
uint32 reqst  = pagid & SUITMASK;                       /* required suit */
uint32 pteval = ReadW(page31 | pteidx);                 /* get PTE entry */
*physpg = pteval & 0001777;                             /* store physical page number */
suit = pteval & SUITMASK;                               /* suit number seen */
if (pteval == NILPAGE) return FALSE;                    /* NIL value in PTE */
return suit == reqst || !*physpg;                       /* good page or last+1 */
}


/* handle page fault */

static t_stat cpu_vma_fault(uint32 x,uint32 y,int32 mapr,uint32 ptepg,uint32 ptr,uint32 faultpc)
{
uint32 pre = ReadU (xi);                                /* get program preamble */
uint32 ema = ReadU (pre + 2);                           /* get address of $EMA$/$VMA$ */
WriteU (ema, faultpc);                                  /* write addr of fault instr */
XR = x;                                                 /* X = faulting page */
YR = y;                                                 /* Y = faulting address for page */

if (mapr>0)
    meu_write_map(User_Map, mapr, ptepg);               /* map PTE into specified user dmsmap */

/* do a safety check: first instr of $EMA$/$VMA$ must be a DST instr */
if (ReadU (ema + 1) != 0104400) {
    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  fatal error, no EMA/VMA user code present\n",
             PR, IR);
    mp_violation ();                                    /* MP abort */
    return SCPE_IERR;                                   /*   unless MP is off, which is impossible */
    }

PR = (ema+1) & LA_MASK;                                 /* restart $EMA$ user code, */
                                                        /* will return to fault instruction */

AR = UPPER_WORD (ptr);                                  /* restore A, B */
BR = LOWER_WORD (ptr);
E = 0;                                                  /* enforce E = 0 */
tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  page fault\n",
         PR, IR);
return SCPE_OK;
}


/* map in PTE into last page, return false, if page fault */

static t_bool cpu_vma_mapte(uint32* ptepg)
{
uint32 idext,idext2;
uint32 dispatch = ReadU (vswp) & 01777;                 /* get fresh dispatch flag */
t_bool swapflag = TRUE;

if (dispatch == 0) {                                    /* not yet set */
    idext = ReadU (idx);                                /* go into ID segment extent */
    if (idext == 0) {                                   /* is ema/vma program? */
        swapflag = FALSE;                               /* no, so mark PTE as invalid */
        *ptepg = (uint32) -1;                           /*   and return an invalid page number */
        }

    else {                                              /* is an EMA/VMA program */
        dispatch = ReadWA(idext+1) & 01777;             /* get 1st ema page: new vswp */
        WriteU (vswp, dispatch);                        /* move into $VSWP */
        idext2 = ReadWA(idext+2);                       /* get swap bit */
        swapflag = (idext2 & 020000) != 0;              /* bit 13 = swap bit */
        }
    }

if (dispatch) {                                         /* some page is defined */
    meu_write_map(User_Map, 31, dispatch);              /* map $VSWP to register 31 */
    *ptepg = dispatch;                                  /* return PTEPG# for later */
    }

return swapflag;                                        /* true for valid PTE */
}


/*  .LBP
    ASSEMBLER CALLING SEQUENCE:

    DLD PONTR       TRANSLATE 32 BIT POINTER TO 15
    JSB .LBP        BIT POINTER.
    <RETURN - B = LOGICAL ADDRESS, A = PAGID>

    32 bit pointer:
    ----------AR------------ -----BR-----
    15 14....10 9....4 3...0 15.10 9....0
    L<----------------------------------- L=1 local reference bit
       XXXXXXXX<------------------------- 5 bit unused
                PPPPPP PPPPP PPPPP<------ 16 bit PAGEID
                SSSSSS<------------------ SUIT# within PAGEID
                       PPPPP PPPPP<------ 10 bit PAGEID index into PTE
                                   OOOOOO 10 bit OFFSET


   Implementation notes:

    1. The comments prededing the .LBP microcode are wrong with regard to the
       VSEG map setup when the first mapped page is the last page in the VM
       area.  They claim, "THE MICROCODE WILL MAP IN PHYSICAL PAGE 1023 MARKING
       IT READ/WRITE PROTECTED.  ANY ACCESS TO THIS PAGE WILL PRODUCE A DMS
       ERROR."  Actually, the microcode sets the second map register to point at
       the last page (same as the first map register), and the page is not
       protected.  This means that a spillover access beyond the last VM page
       will corrupt the last VM page instead of causing a DM abort as intended.
       Ths simulator follows the microcode in reproducing this bug.
*/

static t_stat cpu_vma_lbp(uint32 ptr,uint32 aoffset,uint32 faultpc)
{
uint32 pagid,offset,pgidx,ptepg;
HP_WORD p30,p31,suit;
t_stat reason = SCPE_OK;
uint32 faultab = ptr;                                   /* remember A,B for page fault */
ptr += aoffset;                                         /* add the offset e.g. for .LPX */

tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  virtual address %011o\n",
         PR, IR, ptr);

O = 0;                                                  /* clear overflow */
if (ptr & 0x80000000) {                                 /* is it a local reference? */
    MR = (HP_WORD) (ptr & LA_MASK);
    if (ptr & IR_IND) {
        MR = ReadW (MR);
        reason = cpu_resolve_indirects (FALSE);         /* resolve indirects (uninterruptible) */
        if (reason)
            return reason;                              /* yes, resolve indirect ref */
        }
    BR = MR & LA_MASK;                                  /* address is local */
    AR = UPPER_WORD (ptr);
    return SCPE_OK;
    }

pagid  = (ptr >> 10) & D16_MASK;                        /* extract page id (16 bit idx, incl suit*/
offset = ptr & 01777;                                   /* and offset */
suit   = pagid & SUITMASK;                              /* suit of page */
pgidx  = pagid & PAGEIDX;                               /* index into PTE */

tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  suit %u PTE index %u offset %u\n",
         PR, IR, suit >> 10, pgidx, offset);

if (!cpu_vma_mapte(&ptepg))                             /* map in PTE */
    return cpu_vma_fault(65535,ptemiss,-1,ptepg,faultab,faultpc); /* oops, must init PTE */

/* ok, we have the PTE mapped to page31 */
/* the microcode tries to reads two consecutive data pages into page30 and page31 */

/* read the 1st page value from PTE */
p30 = ReadW(page31 | pgidx) ^ suit;
if (!p30)                                               /* matched suit for 1st page */
    return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc);

/* suit switch situation: 1st page is in last idx of PTE, then following page
 * must be in idx 0 of PTE */
if (pgidx==01777) {                                     /* suit switch situation */
    pgidx = 0;                                          /* select correct idx 0 */
    suit = (HP_WORD) (pagid + 1 & D16_MASK);            /* suit needs increment with wraparound */
    if (suit==0) {                                      /* is it page 65536? */
        offset += 02000;                                /* adjust to 2nd page */
        suit = NILPAGE;
        pgidx = 01777;
        }
} else
    pgidx++;                                            /* select next page */

p31 = ReadW(page31 | pgidx) ^ suit;
if (!p31) {                                             /* matched suit for 2nd page */
    p31 = suit;                                         /* restore the suit number */
    meu_write_map(User_Map, 31, p30);
    if (p30 & SUITMASK)
        return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc);
    if (!(p31 ^ NILPAGE))                               /* suit is 63: fault */
        return cpu_vma_fault(pagid+1,page31,31,ptepg,faultab,faultpc);

    offset += 02000;                                    /* adjust offset to last user map because */
                                                        /* the address requested page 76xxx */
    }
else {
    meu_write_map(User_Map, 30, p30);
    if (p30 & SUITMASK)
        return cpu_vma_fault(pagid,page30,30,ptepg,faultab,faultpc);
    meu_write_map(User_Map, 31, p31);
    if (p31 & SUITMASK)
        return cpu_vma_fault(pagid+1,page31,31,ptepg,faultab,faultpc);
    }

AR = (HP_WORD) pagid;                                   /* return pagid in A */
BR = (HP_WORD) (page30 + offset);                       /* mapped address in B */
return SCPE_OK;
}


/*  .PMAP
    ASSEMBLER CALLING SEQUENCE:

    LDA UMAPR          (MSEG - 31)
    LDB PAGID          (0-65535)
    JSB .PMAP          GO MAP IT IN
    <ERROR RETURN>     A-REG = REASON, NOTE 1
    <RETURN A=A+1, B=B+1,E=0 >> SEE NOTE 2>

    NOTE 1 : IF BIT 15 OF A-REG SET, THEN ALL NORMAL BRANCHES TO THE
          $EMA$/$VMA$ CODE WILL BE CHANGED TO P+1 EXIT.  THE A-REG
          WILL BE THE REASON THE MAPPING WAS NOT SUCCESSFUL IF BIT 15
          OF THE A-REG WAS NOT SET.
          THIS WAS DONE SO THAT A ROUTINE ($VMA$) CAN DO A MAPPING
          WITHOUT THE POSSIBILITY OF BEING RE-CURRED.  IT IS USED
          BY $VMA$ AND PSTVM IN THE PRIVLEDGED MODE.
    NOTE 2: E-REG WILL = 1 IF THE LAST+1 PAGE IS REQUESTED AND
            MAPPED READ/WRITE PROTECTED ON A GOOD P+2 RETURN.
*/

static t_stat cpu_vma_pmap(uint32 umapr, uint32 pagid)
{
uint32 physpg, ptr, pgpte;
uint32 mapnm = umapr & 0x7fff;                          /* strip off bit 15 */

if (mapnm > 31) {                                       /* check for invalid map register */
    AR = 80;                                            /* error: corrupt EMA/VMA system */

    tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  invalid map register %u\n",
             PR, IR, mapnm);

    return SCPE_OK;                                     /* return exit P+1 */
    }

ptr = TO_DWORD (umapr, pagid);                          /* build the ptr argument for vma_fault */

if (!cpu_vma_mapte(&pgpte)) {                           /* map the PTE */
    if (umapr & 0x8000) {
        XR = 65535;
        YR = ptemiss;
        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  page fault exit\n",
                 PR, IR);
        return SCPE_OK;                                 /* use P+1 error exit */
        }

    return cpu_vma_fault(65535,ptemiss,-1,pgpte,ptr,PR-1);  /* oops: fix PTE */
    }

/* PTE is successfully mapped to page31 and dmsmap[63] */

if (!cpu_vma_ptevl(pagid,&physpg)) {
    if (umapr & 0x8000) {
        XR = (HP_WORD) pagid;
        YR = page31;
        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  page fault exit\n",
                 PR, IR);
        return SCPE_OK;                                 /* use P+1 error exit*/
        }
    return cpu_vma_fault(pagid,page31,31,pgpte,ptr,PR-1);   /* page not present */
    }

E = 1;
if (physpg == 0)                                        /* last+1 page ? */
    physpg = RWPROT;                                    /* yes, use page 1023 RW/Protected */
else E = 0;                                             /* normal page to map */

meu_write_map(User_Map, mapnm, physpg);                 /* map page to user page reg */
if (mapnm != 31)                                        /* unless already unmapped, */
    meu_write_map(User_Map, 31, RWPROT);                /* unmap PTE */

AR = (umapr + 1) & R_MASK;                              /* increment mapr for next call */
BR = (pagid + 1) & R_MASK;                              /* increment pagid for next call */
O = 0;                                                  /* clear overflow */
PR = (PR + 1) & LA_MASK;                                /* normal P+2 return */
return SCPE_OK;
}


/* array calc helper for .imar, .jmar, .imap, .jmap
   ij=in_s: 16 bit descriptors
   ij=in_d: 32 bit descriptors

   This helper expects mainly the following arguments:
   dtbl: pointer to an array descriptor table
   atbl: pointer to the table of actual subscripts

   where subscript table is the following:
   atbl-> DEF last_subscript,I      (point to single or double integer)
          ...
          DEF first subscript,I     (point to single or double integer)

   where Descriptor_table is the following table:
   dtbl-> DEC #dimensions
          DEC/DIN next-to-last dimension    (single or double integer)
          ...
          DEC/DIN first dimension           (single or double integer)
          DEC elementsize in words
          DEC high,low offset from start of EMA to element(0,0...0)

   Note that subscripts are counting from 0


   Implementation notes:

    1. "mem_fast_read" calls are used to read memory when tracing so that the
       reads themselves are not traced (they are not part of the instruction
       memory accesses).
*/

static t_stat cpu_vma_ijmar(OPSIZE ij, uint32 dtbl, uint32 atbl, uint32* dimret)
{
t_stat reason = SCPE_OK;
uint32 ndim,i,j,value,ws;
int32 accu,ax,dx;
OP din;
uint32 opsz = (ij == in_d ? 2 : 1);

ndim = ReadW(dtbl++);                                   /* get #dimensions itself */

if (TRACING (cpu_dev, TRACE_OPND)) {
    hp_trace (&cpu_dev, TRACE_OPND, OPND_FORMAT "  dimension count %u, subscript size %d\n",
              PR, IR, ndim, opsz);

    for (i = ndim; i > 0; i--) {                        /* subscripts appear in 3, 2, 1 order */
        MR = mem_fast_read (atbl + i - 1, Current_Map); /* get the pointer to the subscript */

        reason = cpu_resolve_indirects (FALSE);         /* resolve indirects (uninterruptible) */

        if (reason != SCPE_OK)                          /* if resolution failed */
            return reason;                              /*   then return the reason */

        for (value = j = 0; j < opsz; j++)              /* assemble the subscript */
            value = value << DV_WIDTH                   /*   which may be 1 or 2 words in size */
                      | mem_fast_read (MR + j, Current_Map);

        hp_trace (&cpu_dev, TRACE_OPND, OPND_FORMAT "  subscript %u is %u\n",
                  PR, IR, ndim - i + 1, value);
        }

    if (ndim != 0) {
        for (i = ndim; i > 1; i--) {                    /* dimensions appear in 3, 2 order */
            for (value = j = 0; j < opsz; j++)          /* assemble the element count */
                value = value << DV_WIDTH               /*   which may be 1 or 2 words in size */
                          | mem_fast_read (dtbl + (i - 2) * opsz + j, Current_Map);

            hp_trace (&cpu_dev, TRACE_OPND, OPND_FORMAT "  dimension %u element count %u\n",
                      PR, IR, ndim - i + 1, value);
            }

        i = dtbl+1+(ndim-1)*opsz;
        ws = mem_fast_read (i - 1, Current_Map);
        }

    else {
        i = dtbl;
        ws = 1;
        }

    value = TO_DWORD (mem_fast_read (i, Current_Map),
                      mem_fast_read (i + 1, Current_Map));

    hp_trace (&cpu_dev, TRACE_OPND, OPND_FORMAT "  element size %u offset %011o\n",
              PR, IR, ws, value);
    }

if (dimret) *dimret = ndim;                             /* return dimensions */
if (ndim == 0) {                                        /* no dimensions:  */
    AR = ReadW(dtbl++);                                 /* return the array base itself */
    BR = ReadW(dtbl);
    return SCPE_OK;
    }

/* calculate
 *  (...(An*Dn-1)+An-1)*Dn-2)+An-2....)+A2)*D1)+A1)*#words + Array base
 * Depending on ij, Ax and Dx can be 16 or 32 bit
 */
accu = 0;
while (ndim-- > 0) {
    MR = ReadW(atbl++);                                 /* get addr of subscript */
    reason = cpu_resolve_indirects (TRUE);              /* resolve indirects */
    if (reason)
        return reason;
    din = ReadOp(MR,ij);                                /* get actual subscript value */
    ax = ij==in_d ? INT32(din.dword) : INT16(din.word);
    accu += ax;                                         /* add to accu */

    if (ndim==0) ij = in_s;                             /* #words is single */
    din = ReadOp(dtbl,ij);                              /* get dimension from descriptor table */
    if (ij==in_d) {
        dx = INT32(din.dword);                          /* either get double or single dimension */
        dtbl += 2;
    } else {
        dx = INT16(din.word);
        dtbl++;
        }
    accu *= dx;                                         /* multiply */
    }

din = ReadOp(dtbl,in_d);                                /* add base address */
accu += din.dword;

AR = UPPER_WORD (accu);                              /* transfer to AB */
BR = LOWER_WORD (accu);

tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  virtual address %011o\n",
         PR, IR, accu);

return reason;
}
