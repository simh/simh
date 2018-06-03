/* hp2100_cpu2.c: HP 2100/1000 FP/DMS/EIG/IOP instructions

   Copyright (c) 2005-2016, Robert M. Supnik
   Copyright (c) 2017       J. David Bryan

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

   CPU2         Floating-point, dynamic mapping, extended, and I/O processor
                instructions

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

#if !defined (HAVE_INT64)                               /* int64 support unavailable */

#include "hp2100_fp.h"


/* Single-Precision Floating Point Instructions

   The 2100 and 1000 CPUs share the single-precision (two word) floating-point
   instruction codes.  Floating-point firmware was an option on the 2100 and was
   standard on the 1000-M and E.  The 1000-F had a standard hardware Floating
   Point Processor that executed these six instructions and added extended- and
   double-precision floating- point instructions, as well as double-integer
   instructions (the FPP is simulated separately).

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A    12901A   std     std     N/A

   The instruction codes for the 2100 and 1000-M/E systems are mapped to
   routines as follows:

     Instr.  2100/1000-M/E   Description
     ------  -------------  -----------------------------------
     105000       FAD       Single real add
     105020       FSB       Single real subtract
     105040       FMP       Single real multiply
     105060       FDV       Single real divide
     105100       FIX       Single integer to single real fix
     105120       FLT       Single real to single integer float

   Bits 3-0 are not decoded by these instructions, so FAD (e.g.) would be
   executed by any instruction in the range 105000-105017.

   Implementation note: rather than have two simulators that each executes the
   single-precision FP instruction set, we compile conditionally, based on the
   availability of 64-bit integer support in the host compiler.  64-bit integers
   are required for the FPP, so if they are available, then the FPP is used to
   handle the six single-precision instructions for the 2100 and M/E-Series, and
   this function is omitted.  If support is unavailable, this function is used
   instead.

   Implementation note: the operands to FAD, etc. are floating-point values, so
   OP_F would normally be used.  However, the firmware FP support routines want
   floating-point operands as 32-bit integer values, so OP_D is used to achieve
   this.
*/

static const OP_PAT op_fp[8] = {
  OP_D,    OP_D,    OP_D,    OP_D,                      /*  FAD    FSB    FMP    FDV  */
  OP_N,    OP_N,    OP_N,    OP_N                       /*  FIX    FLT    ---    ---  */
  };

t_stat cpu_fp (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = (IR >> 4) & 017;                                /* mask to entry point */

if (op_fp [entry] != OP_N) {
    reason = cpu_ops (op_fp [entry], op, intrq);        /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<7:4> */

    case 000:                                           /* FAD 105000 (OP_D) */
        O = f_as (op[0].dword, 0);                      /* add, upd ovflo */
        break;

    case 001:                                           /* FSB 105020 (OP_D) */
        O = f_as (op[0].dword, 1);                      /* sub, upd ovflo */
        break;

    case 002:                                           /* FMP 105040 (OP_D) */
        O = f_mul (op[0].dword);                        /* mul, upd ovflo */
        break;

    case 003:                                           /* FDV 105060 (OP_D) */
        O = f_div (op[0].dword);                        /* div, upd ovflo */
        break;

    case 004:                                           /* FIX 105100 (OP_N) */
        O = f_fix ();                                   /* fix, upd ovflo */
        break;

    case 005:                                           /* FLT 105120 (OP_N) */
        O = f_flt ();                                   /* float, upd ovflo */
        break;

    default:                                            /* should be impossible */
        return SCPE_IERR;
        }

return reason;
}

#endif                                                  /* int64 support unavailable */


/* Dynamic Mapping System

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

t_stat cpu_dms (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte;
uint32 entry, absel, i, mapi, mapj;
HP_WORD t;

absel = (IR & I_AB)? 1: 0;                              /* get A/B select */
entry = IR & 037;                                       /* mask to entry point */

if (op_dms [entry] != OP_N) {
    reason = cpu_ops (op_dms [entry], op, intrq);       /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

/* DMS module 1 */

    case 000:                                           /* [undefined] 105700 (OP_N) */
        goto XMM;                                       /* decodes as XMM */

    case 001:                                           /* [self test] 105701 (OP_N) */
        if (UNIT_CPU_MODEL != UNIT_1000_M)              /* executes as NOP on 1000-M */
            ABREG[absel] = ~ABREG[absel];               /* CMA or CMB */
        break;

    case 002:                                           /* MBI 105702 (OP_N) */
        AR = AR & ~1;                                   /* force A, B even */
        BR = BR & ~1;
        while (XR != 0) {                               /* loop */
            byte = ReadB (AR);                          /* read curr */
            WriteBA (BR, byte);                         /* write alt */
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PC;                            /* stop for now */
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
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PC;                            /* stop for now */
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
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq && !(AR & 1)) {             /* more, int, even? */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 005:                                           /* MWI 105705 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadW (AR & VAMASK);                    /* read curr */
            WriteWA (BR & VAMASK, t);                   /* write alt */
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 006:                                           /* MWF 105706 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadWA (AR & VAMASK);                   /* read alt */
            WriteW (BR & VAMASK, t);                    /* write curr */
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 007:                                           /* MWW 105707 (OP_N) */
        while (XR != 0) {                               /* loop */
            t = ReadWA (AR & VAMASK);                   /* read alt */
            WriteWA (BR & VAMASK, t);                   /* write alt */
            AR = (AR + 1) & DMASK;                      /* incr ptrs */
            BR = (BR + 1) & DMASK;
            XR = (XR - 1) & DMASK;
            if (XR && intrq) {                          /* more and intr? */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 010:                                           /* SYA, SYB 10x710 (OP_N) */
    case 011:                                           /* USA, USB 10x711 (OP_N) */
    case 012:                                           /* PAA, PAB 10x712 (OP_N) */
    case 013:                                           /* PBA, PBB 10x713 (OP_N) */
        mapi = (IR & 03) << VA_N_PAG;                   /* map base */
        if (ABREG[absel] & SIGN) {                      /* store? */
            for (i = 0; i < MAP_LNT; i++) {
                t = dms_rmap (mapi + i);                /* map to memory */
                WriteW ((ABREG[absel] + i) & VAMASK, t);
                }
            }
        else {                                          /* load */
            dms_viol (err_PC, MVI_PRV);                 /* priv if PRO */
            for (i = 0; i < MAP_LNT; i++) {
                t = ReadW ((ABREG[absel] + i) & VAMASK);
                dms_wmap (mapi + i, t);                 /* mem to map */
                }
            }
        ABREG[absel] = (ABREG[absel] + MAP_LNT) & DMASK;
        break;

    case 014:                                           /* SSM 105714 (OP_A) */
        WriteW (op[0].word, dms_upd_sr ());             /* store stat */
        break;

    case 015:                                           /* JRS 105715 (OP_KA) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        dms_enb = 0;                                    /* assume off */
        dms_ump = SMAP;
        if (op[0].word & 0100000) {                     /* set enable? */
            dms_enb = 1;
            if (op[0].word & 0040000) dms_ump = UMAP;   /* set/clr usr */
            }
        mp_mem_changed = TRUE;                          /* set the MP/MEM registers changed flag */
        mp_dms_jmp (op[1].word, 2);                     /* mpck jmp target */
        PCQ_ENTRY;                                      /* save old P */
        PR = op[1].word;                                /* jump */
        ion_defer = TRUE;                               /* defer intr */
        break;

/* DMS module 2 */

    case 020:                                           /* XMM 105720 (OP_N) */
    XMM:
        if (XR == 0) break;                             /* nop? */
        while (XR != 0) {                               /* loop */
            if (XR & SIGN) {                            /* store? */
                t = dms_rmap (AR);                      /* map to mem */
                WriteW (BR & VAMASK, t);
                XR = (XR + 1) & DMASK;
                }
            else {                                      /* load */
                dms_viol (err_PC, MVI_PRV);             /* priv viol if prot */
                t = ReadW (BR & VAMASK);                /* mem to map */
                dms_wmap (AR, t);
                XR = (XR - 1) & DMASK;
                }
            AR = (AR + 1) & DMASK;
            BR = (BR + 1) & DMASK;
            if (intrq && ((XR & 017) == 017)) {         /* intr, grp of 16? */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 021:                                           /* XMS 105721 (OP_N) */
        if ((XR & SIGN) || (XR == 0)) break;            /* nop? */
        dms_viol (err_PC, MVI_PRV);                     /* priv viol if prot */
        while (XR != 0) {
            dms_wmap (AR, BR);                          /* AR to map */
            XR = (XR - 1) & DMASK;
            AR = (AR + 1) & DMASK;
            BR = (BR + 1) & DMASK;
            if (intrq && ((XR & 017) == 017)) {         /* intr, grp of 16? */
                PR = err_PC;
                break;
                }
            }
        break;

    case 022:                                           /* XMA, XMB 10x722 (OP_N) */
        dms_viol (err_PC, MVI_PRV);                     /* priv viol if prot */
        if (ABREG[absel] & 0100000) mapi = UMAP;
        else mapi = SMAP;
        if (ABREG[absel] & 0000001) mapj = PBMAP;
        else mapj = PAMAP;
        for (i = 0; i < MAP_LNT; i++) {
            t = dms_rmap (mapi + i);                    /* read map */
            dms_wmap (mapj + i, t);                     /* write map */
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
            PR = (PR + 1) & VAMASK;
        break;

    case 027:                                           /* LFA, LFB 10x727 (OP_N) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        dms_sr = (dms_sr & ~(MST_FLT | MST_FENCE)) |
            (ABREG[absel] & (MST_FLT | MST_FENCE));

        mp_mem_changed = TRUE;                          /* set the MP/MEM registers changed flag */
        break;

    case 030:                                           /* RSA, RSB 10x730 (OP_N) */
        ABREG [absel] = dms_upd_sr ();                  /* save stat */
        break;

    case 031:                                           /* RVA, RVB 10x731 (OP_N) */
        ABREG [absel] = dms_upd_vr (err_PC);            /* return updated violation register */
        break;

    case 032:                                           /* DJP 105732 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        dms_enb = 0;                                    /* disable map */
        dms_ump = SMAP;
        mp_dms_jmp (op[0].word, 2);                     /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* new P */
        ion_defer = TRUE;                               /* defer interrupts */
        break;

    case 033:                                           /* DJS 105733 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        WriteW (op[0].word, PR);                        /* store ret addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & VAMASK;                 /* new P */
        dms_enb = 0;                                    /* disable map */
        dms_ump = SMAP;
        ion_defer = TRUE;                               /* defer intr */
        break;

    case 034:                                           /* SJP 105734 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        dms_enb = 1;                                    /* enable system */
        dms_ump = SMAP;
        mp_dms_jmp (op[0].word, 2);                     /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* jump */
        ion_defer = TRUE;                               /* defer intr */
        break;

    case 035:                                           /* SJS 105735 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        t = PR;                                         /* save retn addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & VAMASK;                 /* new P */
        dms_enb = 1;                                    /* enable system */
        dms_ump = SMAP;
        WriteW (op[0].word, t);                         /* store ret addr */
        ion_defer = TRUE;                               /* defer intr */
        break;

    case 036:                                           /* UJP 105736 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        dms_enb = 1;                                    /* enable user */
        dms_ump = UMAP;
        mp_dms_jmp (op[0].word, 2);                     /* validate jump addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = op[0].word;                                /* jump */
        ion_defer = TRUE;                               /* defer intr */
        break;

    case 037:                                           /* UJS 105737 (OP_A) */
        if (dms_ump) dms_viol (err_PC, MVI_PRV);        /* priv viol if prot */
        t = PR;                                         /* save retn addr */
        PCQ_ENTRY;                                      /* save curr P */
        PR = (op[0].word + 1) & VAMASK;                 /* new P */
        dms_enb = 1;                                    /* enable user */
        dms_ump = UMAP;
        WriteW (op[0].word, t);                         /* store ret addr */
        ion_defer = TRUE;                               /* defer intr */
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

t_stat cpu_eig (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte, b1, b2;
uint32 entry, absel, sum;
HP_WORD t, v1, v2, wc;
int32 sop1, sop2;

absel = (IR & I_AB)? 1: 0;                              /* get A/B select */
entry = IR & 037;                                       /* mask to entry point */

if (op_eig [entry] != OP_N) {
    reason = cpu_ops (op_eig [entry], op, intrq);       /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<4:0> */

/* EIG module 1 */

    case 000:                                           /* SAX, SBX 10x740 (OP_A) */
        op[0].word = (op[0].word + XR) & VAMASK;        /* indexed addr */
        WriteW (op[0].word, ABREG[absel]);              /* store */
        break;

    case 001:                                           /* CAX, CBX 10x741 (OP_N) */
        XR = ABREG[absel];                              /* copy to XR */
        break;

    case 002:                                           /* LAX, LBX 10x742 (OP_A) */
        op[0].word = (op[0].word + XR) & VAMASK;        /* indexed addr */
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
        if (sum > DMASK) E = 1;                         /* set E, O */
        if (((~XR ^ op[0].word) & (XR ^ sum)) & SIGN) O = 1;
        XR = sum & DMASK;
        break;

    case 007:                                           /* XAX, XBX 10x747 (OP_N) */
        t = XR;                                         /* exchange XR */
        XR = ABREG [absel];
        ABREG [absel] = t;
        break;

    case 010:                                           /* SAY, SBY 10x750 (OP_A) */
        op[0].word = (op[0].word + YR) & VAMASK;        /* indexed addr */
        WriteW (op[0].word, ABREG[absel]);              /* store */
        break;

    case 011:                                           /* CAY, CBY 10x751 (OP_N) */
        YR = ABREG[absel];                              /* copy to YR */
        break;

    case 012:                                           /* LAY, LBY 10x752 (OP_A) */
        op[0].word = (op[0].word + YR) & VAMASK;        /* indexed addr */
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
        if (sum > DMASK) E = 1;                         /* set E, O */
        if (((~YR ^ op[0].word) & (YR ^ sum)) & SIGN) O = 1;
        YR = sum & DMASK;
        break;

    case 017:                                           /* XAY, XBY 10x757 (OP_N) */
        t = YR;                                         /* exchange YR */
        YR = ABREG [absel];
        ABREG [absel] = t;
        break;

/* EIG module 2 */

    case 020:                                           /* ISX 105760 (OP_N) */
        XR = (XR + 1) & DMASK;                          /* incr XR */
        if (XR == 0) PR = (PR + 1) & VAMASK;            /* skip if zero */
        break;

    case 021:                                           /* DSX 105761 (OP_N) */
        XR = (XR - 1) & DMASK;                          /* decr XR */
        if (XR == 0) PR = (PR + 1) & VAMASK;            /* skip if zero */
        break;

    case 022:                                           /* JLY 105762 (OP_A) */
        mp_dms_jmp (op[0].word, 0);                     /* validate jump addr */
        PCQ_ENTRY;
        YR = PR;                                        /* ret addr to YR */
        PR = op[0].word;                                /* jump */
        break;

    case 023:                                           /* LBT 105763 (OP_N) */
        AR = ReadB (BR);                                /* load byte */
        BR = (BR + 1) & DMASK;                          /* incr ptr */
        break;

    case 024:                                           /* SBT 105764 (OP_N) */
        WriteB (BR, LOWER_BYTE (AR));                   /* store byte */
        BR = (BR + 1) & DMASK;                          /* incr ptr */
        break;

    case 025:                                           /* MBT 105765 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        if ((wc & SIGN) &&
            (UNIT_CPU_TYPE == UNIT_TYPE_2100))
            break;                                      /* < 0 is NOP for 2100 IOP */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for MP abort */
            byte = ReadB (AR);                          /* move byte */
            WriteB (BR, byte);
            AR = (AR + 1) & DMASK;                      /* incr src */
            BR = (BR + 1) & DMASK;                      /* incr dst */
            wc = (wc - 1) & DMASK;                      /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PC;                            /* back up P */
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
                PR = (PR + 1 + (b1 > b2)) & VAMASK;
                BR = (BR + wc) & DMASK;                 /* update BR */
                wc = 0;                                 /* clr interim */
                break;
                }
            AR = (AR + 1) & DMASK;                      /* incr src1 */
            BR = (BR + 1) & DMASK;                      /* incr src2 */
            wc = (wc - 1) & DMASK;                      /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PC;                            /* back up P */
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
            BR = (BR + 1) & DMASK;
            if (byte == b2) {                           /* term match? */
                PR = (PR + 1) & VAMASK;
                break;
                }
            if (intrq) {                                /* int pending? */
                PR = err_PC;                            /* back up P */
                break;
                }
            }
        break;

    case 030:                                           /* ISY 105770 (OP_N) */
        YR = (YR + 1) & DMASK;                          /* incr YR */
        if (YR == 0) PR = (PR + 1) & VAMASK;            /* skip if zero */
        break;

    case 031:                                           /* DSY 105771 (OP_N) */
        YR = (YR - 1) & DMASK;                          /* decr YR */
        if (YR == 0) PR = (PR + 1) & VAMASK;            /* skip if zero */
        break;

    case 032:                                           /* JPY 105772 (OP_C) */
        op[0].word = (op[0].word + YR) & VAMASK;        /* index, no indir */
        mp_dms_jmp (op[0].word, 0);                     /* validate jump addr */
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
            PR = (PR + 1) & VAMASK;
        break;

    case 036:                                           /* CMW 105776 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for abort */
            v1 = ReadW (AR & VAMASK);                   /* first op */
            v2 = ReadW (BR & VAMASK);                   /* second op */
            sop1 = SEXT16 (v1);                         /* signed */
            sop2 = SEXT16 (v2);
            if (sop1 != sop2) {                         /* compare */
                PR = (PR + 1 + (sop1 > sop2)) & VAMASK;
                BR = (BR + wc) & DMASK;                 /* update BR */
                wc = 0;                                 /* clr interim */
                break;
                }
            AR = (AR + 1) & DMASK;                      /* incr src1 */
            BR = (BR + 1) & DMASK;                      /* incr src2 */
            wc = (wc - 1) & DMASK;                      /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PC;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

    case 037:                                           /* MVW 105777 (OP_KV) */
        wc = ReadW (op[1].word);                        /* get continuation count */
        if (wc == 0) wc = op[0].word;                   /* none? get initiation count */
        if ((wc & SIGN) &&
            (UNIT_CPU_TYPE == UNIT_TYPE_2100))
            break;                                      /* < 0 is NOP for 2100 IOP */
        while (wc != 0) {                               /* while count */
            WriteW (op[1].word, wc);                    /* for abort */
            t = ReadW (AR & VAMASK);                    /* move word */
            WriteW (BR & VAMASK, t);
            AR = (AR + 1) & DMASK;                      /* incr src */
            BR = (BR + 1) & DMASK;                      /* incr dst */
            wc = (wc - 1) & DMASK;                      /* decr cnt */
            if (intrq && wc) {                          /* intr, more to do? */
                PR = err_PC;                            /* back up P */
                break;
                }
            }
        WriteW (op[1].word, wc);                        /* clean up inline */
        break;

        }

return reason;
}


/* 2000 I/O Processor

   The IOP accelerates certain operations of the HP 2000 Time-Share BASIC system
   I/O processor.  Most 2000 systems were delivered with 2100 CPUs, although IOP
   microcode was developed for the 1000-M and 1000-E.  As the I/O processors
   were specific to the 2000 system, general compatibility with other CPU
   microcode options was unnecessary, and indeed no other options were possible
   for the 2100.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A    13206A  13207A  22702A   N/A

   The routines are mapped to instruction codes as follows:

     Instr.     2100      1000-M/E   Description
     ------  ----------  ----------  --------------------------------------------
     SAI     105060-117  101400-037  Store A indexed by B (+/- offset in IR<4:0>)
     LAI     105020-057  105400-037  Load A indexed by B (+/- offset in IR<4:0>)
     CRC     105150      105460      Generate CRC
     REST    105340      105461      Restore registers from stack
     READF   105220      105462      Read F register (stack pointer)
     INS       --        105463      Initialize F register (stack pointer)
     ENQ     105240      105464      Enqueue
     PENQ    105257      105465      Priority enqueue
     DEQ     105260      105466      Dequeue
     TRSLT   105160      105467      Translate character
     ILIST   105000      105470      Indirect address list (similar to $SETP)
     PRFEI   105222      105471      Power fail exit with I/O
     PRFEX   105223      105472      Power fail exit
     PRFIO   105221      105473      Power fail I/O
     SAVE    105362      105474      Save registers to stack

     MBYTE   105120      105765      Move bytes (MBT)
     MWORD   105200      105777      Move words (MVW)
     SBYTE   105300      105764      Store byte (SBT)
     LBYTE   105320      105763      Load byte (LBT)

   The INS instruction was not required in the 2100 implementation because the
   stack pointer was actually the memory protect fence register and so could be
   loaded directly with an OTA/B 05.  Also, the 1000 implementation did not
   offer the MBYTE, MWORD, SBYTE, and LBYTE instructions because the equivalent
   instructions from the standard Extended Instruction Group were used instead.

   Note that the 2100 MBYTE and MWORD instructions operate slightly differently
   from the 1000 MBT and MVW instructions.  Specifically, the move count is
   signed on the 2100 and unsigned on the 1000.  A negative count on the 2100
   results in a NOP.

   The simulator remaps the 2100 instructions to the 1000 codes.  The four EIG
   equivalents are dispatched to the EIG simulator.  The rest are handled here.

   Additional reference:
   - HP 2000 Computer System Sources and Listings Documentation
        (22687-90020, undated), section 3, pages 2-74 through 2-91.
*/

static const OP_PAT op_iop[16] = {
  OP_V,    OP_N,    OP_N,    OP_N,                      /* CRC    RESTR  READF  INS   */
  OP_N,    OP_N,    OP_N,    OP_V,                      /* ENQ    PENQ   DEQ    TRSLT */
  OP_AC,   OP_CVA,  OP_A,    OP_CV,                     /* ILIST  PRFEI  PRFEX  PRFIO */
  OP_N,    OP_N,    OP_N,    OP_N                       /* SAVE    ---    ---    ---  */
  };

t_stat cpu_iop (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte;
uint32 entry, i;
HP_WORD hp, tp, t, wc, MA;

if (UNIT_CPU_TYPE == UNIT_TYPE_2100) {                  /* 2100 IOP? */
    if ((IR >= 0105020) && (IR <= 0105057))             /* remap LAI */
        IR = 0105400 | (IR - 0105020);
    else if ((IR >= 0105060) && (IR <= 0105117))        /* remap SAI */
        IR = 0101400 | (IR - 0105060);
    else {
        switch (IR) {                                   /* remap others */
        case 0105000: IR = 0105470; break;              /* ILIST */
        case 0105120: return cpu_eig (0105765, intrq);  /* MBYTE (maps to MBT) */
        case 0105150: IR = 0105460; break;              /* CRC   */
        case 0105160: IR = 0105467; break;              /* TRSLT */
        case 0105200: return cpu_eig (0105777, intrq);  /* MWORD (maps to MVW) */
        case 0105220: IR = 0105462; break;              /* READF */
        case 0105221: IR = 0105473; break;              /* PRFIO */
        case 0105222: IR = 0105471; break;              /* PRFEI */
        case 0105223: IR = 0105472; break;              /* PRFEX */
        case 0105240: IR = 0105464; break;              /* ENQ   */
        case 0105257: IR = 0105465; break;              /* PENQ  */
        case 0105260: IR = 0105466; break;              /* DEQ   */
        case 0105300: return cpu_eig (0105764, intrq);  /* SBYTE (maps to SBT) */
        case 0105320: return cpu_eig (0105763, intrq);  /* LBYTE (maps to LBT) */
        case 0105340: IR = 0105461; break;              /* REST  */
        case 0105362: IR = 0105474; break;              /* SAVE  */

        default:                                        /* all others invalid */
            return STOP (cpu_ss_unimpl);
            }
        }
    }

entry = IR & 077;                                       /* mask to entry point */

if (entry <= 037) {                                     /* LAI/SAI 10x400-437 */
    MA = ((entry - 020) + BR) & VAMASK;                 /* +/- offset */
    if (IR & I_AB) AR = ReadW (MA);                     /* AB = 1 -> LAI */
    else WriteW (MA, AR);                               /* AB = 0 -> SAI */
    return reason;
    }
else if (entry <= 057)                                  /* IR = 10x440-457? */
    return STOP (cpu_ss_unimpl);                        /* not part of IOP */

entry = entry - 060;                                    /* offset 10x460-477 */

if (op_iop [entry] != OP_N) {
    reason = cpu_ops (op_iop [entry], op, intrq);       /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<5:0> */

    case 000:                                           /* CRC 105460 (OP_V) */
        t = ReadW (op[0].word) ^ (AR & 0377);           /* xor prev CRC and char */
        for (i = 0; i < 8; i++) {                       /* apply polynomial */
            t = (t >> 1) | ((t & 1) << 15);             /* rotate right */
            if (t & SIGN) t = t ^ 020001;               /* old t<0>? xor */
            }
        WriteW (op[0].word, t);                         /* rewrite CRC */
        break;

    case 001:                                           /* RESTR 105461 (OP_N) */
        iop_sp = (iop_sp - 1) & VAMASK;                 /* decr stack ptr */
        t = ReadW (iop_sp);                             /* get E and O */
        O = ((t >> 1) ^ 1) & 1;                         /* restore O */
        E = t & 1;                                      /* restore E */
        iop_sp = (iop_sp - 1) & VAMASK;                 /* decr sp */
        BR = ReadW (iop_sp);                            /* restore B */
        iop_sp = (iop_sp - 1) & VAMASK;                 /* decr sp */
        AR = ReadW (iop_sp);                            /* restore A */
        if (UNIT_CPU_MODEL == UNIT_2100)
            mp_fence = iop_sp;                          /* 2100 keeps sp in MP FR */
        break;

    case 002:                                           /* READF 105462 (OP_N) */
        AR = iop_sp;                                    /* copy stk ptr */
        break;

    case 003:                                           /* INS 105463 (OP_N) */
        iop_sp = AR;                                    /* init stk ptr */
        break;

    case 004:                                           /* ENQ 105464 (OP_N) */
        hp = ReadW (AR & VAMASK);                       /* addr of head */
        tp = ReadW ((AR + 1) & VAMASK);                 /* addr of tail */
        WriteW ((BR - 1) & VAMASK, 0);                  /* entry link */
        WriteW ((tp - 1) & VAMASK, BR);                 /* tail link */
        WriteW ((AR + 1) & VAMASK, BR);                 /* queue tail */
        if (hp != 0) PR = (PR + 1) & VAMASK;            /* q not empty? skip */
        break;

    case 005:                                           /* PENQ 105465 (OP_N) */
        hp = ReadW (AR & VAMASK);                       /* addr of head */
        WriteW ((BR - 1) & VAMASK, hp);                 /* becomes entry link */
        WriteW (AR & VAMASK, BR);                       /* queue head */
        if (hp == 0)                                    /* q empty? */
            WriteW ((AR + 1) & VAMASK, BR);             /* queue tail */
        else PR = (PR + 1) & VAMASK;                    /* skip */
        break;

    case 006:                                           /* DEQ 105466 (OP_N) */
        BR = ReadW (AR & VAMASK);                       /* addr of head */
        if (BR) {                                       /* queue not empty? */
            hp = ReadW ((BR - 1) & VAMASK);             /* read hd entry link */
            WriteW (AR & VAMASK, hp);                   /* becomes queue head */
            if (hp == 0)                                /* q now empty? */
                WriteW ((AR + 1) & VAMASK, (AR + 1) & DMASK);
            PR = (PR + 1) & VAMASK;                     /* skip */
            }
        break;

    case 007:                                           /* TRSLT 105467 (OP_V) */
        wc = ReadW (op[0].word);                        /* get count */
        if (wc & SIGN) break;                           /* cnt < 0? */
        while (wc != 0) {                               /* loop */
            MA = (AR + AR + ReadB (BR)) & VAMASK;
            byte = ReadB (MA);                          /* xlate */
            WriteB (BR, byte);                          /* store char */
            BR = (BR + 1) & DMASK;                      /* incr ptr */
            wc = (wc - 1) & DMASK;                      /* decr cnt */
            if (wc && intrq) {                          /* more and intr? */
                WriteW (op[0].word, wc);                /* save count */
                PR = err_PC;                            /* stop for now */
                break;
                }
            }
        break;

    case 010:                                           /* ILIST 105470 (OP_AC) */
        do {                                            /* for count */
            WriteW (op[0].word, AR);                    /* write AR to mem */
            AR = (AR + 1) & DMASK;                      /* incr AR */
            op[0].word = (op[0].word + 1) & VAMASK;     /* incr MA */
            op[1].word = (op[1].word - 1) & DMASK;      /* decr count */
            }
        while (op[1].word != 0);
        break;

    case 011:                                           /* PRFEI 105471 (OP_CVA) */
        WriteW (op[1].word, 1);                         /* set flag */
        reason = cpu_iog (op[0].word, 0);               /* execute I/O instr */
        op[0].word = op[2].word;                        /* set rtn and fall through */

    case 012:                                           /* PRFEX 105472 (OP_A) */
        PCQ_ENTRY;
        PR = ReadW (op[0].word) & VAMASK;               /* jump indirect */
        WriteW (op[0].word, 0);                         /* clear exit */
        break;

    case 013:                                           /* PRFIO 105473 (OP_CV) */
        WriteW (op[1].word, 1);                         /* set flag */
        reason = cpu_iog (op[0].word, 0);               /* execute instr */
        break;

    case 014:                                           /* SAVE 105474 (OP_N) */
        WriteW (iop_sp, AR);                            /* save A */
        iop_sp = (iop_sp + 1) & VAMASK;                 /* incr stack ptr */
        WriteW (iop_sp, BR);                            /* save B */
        iop_sp = (iop_sp + 1) & VAMASK;                 /* incr stack ptr */
        t = (HP_WORD) ((O ^ 1) << 1 | E);               /* merge E and O */
        WriteW (iop_sp, t);                             /* save E and O */
        iop_sp = (iop_sp + 1) & VAMASK;                 /* incr stack ptr */
        if (UNIT_CPU_TYPE == UNIT_TYPE_2100)
            mp_fence = iop_sp;                          /* 2100 keeps sp in MP FR */
        break;

    default:                                            /* instruction unimplemented */
        return STOP (cpu_ss_unimpl);
        }

return reason;
}
