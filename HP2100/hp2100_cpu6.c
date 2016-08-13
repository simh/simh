/* hp2100_cpu6.c: HP 1000 RTE-6/VM OS instructions

   Copyright (c) 2006-2016, J. David Bryan

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

   CPU6         RTE-6/VM OS instructions

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   17-May-16    JDB     Set local variable instead of call parameter for .SIP test
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Use MP abort handler declaration in hp2100_cpu.h
   09-May-12    JDB     Separated assignments from conditional expressions
   29-Oct-10    JDB     DMA channels renamed from 0,1 to 1,2 to match documentation
   18-Sep-08    JDB     Corrected .SIP debug formatting
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   27-Nov-07    JDB     Implemented OS instructions
   26-Sep-06    JDB     Created

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


#include <setjmp.h>
#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"


/* Offsets to data and addresses within RTE. */

static const uint32 xi    = 0001647;                    /* XI address */
static const uint32 intba = 0001654;                    /* INTBA address */
static const uint32 intlg = 0001655;                    /* INTLG address */
static const uint32 eqt1  = 0001660;                    /* EQT1  address */
static const uint32 eqt11 = 0001672;                    /* EQT11 address */
static const uint32 pvcn  = 0001712;                    /* PVCN  address */
static const uint32 xsusp = 0001730;                    /* XSUSP address */
static const uint32 dummy = 0001737;                    /* DUMMY address */
static const uint32 mptfl = 0001770;                    /* MPTFL address */
static const uint32 eqt12 = 0001771;                    /* EQT12 address */
static const uint32 eqt15 = 0001774;                    /* EQT15 address */
static const uint32 vctr  = 0002000;                    /* VCTR address */

static const uint32 CLC_0   = 0004700;                  /* CLC 0 instruction */
static const uint32 STC_0   = 0000700;                  /* STC 0 instruction */
static const uint32 CLF_0   = 0001100;                  /* CLF 0 instruction */
static const uint32 STF_0   = 0000100;                  /* STF 0 instruction */
static const uint32 SFS_0_C = 0003300;                  /* SFS 0,C instruction */

enum vctr_offsets { dms_offset = 0,                     /* DMS status */
                    int_offset,                         /* interrupt system status */
                    sc_offset,                          /* select code */
                    clck_offset,                        /* TBG IRQ handler */
                    cic4_offset,                        /* illegal IRQ handler */
                    cic2_offset,                        /* device IRQ handler */
                    sked_offset,                        /* prog sched IRQ handler */
                    rqst_offset,                        /* EXEC request handler */
                    cic_offset,                         /* IRQ location */
                    perr_offset,                        /* parity error IRQ handler */
                    mper_offset,                        /* memory protect IRQ handler */
                    lxnd_offset };                      /* $LIBR return */


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
     $OTST *  105355   OS firmware self test
     .ENTC    105356   Transfer parameter addresses (priv/reent)
     .DSPI    105357   Set display indicator

   Opcodes 105354-105357 are "dual use" instructions that take different
   actions, depending on whether they are executed from a trap cell during an
   interrupt.  When executed from a trap cell, they have these actions:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     $DCPC *  105354   DCPC channel interrupt processing
     $MPV  *  105355   MP/DMS/PE interrupt processing
     $DEV  *  105356   Standard device interrupt processing
     $TBG  *  105357   TBG interrupt processing

   * These mnemonics are recognized by symbolic examine/deposit but are not
     official HP mnemonics.

   Implementation notes:

    1. The microcode differentiates between interrupt processing and normal
       execution of the "dual use" instructions by testing the CPU flag.
       Interrupt vectoring sets the flag; a normal instruction fetch clears it.
       Under simulation, interrupt vectoring is indicated by the value of the
       "iotrap" parameter (0 = normal instruction, 1 = trap cell instruction).

    2. The operand patterns for .ENTN and .ENTC normally would be coded as
       "OP_A", as each takes a single address as a parameter.  However, because
       they might also be executed from a trap cell, we cannot assume that P+1
       is an address, or we might cause a DM abort when trying to resolve
       indirects.  Therefore, "OP_A" handling is done within each routine, once
       the type of use is determined.

    3. The microcode for .ENTC, .ENTN, .FNW, .LLS, .TICK, and .TNAM explicitly
       checks for interrupts during instruction execution.  In addition, the
       .STIO, .CPM, and .LLS instructions implicitly check for interrupts during
       parameter indirect resolution.  Because the simulator calculates
       interrupt requests only between instructions, this behavior is not
       simulated.

    4. The microcode executes certain I/O instructions (e.g., CLF 0) by building
       the instruction in the IR and executing an IOG micro-order.  We simulate
       this behavior by calling the "iogrp" handler with the appropriate
       instruction, rather than manipulating the I/O system directly, so that we
       will remain unaffected by any future changes to the underlying I/O
       simulation structure.

    5. The $OTST and .DSPI microcode uses features (reading the RPL switches and
       boot loader ROM data, loading the display register) that are not
       simulated.  The remaining functions of the $OTST instruction are
       provided. The .DSPI instruction is a NOP or unimplemented instruction
       stop.

    6. Because of the volume of calls to the OS firmware, debug printouts
       attempt to write only one line per instruction invocation.  This means
       that calling and returned register values are printed separately, with a
       newline added at the end of execution.  However, many instructions can MP
       or DM abort, either intentionally or due to improper use.  That would
       leave debug lines without the required trailing newlines.

       There are two ways to address this: either we could replace the CPU's
       setjmp buffer with one that points to a routine that adds the missing
       newline, or we can add a semaphore that is tested on entry to see if it
       is already set, implying a longjmp occurred, and then add the newline if
       so.  The latter would add the newline when the MP trap cell instruction
       was entered or when the next user-level instruction was executed.
       However, the merged-line problem would still exist if some other module
       generated intervening debug printouts.  So we do the former.  This does
       mean that this routine must be changed if the MP abort mechanism is
       changed.

    7. The $LIBX instruction is executed to complete either a privileged or
       reentrant execution.  In the former case, the privileged nest counter
       ($PVCN) is decremented.  In the latter, $PVCN decrement is attempted but
       the write will trap with an MP violation, as reentrant routines execute
       with the interrupt system on.  RTE will then complete the release of
       memory allocated for the original $LIBR call.

    8. The documentation for the .SIP and .YLD instructions is misleading in
       several places.  Comments in the RTE $SIP source file say that .SIP
       doesn't return if a "known" interrupt is pending.  Actually, .SIP always
       returns, either to P+1 for no pending interrupt, or to P+2 if one is
       pending.  There is no check for "known" interrupt handlers.  The
       microcode source comments say that the interrupting select code is
       returned in the B register.  Actually, the B register is unchanged.  The
       RTE Tech Specs say that .SIP "services any pending system interrupts."
       Actually, .SIP only checks for interrupts; no servicing is performed.

       For .YLD, the microcode comments say that two parameters are passed: the
       new P value, and the interrupting select code.  Actually, only the new P
       value is passed.

       The .SIP and .YLD simulations follow the actual microcode rather than the
       documentation.

   Additional references:
    - RTE-6/VM OS Microcode Source (92084-18831, revision 8).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).
*/


/* Save the CPU registers.

   The CPU registers are saved in the current ID segment in preparation for
   interrupt handling.  Although the RTE base page has separate pointers for the
   P, A, B, and E/O registers, they are always contiguous, and the microcode
   simply increments the P-register pointer (XSUSP) to store the remaining
   values.

   This routine is called from the trap cell interrupt handlers and from the
   $LIBX processor.  In the latter case, the privileged system interrupt
   handling is not required, so it is bypassed.  In either case, the current map
   will be the system map when we are called.
*/

static t_stat cpu_save_regs (uint32 iotrap)
{
uint16 save_area, priv_fence;
t_stat reason = SCPE_OK;

save_area = ReadW (xsusp);                              /* addr of PABEO save area */

WriteW (save_area + 0, PR);                             /* save P */
WriteW (save_area + 1, AR);                             /* save A */
WriteW (save_area + 2, BR);                             /* save B */
WriteW (save_area + 3, (E << 15) & SIGN | O & 1);       /* save E and O */

save_area = ReadW (xi);                                 /* addr of XY save area */
WriteWA (save_area + 0, XR);                            /* save X (in user map) */
WriteWA (save_area + 1, YR);                            /* save Y (in user map) */

if (iotrap) {                                           /* do priv setup only if IRQ */
    priv_fence = ReadW (dummy);                         /* get priv fence select code */

    if (priv_fence) {                                   /* privileged system? */
        reason = iogrp (STC_0 + priv_fence, iotrap);    /* STC SC on priv fence */
        reason = iogrp (CLC_0 + DMA1, iotrap);          /* CLC 6 to inh IRQ on DCPC 1 */
        reason = iogrp (CLC_0 + DMA2, iotrap);          /* CLC 7 to inh IRQ on DCPC 2 */
        reason = iogrp (STF_0, iotrap);                 /* turn interrupt system back on */
        }
    }

return reason;
}


/* Save the machine state at interrupt.

   This routine is called from each of the trap cell instructions.  Its purpose
   is to save the complete state of the machine in preparation for interrupt
   handling.

   For the MP/DMS/PE interrupt, the interrupting device must not be cleared and
   the CPU registers must not be saved until it is established that the
   interrupt is not caused by a parity error.  Parity errors cannot be
   inhibited, so the interrupt may have occurred while in RTE.  Saving the
   registers would overwrite the user's registers that were saved at RTE entry.

   Note that the trap cell instructions are dual-use and invoke this routine
   only when they are executed during interrupts.  Therefore, the current map
   will always be the system map when we are called.
*/

static t_stat cpu_save_state (uint32 iotrap)
{
uint16 vectors;
uint32 saved_PR, int_sys_off;
t_stat reason;

saved_PR = PR;                                          /* save current P register */
reason = iogrp (SFS_0_C, iotrap);                       /* turn interrupt system off */
int_sys_off = (PR == saved_PR);                         /* set flag if already off */
PR = saved_PR;                                          /* restore P in case it bumped */

vectors = ReadW (vctr);                                 /* get address of vectors (in SMAP) */

WriteW (vectors + dms_offset, dms_upd_sr ());           /* save DMS status (SSM) */
WriteW (vectors + int_offset, int_sys_off);             /* save int status */
WriteW (vectors + sc_offset,  intaddr);                 /* save select code */

WriteW (mptfl, 1);                                      /* show MP is off */

if (intaddr != 5) {                                     /* only if not MP interrupt */
    reason = iogrp (CLF_0 + intaddr, iotrap);           /* issue CLF to device */
    cpu_save_regs (iotrap);                             /* save CPU registers */
    }

return reason;
}


/* Get the interrupt table entry corresponding to a select code.

   Return the word in the RTE interrupt table that corresponds to the
   interrupting select code.  Return 0 if the select code is beyond the end of
   the table.
*/

static uint16 cpu_get_intbl (uint32 select_code)
{
uint16 interrupt_table;                                 /* interrupt table (starts with SC 06) */
uint16 table_length;                                    /* length of interrupt table */

interrupt_table = ReadW (intba);                        /* get int table address */
table_length = ReadW (intlg);                           /* get int table length */

if (select_code - 6 > table_length)                     /* SC beyond end of table? */
    return 0;                                           /* return 0 for illegal interrupt */
else
    return ReadW (interrupt_table + select_code - 6);   /* else return table entry */
}


/* RTE-6/VM OS instruction dispatcher.

   Debugging printouts are provided with the OS and OSTBG debug flags.  The OS
   flag enables tracing for all instructions except for the three-instruction
   sequence executed for the time-base generator interrupt ($TBG, .TICK, and
   .IRT).  The OSTBG flag enables tracing for just the TBG sequence.  The flags
   are separate, as the TBG generates 100 interrupts per second.  Use caution
   when specifying the OSTBG flag, as the debug output file will grow rapidly.
   Note that the OS flag enables the .IRT instruction trace for all cases except
   a TBG interrupt.

   The default (user microcode) dispatcher will allow the firmware self-test
   instruction (105355) to execute as NOP.  This is because RTE-6/VM will always
   test for the presence of OS and VMA firmware on E/F-Series machines.  If the
   firmware is not present, then these instructions will return to P+1, and RTE
   will then HLT 21.  This means that RTE-6/VM will not run on an E/F-Series
   machine without the OS and VMA firmware.

   Howwever, RTE allows the firmware instructions to be disabled for debugging
   purposes.  If the firmware is present and returns to P+2 but sets the X
   register to 0, then RTE will use software equivalents.  We enable this
   condition when the OS firmware is enabled (SET CPU VMA), the OS debug flag is
   set (SET CPU DEBUG=OS), but debug output has been disabled (SET CONSOLE
   NODEBUG).  That is:

                 OS     Debug
     Firmware   Debug   Output   Tracing   Self-Test Instruction
     ========   =====   ======   =======   =====================
     disabled     x       x        off     NOP
     enabled    clear     x        off     X = revision code
     enabled     set     off       off     X = 0
     enabled     set     on        on      X = revision code
*/

static const OP_PAT op_os[16] = {
  OP_A,    OP_A,    OP_N,    OP_N,                      /* $LIBR  $LIBX  .TICK  .TNAM  */
  OP_A,    OP_K,    OP_A,    OP_KK,                     /* .STIO  .FNW   .IRT   .LLS   */
  OP_N,    OP_C,    OP_KK,   OP_N,                      /* .SIP   .YLD   .CPM   .ETEQ  */
  OP_N,    OP_N,    OP_N,    OP_N                       /* .ENTN  $OTST  .ENTC  .DSPI  */
  };

t_stat cpu_rte_os (uint32 IR, uint32 intrq, uint32 iotrap)
{
t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
uint32 entry, count, cp, sa, da, i, ma, eqta, irq;
uint16 vectors, save_area, priv_fence, eoreg, eqt, key;
char test[6], target[6];
jmp_buf mp_handler;
int abortval;
t_bool debug_print;
static t_bool tbg_tick = FALSE;                         /* set if processing TBG interrupt */

entry = IR & 017;                                       /* mask to entry point */
pattern = op_os[entry];                                 /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op, intrq);              /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

tbg_tick = tbg_tick || (IR == 0105357) && iotrap;       /* set TBG interrupting flag */

debug_print = (DEBUG_PRI (cpu_dev, DEB_OS) && !tbg_tick) ||
              (DEBUG_PRI (cpu_dev, DEB_OSTBG) && tbg_tick);

if (debug_print) {
    fprintf (sim_deb, ">>CPU OS: IR = %06o (", IR);     /* print preamble and IR */
    fprint_sym (sim_deb, (iotrap ? intaddr : err_PC),   /* print instruction mnemonic */
                (t_value *) &IR, NULL, SWMASK('M'));
    fputc (')', sim_deb);

    fprint_ops (pattern, op);                           /* print operands */

    memcpy (mp_handler, save_env, sizeof (jmp_buf));    /* save original MP handler */
    abortval = setjmp (save_env);                       /* set new MP abort handler */

    if (abortval != 0) {                                /* MP abort? */
        fputs ("...MP abort\n", sim_deb);               /* report it and terminate line */
        memcpy (save_env, mp_handler, sizeof (jmp_buf));    /* restore original MP handler */
        longjmp (save_env, abortval);                       /* transfer to MP handler */
        }
    }

switch (entry) {                                        /* decode IR<3:0> */

    case 000:                                           /* $LIBR 105340 (OP_A) */
        if ((op[0].word != 0) ||                        /* reentrant call? */
            (mp_control && (ReadW (dummy) != 0))) {     /* or priv call + MP on + priv sys? */
            if (dms_ump) {                              /* called from user map? */
                dms_viol (err_PC, MVI_PRV);             /* privilege violation */
                }
            dms_ump = SMAP;                             /* set system map */

            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */
            PR = ReadW (vectors + mper_offset);         /* vector to $MPER for processing */
            }

        else {                                          /* privileged call */
            if (mp_control) {                           /* memory protect on? */
                mp_control = CLEAR;                     /* turn it off */
                reason = iogrp (CLF_0, iotrap);         /* turn interrupt system off */
                WriteW (mptfl, 1);                      /* show MP is off */
                save_area = ReadW (xsusp);              /* get addr of P save area */

                if (dms_ump)                                /* user map current? */
                    WriteWA (save_area, (PR - 2) & VAMASK); /* set point of suspension */
                else                                        /* system map current */
                    WriteW (save_area, (PR - 2) & VAMASK);  /* set point of suspension */
                }

            WriteW (pvcn, (ReadW (pvcn) + 1) & DMASK);  /* increment priv nest counter */
            }
        break;

    case 001:                                           /* $LIBX 105341 (OP_A) */
        PR = ReadW (op[0].word);                        /* set P to return point */
        count = (ReadW (pvcn) - 1) & DMASK;             /* decrement priv nest counter */
        WriteW (pvcn, count);                           /* write it back */

        if (count == 0) {                               /* end of priv mode? */
            dms_ump = SMAP;                             /* set system map */
            reason = cpu_save_regs (iotrap);            /* save registers */
            vectors = ReadW (vctr);                     /* get address of vectors */
            PR = ReadW (vectors + lxnd_offset);         /* vector to $LXND for processing */
            }
        break;

    case 002:                                           /* .TICK 105342 (OP_N) */
        if (debug_print)                                /* debugging? */
            fprint_regs (",", REG_A | REG_B, 0);        /* print entry registers */

        do {
            eqt = (ReadW (AR) + 1) & DMASK;             /* bump timeout from EQT15 */

            if (eqt != 1) {                             /* was timeout active? */
                WriteW (AR, eqt);                       /* yes, write it back */

                if (eqt == 0)                           /* did timeout expire? */
                    break;                              /* P+0 return for timeout */
                }

            AR = (AR + 15) & DMASK;                     /* point at next EQT15 */
            BR = (BR - 1) & DMASK;                      /* decrement count of EQTs */
        } while ((BR > 0) && (eqt != 0));               /* loop until timeout or done */

        if (BR == 0)                                    /* which termination condition? */
            PR = (PR + 1) & VAMASK;                     /* P+1 return for no timeout */

        if (debug_print)                                /* debugging? */
            fprint_regs ("; result:",                   /* print return registers */
                         REG_A | REG_B | REG_P_REL,
                         err_PC + 1);
        break;

    case 003:                                           /* .TNAM 105343 (OP_N) */
        if (debug_print)                                /* debugging? */
            fprint_regs (",", REG_A | REG_B, 0);        /* print entry registers */

        E = 1;                                          /* preset flag for not found */
        cp = (BR << 1) & DMASK;                         /* form char addr (B is direct) */

        for (i = 0; i < 5; i++) {                       /* copy target name */
            target[i] = (char) ReadB (cp);              /* name is only 5 chars */
            cp = (cp + 1) & DMASK;
            }

        if ((target[0] == '\0') && (target[1] == '\0')) /* if name is null, */
            break;                                      /* return immed to P+0 */

        key = ReadW (AR);                               /* get first keyword addr */

        while (key != 0) {                              /* end of keywords? */
            cp = ((key + 12) << 1) & DMASK;             /* form char addr of name */

            for (i = 0; i < 6; i++) {                   /* copy test name */
                test[i] = (char) ReadB (cp);            /* name is only 5 chars */
                cp = (cp + 1) & DMASK;                  /* but copy 6 to get flags */
                }

            if (strncmp (target, test, 5) == 0) {       /* names match? */
                AR = (key + 15) & DMASK;                /* A = addr of IDSEG [15] */
                BR = key;                               /* B = addr of IDSEG [0] */
                E = (uint32) ((test[5] >> 4) & 1);      /* E = short ID segment bit */
                PR = (PR + 1) & VAMASK;                 /* P+1 for found return */
                break;
                }

            AR = (AR + 1) & DMASK;                      /* bump to next keyword */
            key = ReadW (AR);                           /* get next keyword */
            };

        if (debug_print)                                /* debugging? */
            fprint_regs ("; result:",                   /* print return registers */
                         REG_A | REG_B | REG_E | REG_P_REL,
                         err_PC + 1);
        break;

    case 004:                                           /* .STIO 105344 (OP_A) */
        count = op[0].word - PR;                        /* get count of operands */

        if (debug_print)                                /* debugging? */
            fprintf (sim_deb,                           /* print registers on entry */
                     ", A = %06o, count = %d", AR, count);

        for (i = 0; i < count; i++) {
            ma = ReadW (PR);                            /* get operand address */

            reason = resolve (ma, &ma, intrq);          /* resolve indirect */

            if (reason != SCPE_OK) {                    /* resolution failed? */
                PR = err_PC;                            /* IRQ restarts instruction */
                break;
                }

            WriteW (ma, ReadW (ma) & ~I_DEVMASK | AR);  /* set SC into instruction */
            PR = (PR + 1) & VAMASK;                     /* bump to next */
            }
        break;

    case 005:                                           /* .FNW  105345 (OP_K) */
        if (debug_print)                                    /* debugging? */
            fprint_regs (",", REG_A | REG_B | REG_X, 0);    /* print entry registers */

        while (XR != 0) {                               /* all comparisons done? */
            key = ReadW (BR);                           /* read a buffer word */

            if (key == AR) {                            /* does it match? */
                PR = (PR + 1) & VAMASK;                 /* P+1 found return */
                break;
                }

            BR = (BR + op[0].word) & DMASK;             /* increment buffer ptr */
            XR = (XR - 1) & DMASK;                      /* decrement remaining count */
            }
                                                        /* P+0 not found return */
        if (debug_print)                                /* debugging? */
            fprint_regs ("; result:",                   /* print return registers */
                         REG_A | REG_B | REG_X | REG_P_REL,
                         err_PC + 2);
        break;

    case 006:                                           /* .IRT  105346 (OP_A) */
        save_area = ReadW (xsusp);                      /* addr of PABEO save area */

        WriteW (op[0].word, ReadW (save_area + 0));     /* restore P to DEF RTN */

        AR = ReadW (save_area + 1);                     /* restore A */
        BR = ReadW (save_area + 2);                     /* restore B */

        eoreg = ReadW (save_area + 3);                  /* get combined E and O */
        E = (eoreg >> 15) & 1;                          /* restore E */
        O = eoreg & 1;                                  /* restore O */

        save_area = ReadW (xi);                         /* addr of XY save area */
        XR = ReadWA (save_area + 0);                    /* restore X (from user map) */
        YR = ReadWA (save_area + 1);                    /* restore Y (from user map) */

        reason = iogrp (CLF_0, iotrap);                 /* turn interrupt system off */
        WriteW (mptfl, 0);                              /* show MP is on */

        priv_fence = ReadW (dummy);                     /* get priv fence select code */

        if (priv_fence) {                               /* privileged system? */
            reason = iogrp (CLC_0 + priv_fence, iotrap);    /* CLC SC on priv fence */
            reason = iogrp (STF_0 + priv_fence, iotrap);    /* STF SC on priv fence */

            if (cpu_get_intbl (DMA1) & SIGN)            /* DCPC 1 active? */
                reason = iogrp (STC_0 + DMA1, iotrap);  /* STC 6 to enable IRQ on DCPC 1 */

            if (cpu_get_intbl (DMA2) & SIGN)            /* DCPC 2 active? */
                reason = iogrp (STC_0 + DMA2, iotrap);  /* STC 7 to enable IRQ on DCPC 2 */
            }

        tbg_tick = 0;                                   /* .IRT terminates TBG servicing */
        break;

    case 007:                                           /* .LLS  105347 (OP_KK) */
        if (debug_print)                                    /* debugging? */
            fprint_regs (",", REG_A | REG_B | REG_E, 0);    /* print entry registers */

        AR = AR & ~SIGN;                                /* clear sign bit of A */

        while ((AR != 0) && ((AR & SIGN) == 0)) {       /* end of list or bad list? */
            key = ReadW ((AR + op[1].word) & VAMASK);   /* get key value */

            if ((E == 0) && (key == op[0].word) ||      /* for E = 0, key = arg? */
                (E != 0) && (key >  op[0].word))        /* for E = 1, key > arg? */
                break;                                  /* search is done */

            BR = AR;                                    /* B = last link */
            AR = ReadW (AR);                            /* A = next link */
            }

        if (AR == 0)                                    /* exhausted list? */
            PR = (PR + 1) & VAMASK;                     /* P+1 arg not found */
        else if ((AR & SIGN) == 0)                      /* good link? */
            PR = (PR + 2) & VAMASK;                     /* P+2 arg found */
                                                        /* P+0 bad link */
        if (debug_print)                                /* debugging? */
            fprint_regs ("; result:",                   /* print return registers */
                         REG_A | REG_B | REG_P_REL,
                         err_PC + 3);
        break;

    case 010:                                           /* .SIP  105350 (OP_N) */
        reason = iogrp (STF_0, iotrap);                 /* turn interrupt system on */
        irq = calc_int ();                              /* check for interrupt requests */
        reason = iogrp (CLF_0, iotrap);                 /* turn interrupt system off */

        if (irq)                                        /* was interrupt pending? */
            PR = (PR + 1) & VAMASK;                     /* P+1 return for pending IRQ */
                                                        /* P+0 return for no pending IRQ */
        if (debug_print)                                /* debugging? */
            fprintf (sim_deb,                           /* print return registers */
                     ", CIR = %02o, return = P+%d",
                     irq, PR - (err_PC + 1));
        break;

    case 011:                                           /* .YLD  105351 (OP_C) */
        PR = op[0].word;                                /* pick up point of resumption */
        reason = iogrp (STF_0, iotrap);                 /* turn interrupt system on */
        ion_defer = 0;                                  /* kill defer so irq occurs immed */
        break;

    case 012:                                           /* .CPM  105352 (OP_KK) */
        if (INT16 (op[0].word) > INT16 (op[1].word))
            PR = (PR + 2) & VAMASK;                     /* P+2 arg1 > arg2 */
        else if (INT16 (op[0].word) < INT16 (op[1].word))
            PR = (PR + 1) & VAMASK;                     /* P+1 arg1 < arg2 */
                                                        /* P+0 arg1 = arg2 */
        if (debug_print)                                /* debugging? */
            fprint_regs (",", REG_P_REL, err_PC + 3);   /* print return registers */
        break;

    case 013:                                           /* .ETEQ 105353 (OP_N) */
        eqt = ReadW (eqt1);                             /* get addr of EQT1 */

        if (AR != eqt) {                                /* already set up? */
            for (eqta = eqt1; eqta <= eqt11; eqta++)    /* init EQT1-EQT11 */
                WriteW (eqta, AR++ & DMASK);
            for (eqta = eqt12; eqta <= eqt15; eqta++)   /* init EQT12-EQT15 */
                WriteW (eqta, AR++ & DMASK);            /* (not contig with EQT1-11) */
            }

        AR = AR & DMASK;                                /* ensure wraparound */

        if (debug_print)                                /* debugging? */
            fprintf (sim_deb,                           /* print return registers */
                     ", A = %06o, EQT1 = %06o", AR, eqt);
        break;

    case 014:                                           /* .ENTN/$DCPC 105354 (OP_N) */
        if (iotrap) {                                   /* in trap cell? */
            reason = cpu_save_state (iotrap);           /* DMA interrupt */
            AR = cpu_get_intbl (intaddr) & ~SIGN;       /* get intbl value and strip sign */
            goto DEVINT;                                /* vector by intbl value */
            }

        else {                                          /* .ENTN instruction */
            ma = (PR - 2) & VAMASK;                     /* get addr of entry point */

        ENTX:                                           /* enter here from .ENTC */
            reason = cpu_ops (OP_A, op, intrq);         /* get instruction operand */
            da = op[0].word;                            /* get addr of 1st formal */
            count = ma - da;                            /* get count of formals */
            sa = ReadW (ma);                            /* get addr of 1st actual */
            WriteW (ma, (sa + count) & VAMASK);         /* adjust return point to skip actuals */

            if (debug_print)                            /* debugging? */
                fprintf (sim_deb,                       /* print entry registers */
                         ", op [0] = %06o, pcount = %d",
                         da, count);

            for (i = 0; i < count; i++) {               /* parameter loop */
                ma = ReadW (sa);                        /* get addr of actual */
                sa = (sa + 1) & VAMASK;                 /* increment address */

                reason = resolve (ma, &ma, intrq);      /* resolve indirect */

                if (reason != SCPE_OK) {                /* resolution failed? */
                    PR = err_PC;                        /* irq restarts instruction */
                    break;
                    }

                WriteW (da, ma);                        /* put addr into formal */
                da = (da + 1) & VAMASK;                 /* increment address */
                }

            if (entry == 016)                           /* call was .ENTC? */
                AR = (uint16) sa;                       /* set A to return address */
            }
        break;

    case 015:                                           /* $OTST/$MPV 105355 (OP_N) */
        if (iotrap) {                                   /* in trap cell? */
            reason = cpu_save_state (iotrap);           /* MP/DMS/PE interrupt */
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */

            if (mp_viol & SIGN) {                       /* parity error? */
                WriteW (vectors + cic_offset, PR);      /* save point of suspension in $CIC */
                PR = ReadW (vectors + perr_offset);     /* vector to $PERR for processing */
                }

            else {                                      /* MP/DMS violation */
                cpu_save_regs (iotrap);                 /* save CPU registers */
                PR = ReadW (vectors + rqst_offset);     /* vector to $RQST for processing */
                }

            if (debug_print) {                          /* debugging? */
                fprint_regs (",", REG_CIR, 0);          /* print interrupt source */
                                                        /* and cause */
                if (mp_viol & SIGN)
                    fputs (", parity error", sim_deb);
                else if (mp_mevff)
                    fputs (", DM violation", sim_deb);
                else
                    fputs (", MP violation", sim_deb);
                }
            }

        else {                                          /* self-test instruction */
            YR = 0000000;                               /* RPL switch (not implemented) */
            AR = 0000000;                               /* LDR [B] (not implemented) */
            SR = 0102077;                               /* test passed code */
            PR = (PR + 1) & VAMASK;                     /* P+1 return for firmware OK */

            if ((cpu_dev.dctrl & DEB_OS) &&             /* OS debug flag set, */
                (sim_deb == NULL))                      /* but debugging disabled? */
                XR = 0;                                 /* rev = 0 means RTE won't use ucode */
            else
                XR = 010;                               /* firmware revision 10B = 8 */

            if (debug_print)                            /* debugging? */
                fprint_regs (",", REG_X | REG_P_REL,    /* print return registers */
                             err_PC + 1);
            }
        break;

    case 016:                                           /* .ENTC/$DEV 105356 (OP_N) */
        if (iotrap) {                                   /* in trap cell? */
            reason = cpu_save_state (iotrap);           /* device interrupt */
            AR = cpu_get_intbl (intaddr);               /* get interrupt table value */

        DEVINT:
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */

            if (INT16 (AR) < 0)                         /* negative (program ID)? */
                PR = ReadW (vectors + sked_offset);     /* vector to $SKED for processing */
            else if (AR > 0)                            /* positive (EQT address)? */
                PR = ReadW (vectors + cic2_offset);     /* vector to $CIC2 for processing */
            else                                        /* zero (illegal interrupt) */
                PR = ReadW (vectors + cic4_offset);     /* vector to $CIC4 for processing */

            if (debug_print)                            /* debugging? */
                fprintf (sim_deb,                       /* print return registers */
                         ", CIR = %02o, INTBL = %06o",
                         intaddr, AR);
            }

        else {                                          /* .ENTC instruction */
            ma = (PR - 4) & VAMASK;                     /* get addr of entry point */
            goto ENTX;                                  /* continue with common processing */
            }
        break;

    case 017:                                           /* .DSPI/$TBG 105357 (OP_N) */
        if (iotrap) {                                   /* in trap cell? */
            reason = cpu_save_state (iotrap);           /* TBG interrupt */
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */
            PR = ReadW (vectors + clck_offset);         /* vector to $CLCK for processing */

            if (debug_print)                            /* debugging? */
                fprint_regs (",", REG_CIR, 0);          /* print interrupt source */
            }

        else                                            /* .DSPI instruction */
            reason = stop_inst;                         /* not implemented yet */

        break;
    }

if (debug_print) {                                      /* debugging? */
    fputc ('\n', sim_deb);                              /* terminate line */
    memcpy (save_env, mp_handler, sizeof (jmp_buf));    /* restore original MP handler */
    }

return reason;
}
