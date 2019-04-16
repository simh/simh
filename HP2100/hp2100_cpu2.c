/* hp2100_cpu2.c: HP 1000 DMS and EIG microcode simulator

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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU2         Dynamic Mapping System and Extended Instruction Set instructions

   02-Oct-18    JDB     Replaced DMASK with D16_MASK or R_MASK as appropriate
   02-Aug-18    JDB     Moved FP and IOP dispatchers to hp2100_cpu1.c
   30-Jul-18    JDB     Renamed "dms_[rw]map" to "meu_read_map", "meu_write_map"
   24-Jul-18    JDB     Removed unneeded "iotrap" parameter from "cpu_iog" routine
   07-Sep-17    JDB     Removed unnecessary "uint16" casts
   10-Jul-17    JDB     Renamed the global routine "iogrp" to "cpu_iog"
   26-Jun-17    JDB     Replaced SEXT with SEXT16
   22-Mar-17    JDB     Corrected comments regarding IR bit 11 selecting A/B
   25-Jan-17    JDB     Set mp_mem_changed whenever MEM registers are changed
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   09-May-12    JDB     Separated assignments from conditional expressions
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   05-Aug-08    JDB     Updated mp_dms_jmp calling sequence
                        Fixed DJP, SJP, and UJP jump target validation
   30-Jul-08    JDB     RVA/B conditionally updates dms_vr before returning value
   19-Dec-06    JDB     DMS self-test now executes as NOP on 1000-M
   01-Dec-06    JDB     Substitutes FPP for firmware FP if HAVE_INT64
   26-Sep-06    JDB     Moved from hp2100_cpu1.c to simplify extensions
   22-Feb-05    JDB     Fixed missing MPCK on JRS target
   21-Jan-05    JDB     Reorganized CPU option and operand processing flags
                        Split code along microcode modules
   15-Jan-05    RMS     Cloned from hp2100_cpu.c

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
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* Dynamic Mapping System.

   The 1000 Dynamic Mapping System (DMS) consisted of the 12731A Memory
   Expansion Module (MEM) card and 38 instructions to expand the basic 32K
   logical address space to a 1024K physical space.  The MEM provided four maps
   of 32 mapping registers each: a system map, a user map, and two DCPC maps.
   DMS worked in conjunction with memory protect to provide a "protected mode"
   in which memory read and write violations could be trapped, and that
   inhibited "privileged" instruction execution that attempted to alter the
   memory mapping.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A    12976B  13307B   std

   The instruction codes are mapped to routines as follows:

     Instr.  1000-M  1000-E/F   Instr.   1000-M  1000-E/F
     ------  ------  --------   ------   ------  --------
     10x700  [xmm]    [xmm]     10x720    XMM      XMM
     10x701  [nop]    [test]    10x721    XMS      XMS
     10x702   MBI      MBI      10x722    XM*      XM*
     10x703   MBF      MBF      10x723   [nop]    [nop]
     10x704   MBW      MBW      10x724    XL*      XL*
     10x705   MWI      MWI      10x725    XS*      XS*
     10x706   MWF      MWF      10x726    XC*      XC*
     10x707   MWW      MWW      10x727    LF*      LF*
     10x710   SY*      SY*      10x730    RS*      RS*

     10x711   US*      US*      10x731    RV*      RV*
     10x712   PA*      PA*      10x732    DJP      DJP
     10x713   PB*      PB*      10x733    DJS      DJS
     10x714   SSM      SSM      10x734    SJP      SJP
     10x715   JRS      JRS      10x735    SJS      SJS
     10x716  [nop]    [nop]     10x736    UJP      UJP
     10x717  [nop]    [nop]     10x737    UJS      UJS

   Instructions that use IR bit 11 to select the A or B register are designated
   with a * above (e.g., 101710 is SYA, and 105710 is SYB).  For those that do
   not use this feature, either the 101xxx or 105xxx code will execute the
   corresponding instruction, although the 105xxx form is the documented
   instruction code.


   Implementation notes:

    1. Instruction code 10x700 will execute the XMM instruction, although 10x720
       is the documented instruction value.

    2. Instruction code 10x701 will complement the A or B register, as
       indicated, on 1000-E and F-Series machines.  This instruction is a NOP on
       M-Series machines.

    3. The DMS privilege violation rules are:
       - load map and CTL5 set (XMM, XMS, XM*, SY*, US*, PA*, PB*)
       - load state or fence and UMAP set (JRS, DJP, DJS, SJP, SJS, UJP, UJS, LF*)

    4. DM (write) violations for the use of the MBI, MWI, MBW, MWW, XSA, and XSB
       instructions in protected mode are generated by the mem_write routine.

    5. The protected memory lower bound for the DJP, SJP, UJP, and JRS
       instructions is 2.
*/

static const OP_PAT op_dms[32] = {
  OP_N,    OP_N,    OP_N,    OP_N,                      /* [xmm]  [test] MBI    MBF   */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* MBW    MWI    MWF    MWW   */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* SYA/B  USA/B  PAA/B  PBA/B */
  OP_A,    OP_KA,   OP_N,    OP_N,                      /* SSM    JRS    nop    nop   */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* XMM    XMS    XMA/B  nop   */
  OP_A,    OP_A,    OP_A,    OP_N,                      /* XLA/B  XSA/B  XCA/B  LFA/B */
  OP_N,    OP_N,    OP_A,    OP_A,                      /* RSA/B  RVA/B  DJP    DJS   */
  OP_A,    OP_A,    OP_A,    OP_A                       /* SJP    SJS    UJP    UJS   */
  };

t_stat cpu_dms (uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte;
uint32 entry, absel, i;
HP_WORD t;
MEU_STATE operation;
MEU_MAP_SELECTOR mapi, mapj;

absel = AB_SELECT (IR);                                 /* get the A/B register selector */
entry = IR & 037;                                       /* mask to entry point */

if (op_dms [entry] != OP_N) {
    reason = cpu_ops (op_dms [entry], op);              /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

/* DMS module 1 */

    case 000:                                           /* [undefined] 105700 (OP_N) */
        goto XMM;                                       /* decodes as XMM */

    case 001:                                           /* [self test] 105701 (OP_N) */
        if (!(cpu_configuration & CPU_1000_M))          /* executes as NOP on 1000-M */
            ABREG[absel] = ~ABREG[absel];               /* CMA or CMB */
        break;

    case 002:                                           /* MBI 105702 (OP_N) */
        AR = AR & ~1;                                   /* force A, B even */
        BR = BR & ~1;
        while (XR != 0) {                               /* loop */
            byte = ReadB (AR);                          /* read curr */
            WriteBA (BR, byte);                         /* write alt */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 003:                                           /* MBF 105703 (OP_N) */
        AR = AR & ~1;                                   /* force A, B even */
        BR = BR & ~1;
        while (XR != 0) {                               /* loop */
            byte = ReadBA (AR);                         /* read alt */
            WriteB (BR, byte);                          /* write curr */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 004:                                           /* MBW 105704 (OP_N) */
        AR = AR & ~1;                                   /* force A, B even */
        BR = BR & ~1;
        while (XR != 0) {                               /* loop */
            byte = ReadBA (AR);                         /* read alt */
            WriteBA (BR, byte);                         /* write alt */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 005:                                           /* MWI 105705 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadW (AR & LA_MASK);                   /* read curr */
            WriteWA (BR & LA_MASK, t);                  /* write alt */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 006:                                           /* MWF 105706 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadWA (AR & LA_MASK);                  /* read alt */
            WriteW (BR & LA_MASK, t);                   /* write curr */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 007:                                           /* MWW 105707 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadWA (AR & LA_MASK);                  /* read alt */
            WriteWA (BR & LA_MASK, t);                  /* write alt */
            AR = (AR + 1) & R_MASK;                     /* incr ptrs */
            BR = (BR + 1) & R_MASK;
            XR = (XR - 1) & R_MASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 010:                                           /* SYA, SYB 10x710 (OP_N) */
    case 011:                                           /* USA, USB 10x711 (OP_N) */
    case 012:                                           /* PAA, PAB 10x712 (OP_N) */
    case 013:                                           /* PBA, PBB 10x713 (OP_N) */
        mapi = TO_MAP_SELECTOR (IR);                    /* map base */
        if (ABREG[absel] & D16_SIGN) {                  /* store? */
            for (i = 0; i < MEU_REG_COUNT; i++) {
                t = meu_read_map (mapi, i);             /* map to memory */
                WriteW ((ABREG[absel] + i) & LA_MASK, t);
                }
            }
        else {                                          /* load */
            meu_privileged (Always);                    /* priv if PRO */
            for (i = 0; i < MEU_REG_COUNT; i++) {
                t = ReadW ((ABREG[absel] + i) & LA_MASK);
                meu_write_map (mapi, i, t);             /* mem to map */
                }
            }
        ABREG[absel] = (ABREG[absel] + MEU_REG_COUNT) & R_MASK;
        break;

    case 014:                                           /* SSM 105714 (OP_A) */
        WriteW (op[0].word, meu_update_status ());      /* store stat */
        break;

    case 015:                                           /* JRS 105715 (OP_KA) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */

        if (op[0].word & 0100000)                       /* bit 15 0/1 = disable/enable MEM */
            operation = ME_Enabled;
        else
            operation = ME_Disabled;

        if (op[0].word & 0040000)                       /* if bit 14 is set */
            meu_set_state (operation, User_Map);        /*   then select the user map */
        else                                            /* otherwise */
            meu_set_state (operation, System_Map);      /*   select the system map */

        mp_check_jmp (op[1].word, 2);                   /* mpck jmp target */
        PCQ_ENTRY;                                      /* save old P */
        PR = op[1].word;                                /* jump */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

/* DMS module 2 */

    case 020:                                           /* XMM 105720 (OP_N) */
    XMM:
        if (XR == 0) break;                             /* nop? */
        while (XR != 0) {                               /* loop */
            if (XR & D16_SIGN) {                        /* store? */
                t = meu_read_map (Linear_Map, AR);      /* map to mem */
                WriteW (BR & LA_MASK, t);
                XR = (XR + 1) & R_MASK;
                }
            else {                                      /* load */
                meu_privileged (Always);                /* priv viol if prot */
                t = ReadW (BR & LA_MASK);               /* mem to map */
                meu_write_map (Linear_Map, AR, t);
                XR = (XR - 1) & R_MASK;
                }
            AR = (AR + 1) & R_MASK;
            BR = (BR + 1) & R_MASK;
            if (intrq && ((XR & 017) == 017)) {         /* intr, grp of 16? */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 021:                                           /* XMS 105721 (OP_N) */
        if ((XR & D16_SIGN) || (XR == 0)) break;        /* nop? */
        meu_privileged (Always);                        /* priv viol if prot */
        while (XR != 0) {
            meu_write_map (Linear_Map, AR, BR);         /* AR to map */
            XR = (XR - 1) & R_MASK;
            AR = (AR + 1) & R_MASK;
            BR = (BR + 1) & R_MASK;
            if (intrq && ((XR & 017) == 017)) {         /* intr, grp of 16? */
                PR = err_PR;
                break;
                }
            }
        break;

    case 022:                                           /* XMA, XMB 10x722 (OP_N) */
        meu_privileged (Always);                        /* priv viol if prot */

        if (ABREG [absel] & 0100000)
            mapi = User_Map;
        else
            mapi = System_Map;

        if (ABREG [absel] & 0000001)
            mapj = Port_B_Map;
        else
            mapj = Port_A_Map;

        for (i = 0; i < MEU_REG_COUNT; i++) {
            t = meu_read_map (mapi, i);                 /* read map */
            meu_write_map (mapj, i, t);                 /* write map */
            }
        break;

    case 024:                                           /* XLA, XLB 10x724 (OP_A) */
        ABREG[absel] = ReadWA (op[0].word);             /* load alt */
        break;

    case 025:                                           /* XSA, XSB 10x725 (OP_A) */
        WriteWA (op[0].word, ABREG[absel]);             /* store alt */
        break;

    case 026:                                           /* XCA, XCB 10x726 (OP_A) */
        if (ABREG[absel] != ReadWA (op[0].word))        /* compare alt */
            PR = (PR + 1) & LA_MASK;
        break;

    case 027:                                           /* LFA, LFB 10x727 (OP_N) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        meu_set_fence (ABREG [absel]);                  /* load the MEM fence register */
        break;

    case 030:                                           /* RSA, RSB 10x730 (OP_N) */
        ABREG [absel] = meu_update_status ();           /* save stat */
        break;

    case 031:                                           /* RVA, RVB 10x731 (OP_N) */
        ABREG [absel] = meu_update_violation ();        /* return updated violation register */
        break;

    case 032:                                           /* DJP 105732 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        meu_set_state (ME_Disabled, System_Map);        /* disable MEM and switch to the system map */
        mp_check_jmp (op[0].word, 2);                   /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* new P */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    case 033:                                           /* DJS 105733 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        WriteW (op[0].word, PR);                        /* store ret addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & LA_MASK;                /* new P */
        meu_set_state (ME_Disabled, System_Map);        /* disable MEM and switch to the system map */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    case 034:                                           /* SJP 105734 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        meu_set_state (ME_Enabled, System_Map);         /* enable MEM and switch to the system map */
        mp_check_jmp (op[0].word, 2);                   /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* jump */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    case 035:                                           /* SJS 105735 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        t = PR;                                         /* save retn addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & LA_MASK;                /* new P */
        meu_set_state (ME_Enabled, System_Map);         /* enable MEM and switch to the system map */
        WriteW (op[0].word, t);                         /* store ret addr */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    case 036:                                           /* UJP 105736 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        meu_set_state (ME_Enabled, User_Map);           /* enable MEM and switch to the user map */
        mp_check_jmp (op[0].word, 2);                   /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* jump */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    case 037:                                           /* UJS 105737 (OP_A) */
        meu_privileged (If_User_Map);                   /* priv viol if prot */
        t = PR;                                         /* save retn addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & LA_MASK;                /* new P */
        meu_set_state (ME_Enabled, User_Map);           /* enable MEM and switch to the user map */
        WriteW (op[0].word, t);                         /* store ret addr */
        cpu_interrupt_enable = CLEAR;                   /* disable interrupts */
        break;

    default:                                            /* others NOP */
        break;
        }

return reason;
}


/* Extended Instruction Group

   The Extended Instruction Group (EIG) adds 32 index and 10 bit/byte/word
   manipulation instructions to the 1000 base set.  These instructions
   use the new X and Y index registers that were added to the 1000.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     std     std     std

   The instruction codes are mapped to routines as follows:

     Instr.  1000-M/E/F   Instr.   1000-M/E/F
     ------  ----------   ------   ----------
     10x740     S*X       10x760      ISX
     10x741     C*X       10x761      DSX
     10x742     L*X       10x762      JLY
     10x743     STX       10x763      LBT
     10x744     CX*       10x764      SBT
     10x745     LDX       10x765      MBT
     10x746     ADX       10x766      CBT
     10x747     X*X       10x767      SFB

     10x750     S*Y       10x770      ISY
     10x751     C*Y       10x771      DSY
     10x752     L*Y       10x772      JPY
     10x753     STY       10x773      SBS
     10x754     CY*       10x774      CBS
     10x755     LDY       10x775      TBS
     10x756     ADY       10x776      CMW
     10x757     X*Y       10x777      MVW

   Instructions that use IR bit 11 to select the A or B register are designated
   with a * above (e.g., 101740 is SAX, and 105740 is SBX).  For those that do
   not use this feature, either the 101xxx or 105xxx code will execute the
   corresponding instruction, although the 105xxx form is the documented
   instruction code.

   Implementation notes:

    1. The LBT, SBT, MBT, and MVW instructions are used as part of the 2100 IOP
       implementation.  When so called, the MBT and MVW instructions have the
       additional restriction that the count must be positive.

    2. The protected memory lower bound for the JLY and JPY instructions is 0.
*/

static const OP_PAT op_eig[32] = {
  OP_A,    OP_N,    OP_A,    OP_A,                      /* S*X    C*X    L*X    STX   */
  OP_N,    OP_K,    OP_K,    OP_N,                      /* CX*    LDX    ADX    X*X   */
  OP_A,    OP_N,    OP_A,    OP_A,                      /* S*Y    C*Y    L*Y    STY   */
  OP_N,    OP_K,    OP_K,    OP_N,                      /* CY*    LDY    ADY    X*Y   */
  OP_N,    OP_N,    OP_A,    OP_N,                      /* ISX    DSX    JLY    LBT   */
  OP_N,    OP_KV,   OP_KV,   OP_N,                      /* SBT    MBT    CBT    SFB   */
  OP_N,    OP_N,    OP_C,    OP_KA,                     /* ISY    DSY    JPY    SBS   */
  OP_KA,   OP_KK,   OP_KV,   OP_KV                      /* CBS    TBS    CMW    MVW   */
  };

t_stat cpu_eig (HP_WORD IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte, b1, b2;
uint32 entry, absel, sum;
HP_WORD t, v1, v2, wc;
int32 sop1, sop2;

absel = AB_SELECT (IR);                                 /* get the A/B register selector */
entry = IR & 037;                                       /* mask to entry point */

if (op_eig [entry] != OP_N) {
    reason = cpu_ops (op_eig [entry], op);              /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<4:0> */

/* EIG module 1 */

    case 000:                                           /* SAX, SBX 10x740 (OP_A) */
        op[0].word = (op[0].word + XR) & LA_MASK;       /* indexed addr */
        WriteW (op[0].word, ABREG[absel]);              /* store */
        break;

    case 001:                                           /* CAX, CBX 10x741 (OP_N) */
        XR = ABREG[absel];                              /* copy to XR */
        break;

    case 002:                                           /* LAX, LBX 10x742 (OP_A) */
        op[0].word = (op[0].word + XR) & LA_MASK;       /* indexed addr */
        ABREG[absel] = ReadW (op[0].word);              /* load */
        break;

    case 003:                                           /* STX 105743 (OP_A) */
        WriteW (op[0].word, XR);                        /* store XR */
        break;

    case 004:                                           /* CXA, CXB 10x744 (OP_N) */
        ABREG [absel] = XR;                             /* copy from XR */
        break;

    case 005:                                           /* LDX 105745 (OP_K)*/
        XR = op[0].word;                                /* load XR */
        break;

    case 006:                                           /* ADX 105746 (OP_K) */
        sum = XR + op[0].word;                          /* add to XR */
        if (sum > D16_UMAX) E = 1;                      /* set E, O */
        if (((~XR ^ op[0].word) & (XR ^ sum)) & D16_SIGN) O = 1;
        XR = sum & R_MASK;
        break;

    case 007:                                           /* XAX, XBX 10x747 (OP_N) */
        t = XR;                                         /* exchange XR */
        XR = ABREG [absel];
        ABREG [absel] = t;
        break;

    case 010:                                           /* SAY, SBY 10x750 (OP_A) */
        op[0].word = (op[0].word + YR) & LA_MASK;       /* indexed addr */
        WriteW (op[0].word, ABREG[absel]);              /* store */
        break;

    case 011:                                           /* CAY, CBY 10x751 (OP_N) */
        YR = ABREG[absel];                              /* copy to YR */
        break;

    case 012:                                           /* LAY, LBY 10x752 (OP_A) */
        op[0].word = (op[0].word + YR) & LA_MASK;       /* indexed addr */
        ABREG[absel] = ReadW (op[0].word);              /* load */
        break;

    case 013:                                           /* STY 105753 (OP_A) */
        WriteW (op[0].word, YR);                        /* store YR */
        break;

    case 014:                                           /* CYA, CYB 10x754 (OP_N) */
        ABREG [absel] = YR;                             /* copy from YR */
        break;

    case 015:                                           /* LDY 105755 (OP_K) */
        YR = op[0].word;                                /* load YR */
        break;

    case 016:                                           /* ADY 105756 (OP_K) */
        sum = YR + op[0].word;                          /* add to YR */
        if (sum > D16_UMAX) E = 1;                      /* set E, O */
        if (((~YR ^ op[0].word) & (YR ^ sum)) & D16_SIGN) O = 1;
        YR = sum & R_MASK;
        break;

    case 017:                                           /* XAY, XBY 10x757 (OP_N) */
        t = YR;                                         /* exchange YR */
        YR = ABREG [absel];
        ABREG [absel] = t;
        break;

/* EIG module 2 */

    case 020:                                           /* ISX 105760 (OP_N) */
        XR = (XR + 1) & R_MASK;                         /* incr XR */
        if (XR == 0) PR = (PR + 1) & LA_MASK;           /* skip if zero */
        break;

    case 021:                                           /* DSX 105761 (OP_N) */
        XR = (XR - 1) & R_MASK;                         /* decr XR */
        if (XR == 0) PR = (PR + 1) & LA_MASK;           /* skip if zero */
        break;

    case 022:                                           /* JLY 105762 (OP_A) */
        mp_check_jmp (op[0].word, 0);                   /* validate jump addr */
        PCQ_ENTRY;
        YR = PR;                                        /* ret addr to YR */
        PR = op[0].word;                                /* jump */
        break;

    case 023:                                           /* LBT 105763 (OP_N) */
        AR = ReadB (BR);                                /* load byte */
        BR = (BR + 1) & R_MASK;                         /* incr ptr */
        break;

    case 024:                                           /* SBT 105764 (OP_N) */
        WriteB (BR, LOWER_BYTE (AR));                   /* store byte */
        BR = (BR + 1) & R_MASK;                         /* incr ptr */
        break;

    case 025:                                           /* MBT 105765 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        if ((wc & D16_SIGN) &&
            cpu_configuration & CPU_2100)
            break;                                      /* < 0 is NOP for 2100 IOP */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for MP abort */
            byte = ReadB (AR);                          /* move byte */
            WriteB (BR, byte);
            AR = (AR + 1) & R_MASK;                     /* incr src */
            BR = (BR + 1) & R_MASK;                     /* incr dst */
            wc = (wc - 1) & D16_MASK;                   /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PR;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

    case 026:                                           /* CBT 105766 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for MP abort */
            b1 = ReadB (AR);                            /* get src1 */
            b2 = ReadB (BR);                            /* get src2 */
            if (b1 != b2) {                             /* compare */
                PR = (PR + 1 + (b1 > b2)) & LA_MASK;
                BR = (BR + wc) & R_MASK;                /* update BR */
                wc = 0;                                 /* clr interim */
                break;
                }
            AR = (AR + 1) & R_MASK;                     /* incr src1 */
            BR = (BR + 1) & R_MASK;                     /* incr src2 */
            wc = (wc - 1) & D16_MASK;                   /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PR;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

    case 027:                                           /* SFB 105767 (OP_N) */
        b1 = LOWER_BYTE (AR);                           /* test byte */
        b2 = UPPER_BYTE (AR);                           /* term byte */
        for (;;) {                                      /* scan */
            byte = ReadB (BR);                          /* read byte */
            if (byte == b1) break;                      /* test match? */
            BR = (BR + 1) & R_MASK;
            if (byte == b2) {                           /* term match? */
                PR = (PR + 1) & LA_MASK;
                break;
                }
            if (intrq) {                                /* int pending? */
                PR = err_PR;                            /* back up P */
                break;
                }
            }
        break;

    case 030:                                           /* ISY 105770 (OP_N) */
        YR = (YR + 1) & R_MASK;                         /* incr YR */
        if (YR == 0) PR = (PR + 1) & LA_MASK;           /* skip if zero */
        break;

    case 031:                                           /* DSY 105771 (OP_N) */
        YR = (YR - 1) & R_MASK;                         /* decr YR */
        if (YR == 0) PR = (PR + 1) & LA_MASK;           /* skip if zero */
        break;

    case 032:                                           /* JPY 105772 (OP_C) */
        op[0].word = (op[0].word + YR) & LA_MASK;       /* index, no indir */
        mp_check_jmp (op[0].word, 0);                   /* validate jump addr */
        PCQ_ENTRY;
        PR = op[0].word;                                /* jump */
        break;

    case 033:                                           /* SBS 105773 (OP_KA) */
        WriteW (op[1].word,                             /* set bits */
                ReadW (op[1].word) | op[0].word);
        break;

    case 034:                                           /* CBS 105774 (OP_KA) */
        WriteW (op[1].word,                             /* clear bits */
                ReadW (op[1].word) & ~op[0].word);
        break;

    case 035:                                           /* TBS 105775 (OP_KK) */
        if ((op[1].word & op[0].word) != op[0].word)    /* test bits */
            PR = (PR + 1) & LA_MASK;
        break;

    case 036:                                           /* CMW 105776 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for abort */
            v1 = ReadW (AR & LA_MASK);                  /* first op */
            v2 = ReadW (BR & LA_MASK);                  /* second op */
            sop1 = SEXT16 (v1);                         /* signed */
            sop2 = SEXT16 (v2);
            if (sop1 != sop2) {                         /* compare */
                PR = (PR + 1 + (sop1 > sop2)) & LA_MASK;
                BR = (BR + wc) & R_MASK;                /* update BR */
                wc = 0;                                 /* clr interim */
                break;
                }
            AR = (AR + 1) & R_MASK;                     /* incr src1 */
            BR = (BR + 1) & R_MASK;                     /* incr src2 */
            wc = (wc - 1) & D16_MASK;                   /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PR;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

    case 037:                                           /* MVW 105777 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        if ((wc & D16_SIGN) &&
            cpu_configuration & CPU_2100)
            break;                                      /* < 0 is NOP for 2100 IOP */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for abort */
            t = ReadW (AR & LA_MASK);                   /* move word */
            WriteW (BR & LA_MASK, t);
            AR = (AR + 1) & R_MASK;                     /* incr src */
            BR = (BR + 1) & R_MASK;                     /* incr dst */
            wc = (wc - 1) & D16_MASK;                   /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PR;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

        }

return reason;
}
