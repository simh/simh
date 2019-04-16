/* hp3000_cpu_base.c: HP 3000 CPU base set instruction simulator

   Copyright (c) 2016-2018, J. David Bryan

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

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   27-Dec-18    JDB     Revised fall through comments to comply with gcc 7
   08-Jan-17    JDB     Fixed bug in SCAL 0/PCAL 0 if a stack overflow occurs
   07-Nov-16    JDB     SETR doesn't set cpu_base_changed if no register change;
                        renamed cpu_byte_to_word_ea to cpu_byte_ea
   03-Nov-16    JDB     Added zero offsets to the cpu_call_procedure calls
   24-Oct-16    JDB     Renamed SEXT macro to SEXT16
   22-Oct-16    JDB     Changed "interrupt_pending" to global for use by CIS
   07-Oct-16    JDB     Moved "extern cpu_dev" to hp3000_cpu.h where it belongs
   22-Sep-16    JDB     Moved byte_to_word_address to hp3000_cpu.c
   21-Sep-16    JDB     Added the COBOL II Extended Instruction Set dispatcher
   12-Sep-16    JDB     Use the PCN_SERIES_II and PCN_SERIES_III constants
   23-Aug-16    JDB     Implement the CMD instruction and module interrupts
   11-Jun-16    JDB     Bit mask constants are now unsigned
   13-Jan-16    JDB     First release version
   11-Dec-12    JDB     Created

   References:
     - HP 3000 Series II System Microprogram Listing
         (30000-90023, August 1976)
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - Machine Instruction Set Reference Manual
         (30000-90022, June 1984)


   This module implements all of the HP 3000 Series II/III base set
   instructions, except for the memory address instructions, which are
   implemented in the main CPU module.


   Implementation notes:

    1. Each instruction executor begins with a comment listing the instruction
       mnemonic and, following in parentheses, the condition code setting, or
       "none" if the condition code is not altered, and a list of any traps that
       might be generated.  The condition code and trap mnemonics are those used
       in the Machine Instruction Set manual.

    2. In the instruction executors, "TOS" refers to the top-of-the-stack value,
       and "NOS" refers to the next-to-the-top-of-the-stack value.

    3. The order of operations in the executors follows the microcode so that
       the registers, condition code, etc. have the expected values if stack
       overflow or underflow traps occur.

    4. There is no common "cpu_div_16" routine, as each of the five base-set
       division instructions (DIVI, DIV, LDIV, DIVL, and DDIV) has a different
       overflow condition.  Therefore, they are all implemented inline.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_fp.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_mem.h"



/* Program constants */

#define SIO_OK              0100000u            /* TIO bit 0 = SIO OK */
#define DIO_OK              0040000u            /* TIO bit 1 = DIO OK */

#define NORM_BIT            (D48_SIGN >> 6)     /* triple normalizing examines bit 6 */
#define NORM_MASK           (D48_MASK >> 6)     /* triple normalizing masks off bits 0-5 */

#define TO_UPPERCASE(b)     ((b) & ~040u)       /* alphabetic byte upshift */


/* CPU base set global data structures */

typedef enum {                                  /* types of shifts */
    arithmetic,                                 /*   arithmetic shift */
    logical,                                    /*   logical shift */
    circular,                                   /*   circular shift (rotate) */
    normalizing                                 /*   normalizing shift */
    } SHIFT_TYPE;

typedef enum {                                  /* shift operand sizes */
    size_16,                                    /*   16-bit single word */
    size_32,                                    /*   32-bit double word */
    size_48,                                    /*   48-bit triple word */
    size_64                                     /*   64-bit quad word */
    } OPERAND_SIZE;


/* CPU base set local utility routines */

static uint32 add_32               (uint32 augend,  uint32 addend);
static uint32 sub_32               (uint32 minuend, uint32 subtrahend);
static void   shift_16_32          (HP_WORD opcode, SHIFT_TYPE shift, OPERAND_SIZE op_size);
static void   shift_48_64          (HP_WORD opcode, SHIFT_TYPE shift, OPERAND_SIZE op_size);
static void   check_stack_bounds   (HP_WORD new_value);
static uint32 tcs_io               (IO_COMMAND command);
static uint32 srw_io               (IO_COMMAND command, HP_WORD ready_flag);
static void   decrement_stack      (uint32 decrement);

static t_stat move_words           (ACCESS_CLASS source_class, uint32 source_base,
                                    ACCESS_CLASS dest_class,   uint32 dest_base,
                                    uint32 decrement);

/* CPU base set local instruction execution routines */

static t_stat move_spec          (void);
static t_stat firmware_extension (void);
static t_stat io_control         (void);



/* CPU base set global utility routines */


/* Test for a pending interrupt.

   This routine is called from within an executor for an interruptible
   instruction to test for a pending interrupt.  It counts an event tick and
   returns TRUE if the instruction should yield, either for an interrupt or for
   an event error, or FALSE if the instruction should continue.

   Instructions that potentially take a long time (e.g., MOVE, SCU, LLSH) test
   for pending interrupts after each word or byte moved or scanned.  The design
   of these instructions is such that an interrupt may be serviced and the
   instruction resumed without disruption.  For example, the MOVE instruction
   updates the source and target addresses and word count on the stack after
   each word moved.  If the instruction is interrupted, the values on the stack
   indicate where to resume after the interrupt handler completes.


   Implementation notes:

    1. The routine is essentially the same sequence as is performed at the top
       of the instruction execution loop in the "sim_instr" routine.  The
       differences are that this routine backs up P to rerun the instruction
       after the interrupt is serviced, and the interrupt holdoff test necessary
       for the SED instruction isn't done here, as this routine is not called by
       the SED executor.

    2. The event interval decrement that occurs in the main instruction loop
       after each instruction execution is cancelled here if "sim_process_event"
       returns an error code.  This is done so that a STEP command does not
       decrement sim_interval twice.  Note that skipping the initial decrement
       here does not help, as it's the sim_interval value AFTER the call to
       sim_process_event that must be preserved.
*/

t_bool cpu_interrupt_pending (t_stat *status)
{
uint32 device_number = 0;

sim_interval = sim_interval - 1;                        /* count the cycle */

if (sim_interval <= 0) {                                /* if an event timeout expired */
    *status = sim_process_event ();                     /*   then process the event service */

    if (*status != SCPE_OK) {                           /* if the service failed */
        P = P - 1 & R_MASK;                             /*   then back up to reenter the instruction */
        sim_interval = sim_interval + 1;                /*     and cancel the instruction loop increment */

        return TRUE;                                    /* abort the instruction and stop the simulator */
        }
    }

else                                                    /* otherwise */
    *status = SCPE_OK;                                  /*   indicate good status from the service */

if (sel_request)                                        /* if a selector channel request is pending */
    sel_service (1);                                    /*   then service it */

if (mpx_request_set)                                    /* if a multiplexer channel request is pending */
    mpx_service (1);                                    /*   then service it */

if (iop_interrupt_request_set && STA & STATUS_I)        /* if a hardware interrupt request is pending and enabled */
    device_number = iop_poll ();                        /*   then poll to acknowledge the request */

if (CPX1 & CPX1_IRQ_SET) {                              /* if an interrupt is pending */
    P = P - 1 & R_MASK;                                 /*   then back up to reenter the instruction */
    cpu_run_mode_interrupt (device_number);             /*     and set up the service routine */

    return TRUE;                                        /* abort the instruction */
    }

else                                                    /* otherwise */
    return FALSE;                                       /*   continue with the current instruction */
}


/* Execute a short branch.

   The program counter is adjusted by the displacement specified in the CIR, and
   the NIR is loaded with the target instruction.  If the "check_loop" parameter
   is TRUE, an infinite loop check is made if the corresponding simulator stop
   is enabled.  Branch instructions that cannot cause an infinite loop because
   they modify the CPU state during execution will specify the parameter as
   FALSE.

   On entry, the CIR must be loaded with a branch instruction having a short
   (5-bit plus sign bit) displacement.  The instruction format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | I |   branch opcode   |+/-|  P displacement   |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   On exit, the NIR and P registers are updated, and STOP_INFLOOP is returned if
   an infinite loop is enabled and was detected, or SCPE_OK is returned if
   simulation may continue.
*/

t_stat cpu_branch_short (t_bool check_loop)
{
HP_WORD displacement, address;
t_stat  status;

displacement = CIR & DISPL_31_MASK;                     /* get the displacement */

if (CIR & DISPL_31_SIGN)                                /* if the displacement is negative */
    address = P - 2 - displacement & LA_MASK;           /*   then subtract the displacement from the base */
else                                                    /* otherwise */
    address = P - 2 + displacement & LA_MASK;           /*   add the displacement to the base */

if ((CIR & I_FLAG_BIT_4) != 0) {                                /* if the mode is indirect */
    cpu_read_memory (program_checked, address, &displacement);  /*   then get the displacement value */

    address = address + displacement & LA_MASK;         /* add the displacement to the base */
    }

if (cpu_stop_flags & SS_LOOP                            /* if the infinite loop stop is active */
  && check_loop                                         /*   and an infinite loop is possible */
  && address == (P - 2 & LA_MASK))                      /*   and the target is the current instruction */
    status = STOP_INFLOOP;                              /*     then stop the simulator */
else                                                    /* otherwise */
    status = SCPE_OK;                                   /*   continue */

cpu_read_memory (fetch_checked, address, &NIR);         /* load the next instruction register */
P = address + 1 & R_MASK;                               /*   and increment the program counter */

return status;                                          /* return the execution status */
}


/* Add two 16-bit numbers.

   Two 16-bit values are added, and the 16-bit sum is returned.  The C (carry)
   bit in the status register is set if the result is truncated and cleared
   otherwise.  The O (overflow) bit is set if the result exceeds the maximum
   positive or negative range, i.e., the result overflows into the sign bit.  In
   addition, an integer overflow interrupt (ARITH trap) occurs if the user trap
   bit is set.
*/

HP_WORD cpu_add_16 (HP_WORD augend, HP_WORD addend)
{
uint32 sum;

sum = augend + addend;                                  /* sum the values */

SET_CARRY (sum > D16_UMAX);                             /* set C if there's a carry out of the MSB */

SET_OVERFLOW (D16_SIGN                                  /* set O if the signs */
                & (~augend ^ addend)                    /*   of the operands are the same */
                & (augend ^ sum));                      /*     but the sign of the result differs */

return (HP_WORD) LOWER_WORD (sum);                      /* return the lower 16 bits of the sum */
}


/* Subtract two 16-bit numbers.

   Two 16-bit values are subtracted, and the 16-bit difference is returned.  The
   C (carry) bit in the status register is set if the subtraction did not
   require a borrow for the most-significant bit.  The O (overflow) bit is set
   if the result exceeds the maximum positive or negative range, i.e., the
   result borrows from the sign bit.  In addition, an integer overflow interrupt
   (ARITH trap) occurs if the user trap bit is set.


   Implementation notes:

    1. The carry bit is set to the complement of the borrow, i.e., carry = 0 if
       there is a borrow and 1 is there is not.
*/

HP_WORD cpu_sub_16 (HP_WORD minuend, HP_WORD subtrahend)
{
uint32 difference;

difference = minuend - subtrahend;                      /* subtract the values */

SET_CARRY (subtrahend <= minuend);                      /* set C if no borrow from the MSB was done */

SET_OVERFLOW (D16_SIGN                                  /* set O if the signs */
               & (minuend ^ subtrahend)                 /*   of the operands differ */
               & (minuend ^ difference));               /*     as do the signs of the minuend and result */

return (HP_WORD) LOWER_WORD (difference);               /* return the lower 16 bits of the difference */
}


/* Multiply two 16-bit numbers.

   Two 16-bit values are multiplied, and the 16-bit product is returned.  The O
   (overflow) bit in the status register is set if the result exceeds the
   maximum positive or negative range, i.e., if the top 17 bits of the 32-bit
   result are not all zeros or ones.  In addition, an integer overflow interrupt
   (ARITH trap) occurs if the user trap bit is set.
*/

HP_WORD cpu_mpy_16 (HP_WORD multiplicand, HP_WORD multiplier)
{
int32  product;
uint32 check;

product = SEXT16 (multiplicand) * SEXT16 (multiplier);  /* sign-extend the operands and multiply */

check = (uint32) product & S16_OVFL_MASK;               /* check the top 17 bits and set overflow */
SET_OVERFLOW (check != 0 && check != S16_OVFL_MASK);    /*   if they are not all zeros or all ones */

return (HP_WORD) LOWER_WORD (product);                  /* return the lower 16 bits of the product */
}



/* CPU base set global instruction execution routines */


/* Execute a stack instruction (subopcode 00).

   This routine is called to execute a single stack instruction held in the CIR.
   The instruction format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0 |   1st stack opcode    |   2nd stack opcode    |  Stack
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   As a single program word holds two stack opcodes, this routine is generally
   called twice.  If the R (right-hand) bit in the status register is set, the
   opcode in the lower six bits of the CIR is executed; otherwise, the opcode in
   the upper six bits is executed.  The R bit is set when the left-hand opcode
   is executing if the right-hand opcode is not a NOP.  This is an optimization
   that causes the instruction loop to fetch the next instruction in lieu of
   calling this routine again to execute the right-hand NOP.  The R bit also
   marks a pending right-hand stack opcode execution when an interrupt is
   detected after the left-hand stack opcode completes.


   Implementation notes:

    1. The entry status must be saved so that it may be restored if the
       unimplemented opcode 072 is executed with the SS_UNIMPL simulator stop
       flag set.  This allows the instruction to be reexecuted and the
       Unimplemented Instruction trap taken if the stop is subsequently
       bypassed.

    2. In hardware, the NEXT microcode order present at the end of each
       instruction transfers the NIR content to the CIR, reads the memory word
       at P into the NIR, and increments P.  However, if an interrupt is
       present, then this action is omitted, and a microcode jump is performed
       to control store location 3, which then jumps to the microcoded interrupt
       handler.  In simulation, the CIR/NIR/P update is performed before the
       next instruction is executed, rather than after the last instruction
       completes, so that interrupts are handled before updating.

       In addition, the NEXT action is modified in hardware if the NIR contains
       a stack instruction with a non-NOP B (right-hand) stack opcode.  In this
       case, NEXT transfers the NIR content to the CIR, reads the memory word at
       P into the NIR, but does not increment P.  Instead, the R bit of the
       status register is set to indicate that a B stackop is pending.  When the
       NEXT at the completion of the A (left-hand) stackop is executed, the NIR
       and CIR are untouched, but P is incremented, and the R bit is cleared.
       This ensures that if an interrupt or trap occurs between the stackops, P
       will point correctly at the next instruction to be executed.

       In simulation, following the hardware would require testing the NIR for a
       non-NOP B stackop at every pass through the instruction execution loop.
       To avoid this, the NEXT simulation unilaterally increments P, rather than
       only when a B stackop is not present, and the stack instruction executor
       tests for the B stackop and sets the R bit there.  However, by that time,
       P has already been incremented, so we decrement it here to return it to
       the correct value.

    3. Increments, decrements, and negates use the "cpu_add_16" and "cpu_sub_16"
       instead of inline adds and subtracts in order to set the carry and
       overflow status bits properly.

    4. On division by zero, the FDIV microcode sets condition code CCA before
       trapping.  All other floating-point arithmetic traps are taken before
       setting the condition code.
*/

t_stat cpu_stack_op (void)
{
static const uint8 preadjustment [64] = {       /* stack preadjustment, indexed by operation */
    0, 2, 2, 0, 0, 0, 0, 0,                     /*   NOP  DELB DDEL ZROX INCX DECX ZERO DZRO */
    4, 4, 4, 2, 3, 2, 4, 2,                     /*   DCMP DADD DSUB MPYL DIVL DNEG DXCH CMP  */
    2, 2, 2, 2, 1, 1, 2, 2,                     /*   ADD  SUB  MPY  DIV  NEG  TEST STBX DTST */
    2, 1, 2, 1, 1, 1, 1, 1,                     /*   DFLT BTST XCH  INCA DECA XAX  ADAX ADXA */
    1, 2, 2, 1, 0, 1, 2, 1,                     /*   DEL  ZROB LDXB STAX LDXA DUP  DDUP FLT  */
    4, 4, 4, 4, 4, 2, 3, 2,                     /*   FCMP FADD FSUB FMPY FDIV FNEG CAB  LCMP */
    2, 2, 2, 3, 1, 2, 2, 2,                     /*   LADD LSUB LMPY LDIV NOT  OR   XOR  AND  */
    2, 2, 0, 2, 2, 2, 2, 2                      /*   FIXR FIXT  --  INCB DECB XBX  ADBX ADXB */
    };

HP_WORD entry_status, exchanger;
uint32  operation, sum, difference, uproduct, udividend, uquotient, uremainder, check;
int32   product, dividend, divisor, quotient, remainder;
FP_OPND operand_u, operand_v, operand_w;
t_stat  status = SCPE_OK;

entry_status = STA;                                     /* save the entry status for a potential rollback */

if (STA & STATUS_R) {                                   /* if right-hand stackop is pending */
    operation = STACKOP_B (CIR);                        /*   then get the right-hand opcode */
    STA &= ~STATUS_R;                                   /*     and flip the flag off */
    }

else {                                                  /* otherwise */
    operation = STACKOP_A (CIR);                        /*   get the left-hand opcode */

    if (STACKOP_B (CIR) != NOP) {                       /* if the right-hand opcode is a not a NOP */
        STA |= STATUS_R;                                /*   then set the right-hand stackop pending flag */
        P = P - 1 & R_MASK;                             /*     and decrement P to cancel the later increment */
        }
    }

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the stack operation */

    case 000:                                           /* NOP (none; none)*/
        break;                                          /* there is nothing to do for a no-operation */


    case 001:                                           /* DELB (none; STUN) */
        RB = RA;                                        /* copy the TOS into the NOS */
        cpu_pop ();                                     /*   and pop the TOS, effectively deleting the NOS */
        break;


    case 002:                                           /* DDEL (none; STUN) */
        cpu_pop ();                                     /* pop the TOS */
        cpu_pop ();                                     /*   and the NOS */
        break;


    case 003:                                           /* ZROX (none; none) */
        X = 0;                                          /* set X to zero */
        break;


    case 004:                                           /* INCX (CCA, C, O; ARITH) */
        X = cpu_add_16 (X, 1);                          /* increment X */
        SET_CCA (X, 0);                                 /*   and set the condition code */
        break;


    case 005:                                           /* DECX (CCA, C, O; ARITH) */
        X = cpu_sub_16 (X, 1);                          /* decrement X */
        SET_CCA (X, 0);                                 /*   and set the condition code */
        break;


    case 006:                                           /* ZERO (none; STOV) */
        cpu_push ();                                    /* push the stack down */
        RA = 0;                                         /*   and set the TOS to zero */
        break;


    case 007:                                           /* DZRO (none; STOV) */
        cpu_push ();                                    /* push the stack */
        cpu_push ();                                    /*   down twice */
        RA = 0;                                         /* set the TOS */
        RB = 0;                                         /*   and NOS to zero */
        break;


    case 010:                                           /* DCMP (CCC; STUN) */
        SR = 0;                                         /* pop all four values from the stack */
        SET_CCC (RD, RC, RB, RA);                       /*   and set the (integer) condition code */
        break;


    case 011:                                           /* DADD (CCA, C, O; STUN, ARTIH) */
        sum = add_32 (TO_DWORD (RD, RC),                /* add the two 32-bit double words on the stack */
                      TO_DWORD (RB, RA));

        RD = UPPER_WORD (sum);                          /* split the MSW */
        RC = LOWER_WORD (sum);                          /*   and the LSW of the sum */

        cpu_pop ();                                     /* pop the old TOS */
        cpu_pop ();                                     /*   and the old NOS */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 012:                                           /* DSUB (CCA, C, O; STUN, ARTIH) */
        difference = sub_32 (TO_DWORD (RD, RC),         /* subtract the two 32-bit double words on the stack */
                             TO_DWORD (RB, RA));

        RD = UPPER_WORD (difference);                   /* split the MSW */
        RC = LOWER_WORD (difference);                   /*   and the LSW of the difference */

        cpu_pop ();                                     /* pop the old TOS */
        cpu_pop ();                                     /*   and the old NOS */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 013:                                           /* MPYL (CCA, C, O; STUN, ARITH) */
        product = SEXT16 (RA) * SEXT16 (RB);            /* sign-extend the 16-bit operands and multiply */

        RB = UPPER_WORD (product);                      /* split the MSW */
        RA = LOWER_WORD (product);                      /*   and the LSW of the product */

        check = (uint32) product & S16_OVFL_MASK;           /* check the top 17 bits and set carry */
        SET_CARRY (check != 0 && check != S16_OVFL_MASK);   /*   if they are not all zeros or all ones */

        STA &= ~STATUS_O;                               /* clear O as this operation cannot overflow */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 014:                                           /* DIVL (CCA, O; STUN, ARITH) */
        dividend = INT32 (TO_DWORD (RC, RB));           /* convert the 32-bit dividend to a signed value */
        divisor  = SEXT16 (RA);                         /*   and sign-extend the 16-bit divisor */

        RB = RA;                                        /* delete the LSW from the stack now */
        cpu_pop ();                                     /*   to conform with the microcode */

        if (RA == 0)                                    /* if dividing by zero */
            MICRO_ABORT (trap_Integer_Zero_Divide);     /*   then trap or set the overflow flag */

        if (abs (divisor) <= abs (SEXT16 (RB)))         /* if the divisor is <= the MSW of the dividend */
            SET_OVERFLOW (TRUE);                        /*   an overflow will occur on the division */

        else {                                          /* otherwise, the divisor might be large enough */
            quotient  = dividend / divisor;             /* form the 32-bit signed quotient */
            remainder = dividend % divisor;             /*   and 32-bit signed remainder */

            check = (uint32) quotient & S16_OVFL_MASK;              /* check the top 17 bits and set overflow */
            SET_OVERFLOW (check != 0 && check != S16_OVFL_MASK);    /*   if they are not all zeros or all ones */

            RA = remainder & R_MASK;                    /* store the remainder on the TOS */
            RB = quotient  & R_MASK;                    /*   and the quotient on the NOS */

            SET_CCA (RB, 0);                            /* set the condition code */
            }
        break;


    case 015:                                           /* DNEG (CCA, O; STUN, ARITH) */
        difference = sub_32 (0, TO_DWORD (RB, RA));     /* negate the 32-bit double word on the stack */

        RB = UPPER_WORD (difference);                   /* split the MSW */
        RA = LOWER_WORD (difference);                   /*   and the LSW of the difference */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 016:                                           /* DXCH (CCA; STUN) */
        exchanger = RA;                                 /* exchange */
        RA = RC;                                        /*   the TOS */
        RC = exchanger;                                 /*     and the third stack word */

        exchanger = RB;                                 /* exchange */
        RB = RD;                                        /*   the NOS */
        RD = exchanger;                                 /*     and the fourth stack word */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 017:                                           /* CMP (CCC; STUN) */
        SET_CCC (RB, 0, RA, 0);                         /* set the (integer) condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        cpu_pop ();                                     /*     and the NOS */
        break;


    case 020:                                           /* ADD (CCA, C, O; STUN, ARITH) */
        RB = cpu_add_16 (RB, RA);                       /* add the NOS and TOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the old TOS */
        break;


    case 021:                                           /* SUB (CCA, C, O; STUN, ARITH) */
        RB = cpu_sub_16 (RB, RA);                       /* subtract the NOS and TOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the old TOS */
        break;


    case 022:                                           /* MPY (CCA, O; STUN, ARITH) */
        RB = cpu_mpy_16 (RA, RB);                       /* multiply the NOS and TOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the old TOS */
        break;


    case 023:                                           /* DIV (CCA, O; STUN, ARITH) */
        if (RA == 0)                                    /* if dividing by zero */
            MICRO_ABORT (trap_Integer_Zero_Divide);     /*   then trap or set the overflow flag */

        dividend = SEXT16 (RB);                         /* sign-extend the 16-bit dividend */
        divisor  = SEXT16 (RA);                         /*   and the 16-bit divisor */

        quotient  = dividend / divisor;                 /* form the 32-bit signed quotient */
        remainder = dividend % divisor;                 /*   and 32-bit signed remainder */

        SET_OVERFLOW (dividend == -32768 && divisor == -1); /* set overflow for -2**15 / -1 */

        RA = remainder & R_MASK;                        /* store the remainder on the TOS */
        RB = quotient  & R_MASK;                        /*   and the quotient on the NOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        break;


    case 024:                                           /* NEG (CCA, C, O; STUN, ARTIH) */
        RA = cpu_sub_16 (0, RA);                        /* negate the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 025:                                           /* TEST (CCA; STUN) */
        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 026:                                           /* STBX (CCA; STUN) */
        X = RB;                                         /* store the NOS into X */
        SET_CCA (X, 0);                                 /*   and set the condition code */
        break;


    case 027:                                           /* DTST (CCA, C; STUN) */
        SET_CCA (RB, RA);                               /* set the condition code */

        check = TO_DWORD (RB, RA) & S16_OVFL_MASK;          /* check the top 17 bits and set carry */
        SET_CARRY (check != 0 && check != S16_OVFL_MASK);   /*   if they are not all zeros or all ones */
        break;


    case 030:                                           /* DFLT (CCA; none) */
        operand_u.precision = in_d;                     /* set the operand precision to double integer */

        operand_u.words [0] = RB;                       /* load the MSW */
        operand_u.words [1] = RA;                       /*   and LSW of the operand */

        operand_v = fp_exec (fp_flt, operand_u, FP_NOP);    /* convert the integer to floating point */

        RB = operand_v.words [0];                       /* unload the MSW */
        RA = operand_v.words [1];                       /*   and the LSW of the result */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 031:                                           /* BTST (CCB; STUN) */
        SET_CCB (LOWER_BYTE (RA));                      /* set the condition code */
        break;


    case 032:                                           /* XCH (CCA; STUN) */
        exchanger = RA;                                 /* exchange */
        RA = RB;                                        /*   the TOS */
        RB = exchanger;                                 /*     and the NOS */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 033:                                           /* INCA (CCA, C, O; STUN, ARITH) */
        RA = cpu_add_16 (RA, 1);                        /* increment the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 034:                                           /* DECA (CCA, C, O; STUN, ARITH) */
        RA = cpu_sub_16 (RA, 1);                        /* decrement the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 035:                                           /* XAX (CCA; STUN) */
        exchanger = X;                                  /* exchange */
        X = RA;                                         /*   the TOS */
        RA = exchanger;                                 /*     and X */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 036:                                           /* ADAX (CCA, C, O; STUN, ARITH) */
        X = cpu_add_16 (X, RA);                         /* add the TOS to X */
        cpu_pop ();                                     /*   and pop the TOS */

        SET_CCA (X, 0);                                 /* set the condition code */
        break;


    case 037:                                           /* ADXA (CCA, C, O; STUN, ARITH) */
        RA = cpu_add_16 (X, RA);                        /* add X to the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 040:                                           /* DEL (none; STUN) */
        cpu_pop ();                                     /* pop the TOS */
        break;


    case 041:                                           /* ZROB (none; STUN) */
        RB = 0;                                         /* set the NOS to zero */
        break;


    case 042:                                           /* LDXB (CCA; STUN) */
        RB = X;                                         /* load X into the NOS */
        SET_CCA (RB, 0);                                /*   and set the condition code */
        break;


    case 043:                                           /* STAX (CCA; STUN) */
        X = RA;                                         /* store the TOS into X */
        cpu_pop ();                                     /*   and pop the TOS */

        SET_CCA (X, 0);                                 /* set the condition code */
        break;


    case 044:                                           /* LDXA (CCA; STOV) */
        cpu_push ();                                    /* push the stack down */
        RA = X;                                         /*   and set the TOS to X */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 045:                                           /* DUP (CCA; STUN, STOV) */
        cpu_push ();                                    /* push the stack down */
        RA = RB;                                        /*   and copy the old TOS to the new TOS */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 046:                                           /* DDUP (CCA; STUN, STOV) */
        cpu_push ();                                    /* push the stack */
        cpu_push ();                                    /*   down twice */

        RA = RC;                                        /* copy the old TOS and NOS */
        RB = RD;                                        /*   to the new TOS and NOS */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 047:                                           /* FLT (CCA; none) */
        operand_u.precision = in_s;                     /* set the operand precision to single integer */

        operand_u.words [0] = RA;                       /* load the operand */

        operand_v = fp_exec (fp_flt, operand_u, FP_NOP);    /* convert the integer to floating point */

        cpu_push ();                                    /* push the stack down */

        RB = operand_v.words [0];                       /* unload the MSW */
        RA = operand_v.words [1];                       /*   and the LSW of the result */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 050:                                           /* FCMP (CCC; STUN) */
        if (RB & RD & D16_SIGN)                         /* if the operand signs are both negative */
            SET_CCC (RB, RA, RD, RC);                   /*   then swap operands and compare the magnitudes */
        else                                            /* otherwise */
            SET_CCC (RD, RC, RB, RA);                   /*   compare them as they are */

        SR = 0;                                         /* pop all four values */
        break;


    case 051:                                           /* FADD (CCA, O; STUN, ARITH) */
    case 052:                                           /* FSUB (CCA, O; STUN, ARITH) */
    case 053:                                           /* FMPY (CCA, O; STUN, ARITH) */
    case 054:                                           /* FDIV (CCA, O; STUN, ARITH) */
        operand_u.precision = fp_f;                     /* set the operand precision to single_float */
        operand_v.precision = fp_f;                     /*   and the result precision to single float */

        operand_u.words [0] = RD;                       /* load the MSW */
        operand_u.words [1] = RC;                       /*   and LSW of the first operand */

        operand_v.words [0] = RB;                       /* load the MSW */
        operand_v.words [1] = RA;                       /*   and LSW of the second operand */

        STA &= ~STATUS_O;                               /* clear the overflow flag */

        cpu_pop ();                                     /* delete two words */
        cpu_pop ();                                     /*   from the stack */

        operand_w =                                         /* call the floating-point executor */
           fp_exec ((FP_OPR) (operation - 051 + fp_add),    /*   and convert the opcode */
                    operand_u, operand_v);                  /*     to an arithmetic operation */

        RB = operand_w.words [0];                       /* unload the MSW */
        RA = operand_w.words [1];                       /*   and the LSW of the result */

        if (operand_w.trap != trap_None) {                  /* if an error occurred */
            if (operand_w.trap == trap_Float_Zero_Divide)   /*   then if it is division by zero */
                SET_CCA (RB, RA);                           /*     then set the condition code */

            MICRO_ABORT (operand_w.trap);               /* trap or set overflow */
            }

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 055:                                           /* FNEG (CCA; STUN) */
        if ((RB | RA) == 0)                             /* if the floating point value is zero */
            SET_CCE;                                    /*   then it remains zero after negation */

        else {                                          /* otherwise */
            RB = RB ^ D16_SIGN;                         /*   flip the sign bit */
            SET_CCA (RB, 1);                            /*     and set CCL or CCG from the sign bit */
            }
        break;


    case 056:                                           /* CAB (CCA; STUN) */
        exchanger = RC;                                 /* rotate */
        RC = RB;                                        /*   the TOS */
        RB = RA;                                        /*     the NOS */
        RA = exchanger;                                 /*       and the third stack word */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 057:                                           /* LCMP (CCC; STUN) */
        SET_CCC (0, RB, 0, RA);                         /* set the (logical) condition code */

        cpu_pop ();                                     /* pop the TOS */
        cpu_pop ();                                     /*   and the NOS */
        break;


    case 060:                                           /* LADD (CCA, C; STUN) */
        sum = RB + RA;                                  /* add the values */

        SET_CARRY (sum > D16_UMAX);                     /* set C if there's a carry out of the MSB */

        RB = sum & R_MASK;                              /* store the sum in the NOS */
        cpu_pop ();                                     /*   and pop the TOS */

        SET_CCA (RA, 0);                                /* set the (integer) condition code */
        break;


    case 061:                                           /* LSUB (CCA, C; STUN) */
        SET_CARRY (RA <= RB);                           /* set C if there will not be a borrow by the MSB */

        RB = RB - RA & R_MASK;                          /* subtract the values */
        cpu_pop ();                                     /*   and pop the TOS */

        SET_CCA (RA, 0);                                /* set the (integer) condition code */
        break;


    case 062:                                           /* LMPY (CCA, C; STUN) */
        uproduct = RB * RA;                             /* multiply the operands */

        RA = LOWER_WORD (uproduct);                     /* split the MSW */
        RB = UPPER_WORD (uproduct);                     /*   and the LSW of the product */

        SET_CARRY (RB > 0);                             /* set C if the product doesn't fit in one word */

        SET_CCA (RB, RA);                               /* set the (integer) condition code */
        break;


    case 063:                                           /* LDIV (CCA, O; STUN, ARITH) */
        if (RA == 0)                                    /* if dividing by zero */
            MICRO_ABORT (trap_Integer_Zero_Divide);     /*   then trap or set the overflow flag */

        udividend = TO_DWORD (RC, RB);                  /* form the 32-bit unsigned dividend */

        uquotient  = udividend / RA;                    /* form the 32-bit unsigned quotient */
        uremainder = udividend % RA;                    /*   and 32-bit unsigned remainder */

        SET_OVERFLOW (uquotient & ~D16_MASK);           /* set O if the quotient needs more than 16 bits */

        cpu_pop ();                                     /* pop the TOS */

        RA = LOWER_WORD (uremainder);                   /* store the remainder on the TOS */
        RB = LOWER_WORD (uquotient);                    /*   and the quotient on the NOS */

        SET_CCA (RB, 0);                                /* set the (integer) condition code */
        break;


    case 064:                                           /* NOT (CCA; STUN) */
        RA = ~RA & R_MASK;                              /* complement the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 065:                                           /* OR (CCA; STUN) */
        RB = RA | RB;                                   /* logically OR the TOS and NOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        break;


    case 066:                                           /* XOR (CCA; STUN) */
        RB = RA ^ RB;                                   /* logically XOR the TOS and NOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        break;


    case 067:                                           /* AND (CCA; STUN) */
        RB = RA & RB;                                   /* logically AND the TOS and NOS */

        SET_CCA (RB, 0);                                /* set the condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        break;


    case 070:                                           /* FIXR (CCA, C, O; STUN, ARITH) */
    case 071:                                           /* FIXT (CCA, C, O; STUN, ARITH) */
        operand_u.precision = fp_f;                     /* set the operand precision to single_float */

        operand_u.words [0] = RB;                       /* load the MSW */
        operand_u.words [1] = RA;                       /*   and LSW of the operand */

        STA &= ~(STATUS_C | STATUS_O);                  /* the microcode clears the carry and overflow flags here */

        operand_v =                                         /* call the floating-point executor */
           fp_exec ((FP_OPR) (operation - 070 + fp_fixr),   /*   and convert the opcode */
                    operand_u, FP_NOP);                     /*     to a fix operation */

        if (operand_v.trap != trap_None) {              /* if an overflow occurs */
            RB = RB & FRACTION_BITS | ASSUMED_BIT;      /*   then the microcode masks and restores */
            MICRO_ABORT (operand_v.trap);               /*     the leading 1 to the mantissa before trapping */
            }

        RB = operand_v.words [0];                       /* unload the MSW */
        RA = operand_v.words [1];                       /*   and the LSW of the result */

        check = TO_DWORD (RB, RA) & S16_OVFL_MASK;          /* check the top 17 bits and set carry */
        SET_CARRY (check != 0 && check != S16_OVFL_MASK);   /*   if they are not all zeros or all ones */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 072:                                           /* unimplemented */
        status = STOP_UNIMPL;                           /* report that the instruction was not executed */
        STA = entry_status;                             /*   and restore the status register entry value */
        break;


    case 073:                                           /* INCB (CCA, C, O; STUN, ARITH) */
        RB = cpu_add_16 (RB, 1);                        /* increment the NOS */
        SET_CCA (RB, 0);                                /*   and set the condition code */
        break;


    case 074:                                           /* DECB (CCA, C, O; STUN, ARITH) */
        RB = cpu_sub_16 (RB, 1);                        /* decrement the NOS */
        SET_CCA (RB, 0);                                /*   and set the condition code */
        break;


    case 075:                                           /* XBX (none; STUN) */
        exchanger = X;                                  /* exchange */
        X = RB;                                         /*   the NOS */
        RB = exchanger;                                 /*     and X */
        break;


    case 076:                                           /* ADBX (CCA, C, O; STUN, ARITH) */
        X = cpu_add_16 (X, RB);                         /* add the NOS to X */
        SET_CCA (X, 0);                                 /*   and set the condition code */
        break;


    case 077:                                           /* ADXB (CCA, C, O; STUN, ARITH) */
        RB = cpu_add_16 (X, RB);                        /* add X to the NOS */
        SET_CCA (RB, 0);                                /*   and set the condition code */
        break;

     }                                                  /* all cases are handled  */

return status;                                          /* return the execution status */
}


/* Execute a shift, branch, or bit test instruction (subopcode 01).

   This routine is called to execute the shift, branch, or bit test instruction
   currently in the CIR.  The instruction formats are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |   shift opcode    |      shift count      |  Shift
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | I |   branch opcode   |+/-|  P displacement   |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |  bit test opcode  |     bit position      |  Bit Test
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The BCY, BNCY, BOV, and BNOV instructions will enter infinite loops if
       their displacements are zero, so they call the "cpu_branch_short" routine
       with loop checking enabled.  The other branch instructions will not enter
       an infinite loop, even with zero displacements, as they modify registers
       or the stack during execution, so they call the routine with loop
       checking disabled.

    2. All of the shift instructions except QASL and QASR use bit 9 to indicate
       a left (0) or right (1) shift and bit 4 to indicate that the shift count
       includes the index register value.  Bit 9 is always on for QASL and QASR,
       which use bit 4 to indicate a left or right shift, and which always
       include the index register value.  To simplify handling in the shifting
       routine, the QASL and QASR executors move the left/right indication to
       bit 9 and set bit 4 on before calling.
*/

t_stat cpu_shift_branch_bit_op (void)
{
static const uint8 preadjustment [32] = {       /* stack preadjustment, indexed by operation */
    1, 1, 1, 1, 1, 1, 1, 1,                     /*   ASL  ASR  LSL  LSR  CSL  CSR  SCAN IABZ    */
    3, 3, 0, 0, 0, 0, 3, 4,                     /*   TASL TASR IXBZ DXBZ BCY  BNCY TNSL QAS(LR) */
    2, 2, 2, 2, 2, 2, 2, 1,                     /*   DASL DASR DLSL DLSR DCSL DCSR CPRB DABZ    */
    0, 0, 1, 1, 1, 1, 1, 1                      /*   BOV  BNOV TBC  TRBC TSBC TCBC BRO  BRE     */
    };

HP_WORD opcode;
uint32  operation, bit_position, bit_mask, count;
t_stat  status = SCPE_OK;

operation = SBBOP (CIR);                                /* get the opcode from the instruction */

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the shift/branch/bit operation */

    case 000:                                           /* ASL (CCA; STUN) */
    case 001:                                           /* ASR (CCA; STUN) */
        shift_16_32 (CIR, arithmetic, size_16);         /* do an arithmetic left or right shift */
        break;


    case 002:                                           /* LSL (CCA; STUN) */
    case 003:                                           /* LSR (CCA; STUN) */
        shift_16_32 (CIR, logical, size_16);            /* do a logical left or right shift */
        break;


    case 004:                                           /* CSL (CCA; STUN) */
    case 005:                                           /* CSR (CCA; STUN) */
        shift_16_32 (CIR, circular, size_16);           /* do a circular left or right shift */
        break;


    case 006:                                           /* SCAN (CCA; STUN) */
        if (RA == 0)                                    /* if the TOS is zero */
            if (CIR & X_FLAG)                           /*   then if the instruction is indexed */
                X = X + 16 & R_MASK;                    /*     then add 16 to the index register value */
            else                                        /*   otherwise */
                X = 16;                                 /*     set the index register value to 16 */

        else {                                          /* otherwise the TOS is not zero */
            count = 0;                                  /*   so set up to scan for the first "one" bit */

            while ((RA & D16_SIGN) == 0) {              /* while the MSB is clear */
                RA = RA << 1;                           /*   shift the TOS left */
                count = count + 1;                      /*     while counting the shifts */
                }

            if (CIR & X_FLAG)                           /* if the instruction is indexed */
                X = X + count + 1 & R_MASK;             /*   then return the count + 1 */
            else                                        /* otherwise */
                X = count;                              /*   return the count */

            RA = RA << 1 & R_MASK;                      /* shift the leading "one" bit out of the TOS */
            }

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 007:                                           /* IABZ (CCA, C, O; STUN, BNDV) */
        RA = cpu_add_16 (RA, 1);                        /* increment the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */

        if (RA == 0)                                    /* if the TOS is now zero */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */
        break;


    case 010:                                           /* TASL (CCA; STUN) */
    case 011:                                           /* TASR (CCA; STUN) */
        shift_48_64 (CIR, arithmetic, size_48);         /* do a triple arithmetic left or right shift */
        break;


    case 012:                                           /* IXBZ (CCA, C, O; BNDV) */
        X = cpu_add_16 (X, 1);                          /* increment X */
        SET_CCA (X, 0);                                 /*   and set the condition code */

        if (X == 0)                                     /* if X is now zero */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */
        break;


    case 013:                                           /* DXBZ (CCA, C, O; BNDV) */
        X = cpu_sub_16 (X, 1);                          /* decrement X */
        SET_CCA (X, 0);                                 /*   and set the condition code */

        if (X == 0)                                     /* if X is now zero */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */
        break;


    case 014:                                           /* BCY (C = 0; BNDV) */
        if (STA & STATUS_C) {                           /* if the carry bit is set */
            STA &= ~STATUS_C;                           /*   then clear it */
            status = cpu_branch_short (TRUE);           /*     and branch to the target address */
            }
        break;


    case 015:                                           /* BNCY (C = 0; BNDV) */
        if (STA & STATUS_C)                             /* if the carry bit is set */
            STA &= ~STATUS_C;                           /*   then clear it and do not branch */
        else                                            /* otherwise the carry bit is clear */
            status = cpu_branch_short (TRUE);           /*   so branch to the target address */
        break;


    case 016:                                           /* TNSL (CCA; STUN) */
        shift_48_64 (CIR, normalizing, size_48);        /* do a triple normalizing left shift */
        break;


    case 017:                                           /* QASL (CCA; STUN), QASR (CCA; STUN) */
        if ((CIR & ~SHIFT_COUNT_MASK) == QASR)          /* transfer the left/right flag */
            opcode = CIR | SHIFT_RIGHT_FLAG | X_FLAG;   /*   to the same position */
        else                                            /*     as the other shift instructions use */
            opcode = CIR & ~SHIFT_RIGHT_FLAG | X_FLAG;  /*       and set the indexed flag on */

        shift_48_64 (opcode, arithmetic, size_64);      /* do a quadruple arithmetic left or right shift */
        break;


    case 020:                                           /* DASL (CCA; STUN) */
    case 021:                                           /* DASR (CCA; STUN) */
        shift_16_32 (CIR, arithmetic, size_32);         /* do a double arithmetic left or right shift */
        break;


    case 022:                                           /* DLSL (CCA; STUN) */
    case 023:                                           /* DLSR (CCA; STUN) */
        shift_16_32 (CIR, logical, size_32);            /* do a double logical left or right shift */
        break;


    case 024:                                           /* DCSL (CCA; STUN) */
    case 025:                                           /* DCSR (CCA; STUN) */
        shift_16_32 (CIR, circular, size_32);           /* do a double circular left or right shift */
        break;


    case 026:                                           /* CPRB (CCE, CCL, CCG; STUN, BNDV) */
        if (SEXT16 (X) < SEXT16 (RB))                   /* if X is less than the lower bound */
            SET_CCL;                                    /*   then set CCL and continue */

        else if (SEXT16 (X) > SEXT16 (RA))              /* otherwise if X is greater than the upper bound */
            SET_CCG;                                    /*   then set CCG and continue */

        else {                                          /* otherwise lower bound <= X <= upper bound */
            SET_CCE;                                    /*   so set CCE */
            status = cpu_branch_short (FALSE);          /*     and branch to the target address */
            }

        cpu_pop ();                                     /* pop the TOS */
        cpu_pop ();                                     /*   and the NOS */
        break;


    case 027:                                           /* DABZ (CCA, C, O; STUN, BNDV) */
        RA = cpu_sub_16 (RA, 1);                        /* decrement the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */

        if (RA == 0)                                    /* if the TOS is now zero */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */
        break;


    case 030:                                           /* BOV (O = 0; BNDV) */
        if (STA & STATUS_O) {                           /* if the overflow bit is set */
            STA &= ~STATUS_O;                           /*   then clear it */
            status = cpu_branch_short (TRUE);           /*     and branch to the target address */
            }
        break;


    case 031:                                           /* BNOV (O = 0; BNDV) */
        if (STA & STATUS_O)                             /* if the overflow bit is set */
            STA &= ~STATUS_O;                           /*   then clear it and do not branch */
        else                                            /* otherwise the overflow bit is clear */
            status = cpu_branch_short (TRUE);           /*   so branch to the target address */
        break;


    case 032:                                           /* TBC (CCA; STUN) */
    case 033:                                           /* TRBC (CCA; STUN) */
    case 034:                                           /* TSBC (CCA; STUN) */
    case 035:                                           /* TCBC (CCA; STUN) */
        bit_position = BIT_POSITION (CIR);              /* get the position of the bit to test */

        if (CIR & X_FLAG)                               /* if the instruction is indexed */
            bit_position = bit_position + X;            /*   then add the index register value */

        bit_mask = D16_SIGN >> bit_position % D16_WIDTH;    /* shift the bit mask to the desired location */

        SET_CCA (RA & bit_mask, 0);                     /* set the condition code */

        if (operation == 033)                           /* if the instruction is TRBC */
            RA = RA & ~bit_mask;                        /*   then reset the bit */

        else if (operation == 034)                      /* otherwise if the instruction is TSBC */
            RA = RA | bit_mask;                         /*   then set the bit */

        else if (operation == 035)                      /* otherwise if the instruction is TCBC */
            RA = RA ^ bit_mask;                         /*   then complement the bit */
        break;                                          /* or leave it alone for TBC */


    case 036:                                           /* BRO (none; STUN, BNDV) */
        if ((RA & 1) == 1)                              /* if the TOS is odd */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */

        cpu_pop ();                                     /* pop the TOS */
        break;


    case 037:                                           /* BRE (none; STUN, BNDV) */
        if ((RA & 1) == 0)                              /* if the TOS is even */
            status = cpu_branch_short (FALSE);          /*   then branch to the target address */

        cpu_pop ();                                     /* pop the TOS */
        break;

     }                                                  /* all cases are handled  */

return status;                                          /* return the execution status */
}


/* Execute a move, special, firmware, immediate, field, or register instruction (subopcode 02).

   This routine is called to execute the move, special, firmware, immediate,
   field, or register instruction currently in the CIR.  The instruction formats
   are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  move op  | opts/S decrement  |  Move
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  special op   | 0   0 | sp op |  Special
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 |      firmware option op       |  Firmware
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 |  imm opcode   |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | field opcode  |    J field    |    K field    |  Field
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 |  register op  | SK| DB| DL| Z |STA| X | Q | S |  Register
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The PSHR and SETR instructions follow the stack usage in the microcode
       so that SR contains the same value at the end of the instruction as in
       the hardware.  The sequence of stack flushes and queue-ups is therefore
       somewhat asymmetric.

    2. The microcode for the EXF and DPF instructions calculate the alignment
       shifts as 16 - (J + K) MOD 16 and then perform circular right and left
       shifts, respectively, to align the fields.  In simulation, the alignments
       are calculated as (J + K) MOD 16, and the opposite shifts (left and
       right, respectively) are employed.  This produces the same result, as a
       circular left shift of N bits is identical to a circular right shift of
       16 - N bits.
*/

t_stat cpu_move_spec_fw_imm_field_reg_op (void)
{
static const uint8 preadjustment [16] = {       /* stack preadjustment, indexed by operation */
    0, 4, 0, 0, 1, 1, 1, 1,                     /*   ---- ---- LDI  LDXI CMPI ADDI SUBI MPYI */
    1, 0, 0, 0, 1, 1, 2, 4                      /*   DIVI PSHR LDNI LDXN CMPN EXF  DPF  SETR */
    };

int32   divisor;
uint32  operation;
HP_WORD new_sbank, new_sm, new_q, start_bit, bit_count, bit_shift, bit_mask;
t_stat  status = SCPE_OK;

operation = MSFIFROP (CIR);                             /* get the opcode from the instruction */

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the operation */

    case 000:
        status = move_spec ();                          /* execute the move or special instruction */
        break;


    case 001:
        status = firmware_extension ();                 /* execute the DMUL, DDIV, or firmware extension instruction */
        break;


    case 002:                                           /* LDI (CCA; STOV) */
        cpu_push ();                                    /* push the stack down */
        RA = CIR & IMMED_MASK;                          /* store the immediate value on the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 003:                                           /* LDXI (none; none) */
        X = CIR & IMMED_MASK;                           /* load the immediate value into X */
        break;


    case 004:                                           /* CMPI (CCC; STUN) */
        SET_CCC (RA, 0, CIR & IMMED_MASK, 0);           /* set the condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        break;


    case 005:                                           /* ADDI (CCA, C, O; STUN, ARITH) */
        RA = cpu_add_16 (RA, CIR & IMMED_MASK);         /* sum the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 006:                                           /* SUBI (CCA, C, O; STUN, ARITH) */
        RA = cpu_sub_16 (RA, CIR & IMMED_MASK);         /* difference the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 007:                                           /* MPYI (CCA, O; STUN, STOV, ARITH) */
        cpu_push ();                                    /* the microcode does this for commonality with */
        cpu_pop ();                                     /*   MPY and MPYM, so we must too to get STOV */

        RA = cpu_mpy_16 (RA, CIR & IMMED_MASK);         /* multiply the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 010:                                           /* DIVI (CCA; STUN, ARITH) */
        divisor = (int32) CIR & IMMED_MASK;             /* get the immediate (positive) divisor */

        if (divisor == 0)                               /* if dividing by zero */
            MICRO_ABORT (trap_Integer_Zero_Divide);     /*   then trap or set the overflow flag */

        RA = SEXT16 (RA) / divisor & R_MASK;            /* store the quotient (which cannot overflow) on the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 011:                                           /* PSHR (none; STOV, MODE) */
        cpu_flush ();                                   /* flush the TOS register file */

        if (SM + 9 > Z)                                 /* check the stack for enough space */
            MICRO_ABORT (trap_Stack_Overflow);          /*   before pushing any of the registers */

        if (CIR & PSR_S) {                              /* if S is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = SM - DB & R_MASK;                      /*     and store delta S on the TOS */
            }

        if (CIR & PSR_Q) {                              /* if Q is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = Q - DB & R_MASK;                       /*     and store delta Q on the TOS */
            }

        if (CIR & PSR_X) {                              /* if X is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = X;                                     /*     and store X on the TOS */
            }

        if (CIR & PSR_STA) {                            /* if STA is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = STA;                                   /*     and store the status register on the TOS */
            cpu_flush ();                               /* flush the TOS register queue */
            }

        if (CIR & PSR_Z) {                              /* if Z is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = Z - DB & R_MASK;                       /*     and store delta Z on the TOS */
            }

        cpu_flush ();                                   /* flush the TOS register queue */

        if (CIR & PSR_DL) {                             /* if DL is to be stored */
            cpu_push ();                                /*   then push the stack down */
            RA = DL - DB & R_MASK;                      /*     and store delta DL on the TOS */
            }

        if (CIR & (PSR_DB_DBANK | PSR_SBANK)) {         /* if a bank register is to be stored */
            if (NPRV)                                   /*   then if the mode is not privileged */
                MICRO_ABORT (trap_Privilege_Violation); /*     then abort with a privilege violation */

            if (CIR & PSR_DB_DBANK) {                   /* if DBANK and DB are to be stored */
                cpu_push ();                            /*   then push the stack */
                cpu_push ();                            /*     down twice */
                RA = DB;                                /*       and store DB on the TOS */
                RB = DBANK;                             /*         and DBANK in the NOS */
                }

            if (CIR & PSR_SBANK) {                      /* if SBANK is to be stored */
                cpu_push ();                            /*   then push the stack down */
                RA = SBANK;                             /*     and store SBANK on the TOS */
                }
            }
        break;


    case 012:                                           /* LDNI (CCA; STOV) */
        cpu_push ();                                    /* push the stack down */
        RA = NEG16 (CIR & IMMED_MASK);                  /*   and store the negated immediate value on the TOS */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 013:                                           /* LDXN (none; none) */
        X = NEG16 (CIR & IMMED_MASK);                   /* store the negated immediate value into X */
        break;


    case 014:                                           /* CMPN (CCC; STUN) */
        SET_CCC (RA, 0, NEG16 (CIR & IMMED_MASK), 0);   /* set the condition code */
        cpu_pop ();                                     /*   and pop the TOS */
        break;


    case 015:                                           /* EXF (CCA; STUN) */
        start_bit = START_BIT (CIR);                    /* get the starting bit number */
        bit_count = BIT_COUNT (CIR);                    /*   and the number of bits */

        bit_shift = (start_bit + bit_count) % D16_WIDTH;    /* calculate the alignment shift */

        bit_mask = (1 << bit_count) - 1;                    /* form a right-justified mask */

        RA = (RA << bit_shift | RA >> D16_WIDTH - bit_shift)    /* rotate the TOS to align with the mask */
               & bit_mask;                                      /*   and then mask to the desired field */

        SET_CCA (RA, 0);                                   /* set the condition code */
        break;


    case 016:                                           /* DPF (CCA; STUN) */
        start_bit = START_BIT (CIR);                    /* get the starting bit number */
        bit_count = BIT_COUNT (CIR);                    /*   and the number of bits */

        bit_shift = (start_bit + bit_count) % D16_WIDTH;    /* calculate the alignment shift */

        bit_mask = (1 << bit_count) - 1;                    /* form a right-justified mask */

        bit_mask = bit_mask >> bit_shift                    /* rotate it into the correct position */
                     | bit_mask << D16_WIDTH - bit_shift;   /*   to mask the target field */

        RB = (RB & ~bit_mask                                /* mask the NOS and rotate and mask the TOS to fill */
               | (RA >> bit_shift | RA << D16_WIDTH - bit_shift) & bit_mask)
               & R_MASK;

        cpu_pop ();                                     /* pop the TOS */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 017:                                           /* SETR (none; STUN, STOV, MODE)*/
        new_sbank = 0;                                  /* quell erroneous uninitialized use warning */

        if (CIR & PSR_PRIV) {                           /* setting SBANK, DB, DL, and Z are privileged */
            if (NPRV)                                   /* if the mode is not privileged */
                MICRO_ABORT (trap_Privilege_Violation); /*   then abort with a privilege violation */

            if (CIR & PSR_SBANK) {                      /* if SBANK is to be set */
                new_sbank = RA;                         /*   then change it after the parameters are retrieved */
                cpu_pop ();                             /* pop the parameter */
                }

            if (CIR & PSR_DB_DBANK) {                   /* if DBANK and DB are to be set */
                DB = RA;                                /*   then set the */
                DBANK = RB & BA_MASK;                   /*     new values */
                cpu_pop ();                             /*       and pop the */
                cpu_pop ();                             /*         parameters */
                }

            if (CIR & PSR_DL) {                         /* if DL is to be set */
                DL = RA + DB & R_MASK;                  /*   then set the new value as an offset from DB */
                cpu_pop ();                             /*     and pop the parameter */
                }

            if (SR == 0)                                /* queue up a parameter */
                cpu_queue_up ();                        /*   if it is needed */

            if (CIR & PSR_Z) {                          /* if Z is to be set */
                Z = RA + DB & R_MASK;                   /*   then set the new value as an offset from DB */
                cpu_pop ();                             /*     and pop the parameter */
                }
                                                        /* queue up another parameter */
            if (SR == 0)                                /*   if it is needed */
                cpu_queue_up ();
            }

        if (CIR & PSR_STA) {                                    /* if STA is to be set */
            if (NPRV)                                           /*   then if the mode is not privileged */
                STA = STA & ~STATUS_NPRV | RA & STATUS_NPRV;    /*     then only T, O, C, and CC can be set */
            else                                                /*   otherwise privileged mode */
                STA = RA;                                       /*     allows the entire word to be set */

            if ((STA & STATUS_OVTRAP) == STATUS_OVTRAP) /* if overflow was set with trap enabled */
                CPX1 |= cpx1_INTOVFL;                   /*   then an interrupt occurs */

            cpu_pop ();                                 /* pop the parameter */

            if (SR == 0)                                /* queue up another parameter */
                cpu_queue_up ();                        /*   if it is needed */
            }

        if (CIR & PSR_X) {                              /* if X is to be set */
            X = RA;                                     /*   then set the new value */
            cpu_pop ();                                 /*     and pop the parameter */
            }

        if (CIR & PSR_Q) {                              /* if Q is to be set */
            if (SR == 0)                                /*   then queue up another parameter */
                cpu_queue_up ();                        /*     if it is needed */

            new_q = RA + DB & R_MASK;                   /* set the new value as an offset from DB */

            check_stack_bounds (new_q);                 /* trap if the new value is outside of the stack */

            Q = new_q;                                  /* set the new value */
            cpu_pop ();                                 /*   and pop the parameter */
            }

        if (CIR & PSR_S) {                              /* if S is to be set */
            if (SR == 0)                                /*   then queue up another parameter */
                cpu_queue_up ();                        /*     if it is needed */

            new_sm = RA + DB & R_MASK;                  /* set the new value as an offset from DB */

            check_stack_bounds (new_sm);                /* trap if the new value is outside of the stack */

            cpu_flush ();                               /* flush the TOS register file */
            SM = new_sm;                                /*   and set the new stack pointer value */
            }

        if (CIR & PSR_SBANK)                            /* if SBANK is to be set */
            SBANK = new_sbank & BA_MASK;                /*   then update the new value now */

        cpu_base_changed = (CIR != SETR && CIR != SETR_X);  /* set the flag if the base registers changed */
        break;
     }                                                  /* all cases are handled  */

return status;                                          /* return the execution status */
}


/* Execute an I/O, control, program, immediate, or memory instruction (subopcode 03).

   This routine is called to execute the I/O, control, program, immediate, or
   memory instruction currently in the CIR.  The instruction formats are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |  program op   |            N field            |  Program
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | immediate op  |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |   memory op   |        P displacement         |  Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The "N field" of the program instructions contains an index that is used to
   locate the "program label" that describes the procedure or subroutine to call
   or exit.  Labels have this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | U |                        address                        |  Local
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M |        STT number         |        segment number         |  External
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     U (uncallable) = the procedure is callable from privileged mode only
     M (mapped)     = the segment number is physically mapped
     address        = the PB-relative address of the procedure entry
     STT number     = the Segment Transfer Table entry within the target segment
     segment number = the number of the target segment

   The label is located either on the top of the stack (N = 0) or by indexing
   into the STT of the current code segment (N > 0).  Labels may be either
   local, indicating a transfer within the current segment, or external,
   indicating a transfer to another segment.


   Implementation notes:

    1. In hardware, the LDPP and LDPN microcode performs the bounds test E >= PB
       on the effective address E, then does a queue down if necessary, then
       performs the bounds test E < PL (instead of <= to account for second
       word), and then does another queue down if necessary, before reading the
       two words and storing them in the RA and RB registers.  Therefore, the
       order of possible traps is BNDV, STOV, BNDV, and STOV.

       In simulation, the "cpu_read_memory" routine normally checks the upper
       and lower bounds together.  This would lead to a trap order of BNDV,
       BNDV, STOV, and STOV.  To implement the microcode order, explicit bounds
       checks are interleaved with the stack pushes, and then unchecked reads
       are done to obtain the operands.
*/

t_stat cpu_io_cntl_prog_imm_mem_op (void)
{
static const uint8 preadjustment [16] = {       /* stack preadjustment, indexed by operation */
    0, 0, 0, 0, 1, 0, 0, 0,                     /*   ---- SCAL PCAL EXIT SXIT ADXI SBXI LLBL */
    0, 0, 1, 1, 0, 1, 1, 1                      /*   LDPP LDPN ADDS SUBS ---- ORI  XORI ANDI */
    };

ACCESS_CLASS class;
uint32       operation;
HP_WORD      field, operand, offset, new_p, new_q, new_sm, stt_length, label;
t_stat       status = SCPE_OK;

field = CIR & DISPL_255_MASK;                           /* get the N/immediate/displacement field value */

operation = IOCPIMOP (CIR);                             /* get the opcode from the instruction */

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the operation */

    case 000:
        status = io_control ();                         /* execute the I/O or control instruction */
        break;


    case 001:                                           /* SCAL (none; STOV, STUN, STTV, BNDV) */
        if (field == 0) {                               /* if the label is on the TOS */
            PREADJUST_SR (1);                           /*   then ensure a valid TOS register */
            label = RA;                                 /*     before getting the label */
            cpu_pop ();                                 /*       and popping it from the stack */
            }

        else                                                /* otherwise, the label is at M [PL-N] */
            cpu_read_memory (program_checked,               /*   so check the bounds */
                             PL - field & LA_MASK, &label); /*     and then read the label */

        cpu_flush ();                                   /* flush the TOS registers to memory */

        if (SM > Z) {                                   /* if the stack limit was exceeded */
            if (field == 0) {                           /*   then if the label was on the TOS */
                cpu_push ();                            /*     then push the stack down */
                RA = label;                             /*       and restore the label to the TOS */
                }

            MICRO_ABORT (trap_Stack_Overflow);          /* trap for a stack overflow */
            }

        if (label & LABEL_EXTERNAL)                     /* if the label is non-local */
            MICRO_ABORTP (trap_STT_Violation, STA);     /*   then trap for an STT violation */

        cpu_push ();                                    /* push the stack down */
        RA = P - 1 - PB & R_MASK;                       /*   and store the return address on the TOS */

        new_p = PB + (label & LABEL_ADDRESS_MASK);      /* get the subroutine entry address */

        cpu_read_memory (fetch_checked, new_p, &NIR);   /* check the bounds and get the first instruction */
        P = new_p + 1 & R_MASK;                         /*   and set P to point at the next instruction */
        break;


    case 002:                                           /* PCAL (none; STUN, STOV, CSTV, STTV, ABS CST, TRACE, UNCAL, BNDV) */
        if (field == 0) {                               /* if the label is on the TOS */
            PREADJUST_SR (1);                           /*   then ensure a valid TOS register */
            label = RA;                                 /*     before getting the label */
            cpu_pop ();                                 /*       and popping it from the stack */
            }

        else                                                /* otherwise, the label is at M [PL-N] */
            cpu_read_memory (program_checked,               /*   so check the bounds */
                             PL - field & LA_MASK, &label); /*     and then read the label */

        cpu_flush ();                                   /* flush the TOS registers to memory */

        if (SM > Z) {                                   /* if the stack limit was exceeded */
            if (field == 0) {                           /*   then if the label was on the TOS */
                cpu_push ();                            /*     then push the stack down */
                RA = label;                             /*       and restore the label to the TOS */
                }

            MICRO_ABORT (trap_Stack_Overflow);          /* trap for a stack overflow */
            }

        cpu_mark_stack ();                              /* write a stack marker */

        cpu_call_procedure (label, 0);                  /* set up PB, P, PL, and STA to call the procedure */
        break;


    case 003:                                           /* EXIT (CC; STUN, STOV, MODE, CSTV, TRACE, ABSCST, BNDV) */
        if (SM < Q)                                     /* if the stack memory pointer is below the stack marker */
            cpu_flush ();                               /*   then flush the TOS registers to memory */

        SR = 0;                                         /* invalidate the TOS registers */

        new_sm = Q - 4 - field & R_MASK;                /* compute the new stack pointer value */

        cpu_read_memory (stack, Q, &operand);           /* read the delta Q value from the stack marker */
        new_q = Q - operand & R_MASK;                   /*  and determine the new Q value */

        cpu_exit_procedure (new_q, new_sm, field);      /* set up the return code segment and stack */
        break;


    case 004:                                           /* SXIT (none; STUN, STOV, BNDV) */
        new_p = RA + PB & R_MASK;                       /* get the return address */
        cpu_read_memory (fetch_checked, new_p, &NIR);   /* check the bounds and then load the NIR */

        cpu_pop ();                                     /* pop the return address from the stack */

        if (field > 0 && SR > 0)                        /* if an adjustment is wanted and the TOS registers are occupied */
            cpu_flush ();                               /*   then flush the registers to memory */

        new_sm = SM - field & R_MASK;                   /* adjust the stack pointer as requested */

        check_stack_bounds (new_sm);                    /* trap if the new value is outside of the stack */
        SM = new_sm;                                    /*   before setting the new stack pointer value */

        P = new_p + 1 & R_MASK;                         /* set the new P value for the return */
        break;


    case 005:                                           /* ADXI (CCA; none) */
        X = X + field & R_MASK;                         /* add the immediate value to X */
        SET_CCA (X, 0);                                 /*    and set the condition code */
        break;


    case 006:                                           /* SBXI (CCA; none) */
        X = X - field & R_MASK;                         /* subtract the immediate value from X */
        SET_CCA (X, 0);                                 /*   and set the condition code */
        break;


    case 007:                                               /* LLBL (none; STOV, STTV) */
        cpu_read_memory (program_checked, PL, &stt_length); /* read the STT length */

        if ((stt_length & STT_LENGTH_MASK) < field)     /* if the STT index is not within the STT */
            MICRO_ABORTP (trap_STT_Violation, STA);     /*   then trap for an STT violation */

        cpu_read_memory (program_checked,               /* check the bounds */
                         PL - field & LA_MASK, &label); /*   and then read the label */

        if ((label & LABEL_EXTERNAL) == 0)              /* if the label is a local label */
            if (field > LABEL_STTN_MAX)                 /*   then if the STT number is too big for an external */
                MICRO_ABORTP (trap_STT_Violation, STA); /*     then trap for an STT violation */

            else                                        /*   otherwise */
                label = LABEL_EXTERNAL                  /*     convert it to an external label */
                          | (field << LABEL_STTN_SHIFT) /*       by merging the STT number */
                          | STATUS_CS (STA);            /*         with the currently executing segment number */

        cpu_push ();                                    /* push the stack down */
        RA = label;                                     /*   and store the label on the TOS */
        break;


    case 010:                                           /* LDPP (CCA; STOV, BNDV) */
    case 011:                                           /* LDPN (CCA; STOV, BNDV) */
        cpu_ea (CIR & MODE_DISP_MASK,                   /* get the address of the first word */
                &class, &offset, NULL);

        if (offset < PB && NPRV)                        /* if the offset is below PB and the mode is not privileged */
            MICRO_ABORT (trap_Bounds_Violation);        /*   then trap for a bounds violation */

        cpu_push ();                                    /* push the stack down */

        if (offset >= PL && NPRV)                       /* if the offset is at or above PL and the mode is not privileged */
            MICRO_ABORT (trap_Bounds_Violation);        /*   then trap for a bounds violation */

        cpu_push ();                                    /* push the stack down again */

        cpu_read_memory (program, offset, &operand);    /* read the first word */
        RB = operand;                                   /*   and store it in the NOS */

        offset = offset + 1 & LA_MASK;                  /* point at the second word */

        cpu_read_memory (program, offset, &operand);    /* read the second word */
        RA = operand;                                   /*   and store the on the TOS */

        SET_CCA (RB, RA);                               /* set the condition code */
        break;


    case 012:                                           /* ADDS (none; STUN, STOV) */
        if (field == 0)                                 /* if the immediate value is zero */
            field = RA - 1;                             /*   then use the TOS value - 1 instead */

        cpu_flush ();                                   /* empty the TOS registers */

        new_sm = SM + field & R_MASK;                   /* get the new stack pointer value */

        check_stack_bounds (new_sm);                    /* trap if the new value is outside of the stack */
        SM = new_sm;                                    /*   before setting the new stack pointer value */
        break;


    case 013:                                           /* SUBS (none; STUN, STOV) */
        if (field == 0)                                 /* if the immediate value is zero */
            field = RA + 1;                             /*   then use the TOS value + 1 instead */

        cpu_flush ();                                   /* empty the TOS registers */

        new_sm = SM - field & R_MASK;                   /* get the new stack pointer value */

        check_stack_bounds (new_sm);                    /* trap if the new value is outside of the stack */
        SM = new_sm;                                    /*   before setting the new stack pointer value */
        break;


    case 014:
        status = STOP_UNIMPL;                           /* opcodes 036000-036777 are unimplemented */
        break;


    case 015:                                           /* ORI (CCA; STUN) */
        RA = RA | field;                                /* logically OR the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 016:                                           /* XORI (CCA; STUN) */
        RA = RA ^ field;                                /* logically XOR the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 017:                                           /* ANDI (CCA; STUN) */
        RA = RA & field;                                /* logically AND the TOS and the immediate value */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;

     }                                                  /* all cases are handled  */

return status;                                          /* return the execution status */
}



/* CPU base set local utility routines */


/* Add two 32-bit numbers.

   Two 32-bit values are added, and the 32-bit sum is returned.  The C (carry)
   bit in the status register is set if the result is truncated and cleared
   otherwise.  The O (overflow) bit is set if the result exceeds the maximum
   positive or negative range, i.e., the result overflows into the sign bit.  In
   addition, an integer overflow interrupt (ARITH trap) occurs if the user trap
   bit is set.
*/

static uint32 add_32 (uint32 augend, uint32 addend)
{
t_uint64 sum;

sum = (t_uint64) augend + (t_uint64) addend;            /* sum the values */

SET_CARRY (sum > D32_UMAX);                             /* set C if there is a carry out of the MSB */

SET_OVERFLOW (D32_SIGN                                  /* set O if the signs */
                & (~augend ^ addend)                    /*   of the operands are the same */
                & (augend ^ sum));                      /*     but the sign of the result differs */

return (uint32) sum & D32_MASK;                         /* return the lower 32 bits of the sum */
}


/* Subtract two 32-bit numbers.

   Two 32-bit values are subtracted, and the 32-bit difference is returned.  The
   C (carry) bit in the status register is set if the subtraction did not
   require a borrow for the most-significant bit.  The O (overflow) bit is set
   if the result exceeds the maximum positive or negative range, i.e., the
   result borrows from the sign bit.  In addition, an integer overflow interrupt
   (ARITH trap) occurs if the user trap bit is set.


   Implementation notes:

    1. The carry bit is set to the complement of the borrow, i.e., carry = 0 if
       there is a borrow and 1 is there is not.
*/

static uint32 sub_32 (uint32 minuend, uint32 subtrahend)
{
t_uint64 difference;

difference = (t_uint64) minuend - (t_uint64) subtrahend;    /* subtract the values */

SET_CARRY (subtrahend <= minuend);                          /* set C if no borrow from the MSB was done */

SET_OVERFLOW (D32_SIGN                                      /* set O if the signs */
                & (minuend ^ subtrahend)                    /*   of the operands differ */
                & (minuend ^ difference));                  /*     as do the signs of the minuend and result */

return (uint32) difference & D32_MASK;                      /* return the lower 32 bits of the difference */
}


/* Shift single- and double-word operands.

   An arithmetic, logical, or circular left or right shift is performed in place
   on the 16-bit or 32-bit operand in RA or RB and RA, respectively.  Condition
   code A is set for the result.  The shift count and shift direction are
   derived from the instruction supplied.

   An arithmetic left shift retains the sign bit; an arithmetic right shift
   copies the sign bit.  Logical shifts fill zeros into the LSB or MSB.
   Circular shifts rotate bits out of the MSB and into the LSB, or vice versa.

   On entry, the shift count is extracted from the instruction.  If the
   instruction is indexed, the value in the X register is added to the count.

   For the type of shift selected, the fill bits are determined: sign bits fill
   for an arithmetic shift, zero bits fill for a logical shift, and operand bits
   fill for a circular shift.  The result of a shift in excess of the operand
   size is also determined.

   If the shift count is zero, then the result is the original operand.
   Otherwise, if the count is less than the operand size, the selected shift is
   performed.  A right shift of any type is done by shifting the operand and
   filling with bits of the appropriate type.  An arithmetic left shift is done
   by shifting the operand and restoring the sign.  A logical or circular shift
   is done by shifting the operand and filling with bits of the appropriate
   type.

   The result is restored to the TOS register(s), and CCA is set before
   returning.


   Implementation notes:

    1. An arithmetic left shift must be handled as a special case because the
       shifted operand bits "skip over" the sign bit.  That is, the bits are
       lost from the next-most-significant bit while preserving the MSB.  For
       all other shifts, including the arithmetic right shift, the operand may
       be shifted and then merged with the appropriate fill bits.

    2. The C standard specifies that the results of bitwise shifts with counts
       greater than the operand sizes are undefined, so we must handle excessive
       shifts explicitly.

    3. The C standard specifies that the results of bitwise shifts with negative
       signed operands are undefined (for left shifts) or implementation-defined
       (for right shifts).  Therefore, we must use unsigned operands and handle
       arithmetic shifts explicitly.

    4. The compiler requires a "default" case (instead of a "normalizing"
       case) for the switch statement.  Otherwise, it will complain that
       "fill" and "result" are potentially undefined, even though all
       enumeration values are covered.
*/

static void shift_16_32 (HP_WORD opcode, SHIFT_TYPE shift, OPERAND_SIZE op_size)
{
typedef struct {
    uint32 sign;                                /* the sign bit of the operand */
    uint32 data;                                /* the data mask of the operand */
    uint32 width;                               /* the width of the operand in bits */
    } PROPERTY;

static const PROPERTY prop [2] = {
    { D16_SIGN, D16_MASK & ~D16_SIGN, D16_WIDTH },      /* 16-bit operand properties */
    { D32_SIGN, D32_MASK & ~D32_SIGN, D32_WIDTH }       /* 32-bit operand properties */
    };

uint32 count, operand, fill, result;

count = SHIFT_COUNT (opcode);                           /* get the shift count from the instruction */

if (opcode & X_FLAG)                                    /* if the instruction is indexed */
    count = count + X & SHIFT_COUNT_MASK;               /*   then add the index to the count modulo 64 */

operand = RA;                                           /* get the (lower half of the) operand */

if (op_size == size_32)                                 /* if the operand size is 32 bits */
    operand = RB << D16_WIDTH | operand;                /*   then merge the upper half of the operand */

switch (shift) {                                        /* dispatch the shift operation */

    case arithmetic:                                    /* for an arithmetic shift */
        fill = operand & prop [op_size].sign ? ~0 : 0;  /*   fill with copies of the sign bit */

        if (opcode & SHIFT_RIGHT_FLAG)                  /* for a right shift */
            result = fill;                              /*   the excessive shift result is all fill bits */
        else                                            /* whereas for a left shift */
            result = prop [op_size].sign;               /*   the excessive shift result is just the sign bit */
        break;

    case logical:                                       /* for a logical shift */
        fill = 0;                                       /*   fill with zeros */
        result = 0;                                     /* the excessive shift result is all zeros */
        break;

    case circular:                                      /* for a circular shift */
        fill = operand;                                 /*   fill with the operand */
        count = count % prop [op_size].width;           /* an excessive shift count is reduced modulo the word width */
        result = 0;                                     /*   so there is no excessive shift result */
        break;

    default:                                            /* normalizing shifts are not used */
        return;
    }


if (count == 0)                                             /* if the shift count is zero */
    result = operand;                                       /*   then the result is the operand value */

else if (count < prop [op_size].width)                      /* otherwise if the shift count is not excessive */
    if (opcode & SHIFT_RIGHT_FLAG)                          /*   then if this is a right shift of any type */
        result = operand >> count                           /*     then shift the operand */
                   | fill << prop [op_size].width - count;  /*       and fill with fill bits */

    else if (shift == arithmetic)                           /* otherwise if this is an arithmetic left shift */
        result = operand << count & prop [op_size].data     /*   then shift the operand */
                   | fill & prop [op_size].sign;            /*     and restore the sign bit */

    else                                                    /* otherwise this is a logical or circular left shift */
        result = operand << count                           /*   so shift the operand */
                   | fill >> prop [op_size].width - count;  /*     and fill with fill bits */


RA = LOWER_WORD (result);                               /* store the lower word on the TOS */

if (op_size == size_16)                                 /* if the operand is a single word */
    SET_CCA (RA, 0);                                    /*   then set the condition code */

else {                                                  /* otherwise the operand is a double word */
    RB = UPPER_WORD (result);                           /*   so store the upper word in the NOS */
    SET_CCA (RB, RA);                                   /*     and then set the condition code */
    }

return;
}


/* Shift triple- and quad-word operands.

   An arithmetic left or right shift or normalizing left shift is performed
   in place on the 48-bit or 64-bit operand in RC/RB/RA or RD/RC/RB/RA,
   respectively.  Condition code A is set for the result.  The shift count and
   shift direction are derived from the instruction supplied.

   An arithmetic left shift retains the sign bit; an arithmetic right shift
   copies the sign bit.  A normalizing shift does not specify a shift count.
   Instead, the operand is shifted until bit 6 is set, bits 0-5 are cleared, and
   the shift count is returned in the X register.

   On entry for an arithmetic shift, the shift count is extracted from the
   instruction.  If the instruction is indexed, the value in the X register is
   added to the count.  If the shift count is zero, then the result is the
   original operand.  Otherwise, if the count is less than the operand size, the
   selected shift is performed.  A right shift is done by shifting the operand
   and filling with sign bits.  A left shift is done by shifting the operand and
   restoring the sign.

   For a normalizing shift with at least one bit set to the right of bit 5, the
   operand is left-shifted and X is incremented until bit 6 is set.  Bits 0-5
   are then masked off.  If no bits are set to the right of bit 5, X is set to,
   or incremented by, the maximum shift count, CCE is set, and the operand is
   not altered.

   After a successful shift, the result is restored to the TOS registers, and
   CCA is set before returning.


   Implementation notes:

    1. Logical and circular shifts are unsupported as they are not offered by
       the instruction set.

    2. All of the shift instructions except QASL and QASR use bit 9 to indicate
       a left (0) or right (1) shift and bit 4 to indicate that the shift count
       includes the index register value.  Bit 9 is always on for QASL and QASR,
       which use bit 4 to indicate a left or right shift, and which always
       include the index register value.  To simplify handling, the QASL and
       QASR executors move the left/right indication to bit 9 and set bit 4 on
       before calling this routine.
*/

static void shift_48_64 (HP_WORD opcode, SHIFT_TYPE shift, OPERAND_SIZE op_size)
{
typedef struct {
    t_uint64 sign;                              /* the sign bit of the operand */
    t_uint64 data;                              /* the data mask of the operand */
    uint32   width;                             /* the width of the operand in bits */
    uint32   padding;                           /* unused padding to suppress an alignment warning */
    } PROPERTY;

static const PROPERTY prop [4] = {
    { 0, 0, 0 },                                        /* (unused 16-bit properties) */
    { 0, 0, 0 },                                        /* (unused 32-bit properties) */
    { D48_SIGN, D48_MASK & ~D48_SIGN, D48_WIDTH },      /* 48-bit operand properties */
    { D64_SIGN, D64_MASK & ~D64_SIGN, D64_WIDTH }       /* 64-bit operand properties */
    };

uint32   count;
t_uint64 operand, fill, result;

operand = (t_uint64) RC << D32_WIDTH | TO_DWORD (RB, RA);   /* merge the first three words of the operand */

if (op_size == size_64)                                 /* if the operand size is 64 bits */
    operand = (t_uint64) RD << D48_WIDTH | operand;     /*   then merge the fourth word of the operand */

if (shift == arithmetic) {                              /* if this is an arithmetic shift */
    count = SHIFT_COUNT (opcode);                       /*   then the instruction contains the shift count */

    if (opcode & X_FLAG)                                /* if the instruction is indexed */
        count = count + X & SHIFT_COUNT_MASK;           /*   then add the index to the count modulo 64 */

    fill = operand & prop [op_size].sign ? ~0 : 0;      /* filling will use copies of the sign bit */

    if (count == 0)                                     /* if the shift count is zero */
        result = operand;                               /*   then the result is the operand value */

    else if (count < prop [op_size].width)                      /* otherwise if the shift count is not excessive */
        if (opcode & SHIFT_RIGHT_FLAG)                          /*   then if this is a right shift */
            result = operand >> count                           /*     then shift the operand */
                       | fill << prop [op_size].width - count;  /*       and fill with fill bits */
        else                                                    /* otherwise it is a left shift */
            result = operand << count & prop [op_size].data     /*   so shift the operand */
                       | fill & prop [op_size].sign;            /*     and restore the sign bit */

    else                                                /* otherwise the shift count exceeds the operand size */
        if (opcode & SHIFT_RIGHT_FLAG)                  /*   so if this is a right shift */
            result = fill;                              /*     then the excessive shift result is all fill bits */
        else                                            /*   whereas for a left shift */
            result = prop [op_size].sign;               /*     the excessive shift result is just the sign bit */
    }

else if (shift == normalizing) {                        /* otherwise if this is a (left) normalizing shift */
    if ((opcode & X_FLAG) == 0)                         /*   then if the instruction is not indexed */
        X = 0;                                          /*     then clear the shift count */

    if (operand & NORM_MASK) {                          /* if there's at least one unnormalized bit set */
        result = operand;                               /*   then start with the operand */

        while ((result & NORM_BIT) == 0) {              /* while the result is unnormalized */
            result = result << 1;                       /*   left-shift the result */
            X = X + 1;                                  /*     and increment the shift count */
            }

        result = result & NORM_MASK;                    /* mask off the leading bits */
        X = X & R_MASK;                                 /*   and wrap the count value */
        }

    else {                                              /* otherwise there are no bits to normalize */
        X = X + 42 & R_MASK;                            /*   so report the maximum shift count */

        SET_CCE;                                        /* set the condition code */
        return;                                         /*   and return with the operand unmodified */
        }
    }

else                                                    /* otherwise the shift type */
    return;                                             /*   is not supported by this routine */

RA = LOWER_WORD (result);                               /* restore the */
RB = UPPER_WORD (result);                               /*   lower three words */
RC = LOWER_WORD (result >> D32_WIDTH);                  /*     to the stack */

if (op_size == size_48)                                 /* if the operand size is 48 bits */
    SET_CCA (RC, RB | RA);                              /*   then set the condition code */

else {                                                  /* otherwise the size is 64 bits */
    RD = LOWER_WORD (result >> D48_WIDTH);              /*   so merge the upper word */
    SET_CCA (RD, RC | RB | RA);                         /*     and then set the condition code */
    }

return;
}


/* Check a value against the stack bounds.

   This routine checks a new frame (Q) or stack memory (SM) pointer value to
   ensure that it is within the stack bounds.  If the value does not lie between
   DB and Z, a trap will occur.

   The SETR instruction sets the frame and stack pointers, and the SXIT, ADDS,
   and SUBS instructions adjust the stack pointer.  Each verifies that the new
   value is between DB and Z before storing the value in the Q or SM register.
   If the value is greater than Z, a stack overflow trap is taken; if the value
   is less than DB, a stack underflow trap is taken.


   Implementation notes:

    1. Conceptually, ADDS can only exceed Z, whereas SXIT and SUBS can only drop
       below DB.  However, the microcode for all three instructions checks that
       both Z - new_SM and new_SM - DB are positive; if not, the routine traps
       to stack overflow or underflow, respectively.  As the new SM value is
       calculated modulo 2^16, wraparound overflows and underflows are caught
       only if they are within 32K of the Z or DB values.  For full coverage,
       both tests are necessary for each call, as an ADDS wraparound of 48K,
       e.g., would be caught as a stack underflow.  Simulation performs the same
       tests to obtain the same behavior, rather than checking that new_SM <= Z
       and DB <= new_SM.

    2. 32-bit subtractions are performed to ensure that wraparound overflows are
       caught.
*/

static void check_stack_bounds (HP_WORD new_value)
{
if ((uint32) Z - new_value > D16_SMAX)                  /* if the new value is not within 32K below Z */
    MICRO_ABORT (trap_Stack_Overflow);                  /*   then trap for an overflow */

else if ((uint32) new_value - DB > D16_SMAX && NPRV)    /* otherwise if the new value is not within 32K above DB */
    MICRO_ABORT (trap_Stack_Underflow);                 /*   then trap for an underflow unless the mode is privileged */

else                                                    /* otherwise the new value */
    return;                                             /*   is within the stack bounds */
}


/* Perform a test, control, or set interrupt I/O operation.

   The I/O operation specified in the "command" parameter is sent to the device
   whose device number stored on the stack at location S - K.  The K-field of
   the I/O instruction present in the CIR is extracted and subtracted from the
   current stack pointer.  The resulting memory location is read, and the lower
   byte is used as the device number.  The I/O command is sent, along with the
   value in the TOS for a CIO instruction, and the result is obtained.

   If the device number is invalid, an I/O timeout will result.  If this occurs,
   the timeout flag in CPX1 is reset, condition code "less than" is set, and
   this routine returns 0.  Otherwise, condition code "equal" is set to indicate
   success, and the device and result values are merged and returned (which will
   be non-zero, because zero is not a valid device number).


   Implementation notes:

    1. A checked access to memory is requested to obtain the device number.  As
       privileged mode has been previously ascertained, the memory check serves
       only to return a TOS register value if the resulting address is between
       SM and SR.
*/

static uint32 tcs_io (IO_COMMAND command)
{
uint32  address;
HP_WORD device, result;

if (NPRV)                                               /* if the mode is not privileged */
    MICRO_ABORT (trap_Privilege_Violation);             /*   then abort with a privilege violation */

address = SM + SR - IO_K (CIR) & LA_MASK;               /* get the location of the device number */

cpu_read_memory (stack, address, &device);              /* read it from the stack or TOS registers */
device = LOWER_BYTE (device);                           /*   and use only the lower byte of the value */

result = iop_direct_io (device, command,                /* send the I/O order to the device */
                        (command == ioCIO ? RA : 0));   /*   along with the control word for a CIO instruction */

if (CPX1 & cpx1_IOTIMER) {                              /* if an I/O timeout occurred */
    CPX1 &= ~cpx1_IOTIMER;                              /*   then clear the timer */
    SET_CCL;                                            /*     and set condition code "less than" */
    return 0;                                           /*        and fail the instruction */
    }

else {                                                  /* otherwise */
    SET_CCE;                                            /*   set the condition code for success */
    return TO_DWORD (device, result);                   /*     and return the (non-zero) device and result values */
    }
}


/* Perform a start, read, or write I/O operation.

   The I/O operation specified in the "command" parameter is sent to the device
   whose device number stored on the stack at location S - K, where K is the
   K-field value of the I/O instruction present in the CIR.  A Test I/O order is
   first sent to the device to determine if it is ready.  If the device number
   is invalid, the routine returns zero with condition code "less than" set to
   indicate failure.  If the Test I/O succeeded, the device number and test
   result are obtained.

   The test result is checked to see if the bit specified by the "ready_flag"
   parameter is set.  If it is not, then the device is not ready, so the test
   result is pushed onto the TOS, condition code "greater than" is set, and zero
   is returned to indicate failure.  If the bit is set, the device is ready for
   the operation.

   For a Start I/O order, the starting address of the I/O program, located on
   the TOS, is stored in the first word of the Device Reference Table entry
   corresponding to the device number.  The I/O command is sent, along with the
   value in the TOS for a WIO instruction, and the result is obtained.
   Condition code "equal" is set to indicate success, and the device and result
   values are merged and returned (which will be non-zero, because zero is not a
   valid device number).


   Implementation notes:

    1. The initial Test I/O order verifies that the mode is privileged and that
       the device number is valid.  Therefore, the result of the command
       operation need not be tested for validity.
*/

static uint32 srw_io (IO_COMMAND command, HP_WORD ready_flag)
{
uint32  test;
HP_WORD device, result;

test = tcs_io (ioTIO);                                  /* send a Test I/O order to the device */

if (test == 0)                                          /* if an I/O timeout occurred */
    return 0;                                           /*   then return 0 with CCL set to fail the instruction */

device = UPPER_WORD (test);                             /* split the returned value */
result = LOWER_WORD (test);                             /*   into the device number and test result */

if (result & ready_flag) {                              /* if the device is ready */
    if (command == ioSIO)                               /*   then if this is an SIO order */
        cpu_write_memory (absolute, device * 4, RA);    /*     then write the I/O program address to the DRT */

    result = iop_direct_io (device, command,                /* send the I/O order to the device */
                            (command == ioWIO ? RA : 0));   /*   along with the data word for a WIO instruction */

    SET_CCE;                                            /* set the condition code for success */
    return TO_DWORD (device, result);                   /*   and return the (non-zero) device and result values */
    }

else {                                                  /* otherwise the device is not ready */
    cpu_push ();                                        /*   so push the stack down */
    RA = result;                                        /*     and store the TIO response on the TOS */

    SET_CCG;                                            /* set the condition code to indicate "not ready" */
    return 0;                                           /*   and fail the instruction */
    }
}


/* Decrement the stack pointer.

   Pop values from the stack until the stack pointer has been decremented by the
   amount indicated by the "decrement" parameter.

   The word and byte move and comparison instructions include a stack decrement
   field that may be zero or a positive value indicating the number of words to
   remove at the end of the instruction.  This routine is called to implement
   this feature.

   Note that the stack decrement is performed only at the completion of these
   instructions.  If an instruction is interrupted, the decrement is not done,
   as the parameters on the stack will be needed when execution of the
   instruction is resumed after the interrupt handler completes.
*/

static void decrement_stack (uint32 decrement)
{
while (decrement > 0) {                                 /* decrement the stack pointer */
    cpu_pop ();                                         /*   by the count specified */
    decrement = decrement - 1;                          /*     by the instruction */
    }

return;
}


/* Move a block of words in memory.

   A block of words is moved from a source address to a destination address.  If
   a pending interrupt is detected during the move, the move is interrupted to
   service it.  Otherwise at the completion of the move, the stack is
   decremented by the amount indicated.

   On entry, the "source_class" parameter indicates the memory classification
   for source reads.  If the classification is absolute, the "source_base"
   parameter contains the physical address (i.e., memory bank and offset) of the
   base of the first word to move.  If it is not absolute, the parameter
   contains the offset within the bank implied by the classification.
   Similarly, the "dest_class" and "dest_base" parameters designate the base of
   the first word to write.  The "decrement" parameter contains the number of
   stack words to delete if the move completed successfully.

   If the source is absolute, the TOS registers RA, RB, and RD contain the
   count, source offset from the source base, and destination offset from the
   destination base, respectively.  Otherwise, the the TOS registers RA, RB, and
   RC contain the count and bases.

   Register RA contains an unsigned (positive) word count when called for the
   MTDS and MFDS instructions, and a signed word count otherwise.  If the word
   count is negative, the move is performed in reverse order, i.e., starting
   with the last word of the block and ending with the first word of the block.
   If the word count is zero on entry, the move is skipped, but the stack
   decrement is still performed.

   On exit, the TOS registers are updated for the block (or partial block, in
   the case of an intervening interrupt), and normal or error status from the
   interrupt check is returned.


   Implementation notes:

    1. This routine implements the MVWS microcode subroutine.

    2. The type of count (unsigned or signed) is determined by whether or not
       the CIR holds an MTDS or MFDS instruction.

    3. Incrementing and masking of the TOS registers must be done after each
       word is moved, rather than at loop completion, so that an interrupt will
       flush the correct TOS values to memory.
*/

static t_stat move_words (ACCESS_CLASS source_class, uint32 source_base,
                          ACCESS_CLASS dest_class,   uint32 dest_base,
                          uint32 decrement)
{
HP_WORD operand, *RX;
uint32  increment, source_bank, dest_bank;
t_stat  status;

if (RA & D16_SIGN && (CIR & MTFDS_MASK) != MTFDS)       /* if the count is signed and negative */
    increment = 0177777;                                /*   then the memory increment is negative */
else                                                    /* otherwise */
    increment = 1;                                      /*   the increment is positive */

source_bank = source_base & ~LA_MASK;                   /* extract the source bank */
dest_bank   = dest_base   & ~LA_MASK;                   /*   and destination bank in case they are needed */

if (source_class == absolute)                           /* if the source transfer is absolute */
    RX = & RD;                                          /*   then the destination offset is in RD */
else                                                    /* otherwise */
    RX = & RC;                                          /*   it is in RC */

while (RA != 0) {                                       /* while there are words to move */
    cpu_read_memory (source_class,                      /*   read a source word */
                     source_bank | source_base + RB & LA_MASK,
                     &operand);

    cpu_write_memory (dest_class,                       /* move it to the destination */
                      dest_bank | dest_base + *RX & LA_MASK,
                      operand);

    RA  = RA  - increment & R_MASK;                     /* update the count */
    RB  = RB  + increment & R_MASK;                     /*   and the source */
    *RX = *RX + increment & R_MASK;                     /*     and destination offsets */

    if (cpu_interrupt_pending (&status))                /* if an interrupt is pending */
        return status;                                  /*   then return with an interrupt set up or an error */
    }

decrement_stack (decrement);                            /* decrement the stack as indicated */
return SCPE_OK;                                         /*   and return the success of the move */
}



/* CPU base set local instruction execution routines */


/* Execute a move or special instruction (subopcode 02, field 00).

   This routine is called to execute the move or register instruction currently
   in the CIR.  The instruction formats are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  move op  | opts/S decrement  |  Move
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  special op   | 0   0 | sp op |  Special
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Byte move and compare instructions that specify byte counts (e.g., MVB, CMPB)
   bounds-check the starting and ending addresses to avoid checking each access
   separately.  Instructions that do not (e.g., SCW, MVBW) must bounds-check
   each access, as the counts are indeterminate.


   Implementation notes:

    1. CIR bits 8-12 are decoded to determine the instruction.  For some
       instructions, e.g., MOVE, bits 11 and 12 either designate options or are
       not decoded (i.e., are "don't care" bits).  These instructions are
       duplicated in the SR preadjustment table and carry multiple case labels
       in the instruction dispatcher.

    2. The IXIT, LOCK, PCN, and UNLK instructions decode bits 12-15, including
       the reserved bits 12 and 13.  The canonical forms have the reserved bits
       set to zero, but the hardware decodes bits 12-15 as IXIT = 0000, LOCK =
       nn01, PCN = nnn0, and UNLK = nn11 (where "n..." is any collective value
       other than 0).  If a non-canonical form is used, and the UNDEF stop is
       active, a simulation stop will occur.  If the stop is bypassed or not
       set, then the instruction will execute as in hardware.

       The LSEA, SSEA, LDEA, and SDEA instructions decode bits 14-15; the
       reserved bits 12-13 are not decoded and do not affect the instruction
       interpretation.

    3. Two methods of mapping non-canonical forms were examined: using a 16-way
       remapping table if SS_UNDEF was not set and a 4-way switch statement with
       the default returning STOP_UNDEF, or using a 16-way switch statement with
       the non-canonical values returning STOP_UNDEF if SS_UNDEF is set and
       falling into their respective executors otherwise.  The former required
       significantly more instructions and imposed a lookup penalty on canonical
       instructions for the non-stop case.  The latter used fewer instructions,
       imposed no penalty in the non-stop case, and the two extra tests required
       only three instructions each.  The latter method was adopted.

    4. The MVB and MVBW byte-move instructions perform read-modify-write actions
       for each byte moved.  This is inefficient -- each word is read and
       updated twice -- but it is necessary, as interrupts are checked after
       each byte is moved, and it is how the microcode handles these
       instructions.

    5. The MVBW instruction microcode performs bounds checks on the movement by
       determining the number of words from the source and target starting
       addresses to the address of the top of the stack (SM).  The smaller of
       these values is used as a count that is decremented within the move loop.
       When the count reaches zero, a bounds violation occurs if the mode is not
       privileged.  This is used instead of comparing the source and target
       addresses to SM to reduce the per-iteration checks from two to one.

    6. The IXIT microcode assumes that the machine is in privileged mode if the
       dispatcher-is-active flag is set.  In simulation, the privileged mode
       check is performed for all IXIT paths.

    7. When IXIT returns to a user process, the microcode sets the "trace flag"
       located at Q-13 in the ICS global area to -1.  The only description of
       this location is in the system tables manual, which says "flag set
       non-zero on IXIT away from ICS."  The action to set this value was added
       as a patch to the Series II microcode; however, this action is not
       present in the corresponding Series 64 microcode.  The description
       appears in the MPE V tables manual for version G.01.00 but is gone from
       the manual for version G.08.00.  No further information regarding the
       purpose of this flag has been found.

    8. The PCN microcode clears a TOS register via a queue-down operation, if
       necessary, before checking that the machine is in privileged mode.  In
       simulation, the check is performed before the register clear.  However,
       if a Mode Violation trap occurs, all of the TOS registers are flushed to
       memory, so the result is the same.
*/

static t_stat move_spec (void)
{
static const uint8 preadjustment [32] = {       /* stack preadjustment, indexed by operation */
    3, 3, 3, 3, 3, 3, 3, 3,                     /*   MOVE MOVE MOVE MOVE MVB  MVB  MVB  MVB  */
    4, 4, 2, 4, 4, 4, 2, 4,                     /*   MVBL MABS SCW  MTDS MVLB MDS  SCU  MFDS */
    2, 2, 2, 2, 3, 3, 3, 3,                     /*   MVBW MVBW MVBW MVBW CMPB CMPB CMPB CMPB */
    4, 4, 0, 0, 2, 2, 0, 0                      /*   RSW/LLSH  PLDA/PSTA xSEA/xDEA IXIT/etc. */
    };

int32        byte_count;
uint32       operation, address;
HP_WORD      operand, bank, offset, base;
HP_WORD      byte, test_byte, terminal_byte, increment, byte_class, loop_condition;
HP_WORD      source_bank, source, source_end, target_bank, target, target_end;
HP_WORD      stack_db, ics_q, delta_qi, disp_counter, delta_q, new_q, new_sm, device;
t_bool       q_is_qi, disp_active;
ACCESS_CLASS class;
t_stat       status = SCPE_OK;

operation = MSSUBOP (CIR);                              /* get the suboperation from the instruction */

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the move or special operation */

    case 000:                                           /* MOVE (none; STUN, STOV, BNDV) */
    case 001:
    case 002:
    case 003:
        if (RA != 0) {                                  /* if the word count is non-zero */
            if (RA & D16_SIGN)                          /*   then if it is negative */
                increment = 0177777;                    /*     then the memory increment is negative */
            else                                        /*   otherwise */
                increment = 1;                          /*     the increment is positive */

            while (SR > 3)                              /* if more than three TOS register are valid */
                cpu_queue_down ();                      /*   then queue them down until exactly three are left */

            if (CIR & DB_FLAG) {                                /* if the move is from the data segment */
                class = data;                                   /*   then set for data access */
                base = DB;                                      /*     and base the offset on DB */

                source = DB + RB & LA_MASK;                     /* determine the starting */
                source_end = source + RA - increment & LA_MASK; /*   and ending data source addresses */

                if (NPRV                                        /* if the mode is non-privileged */
                  && (source < DL || source > SM                /*   and the starting or ending address */
                  || source_end < DL || source_end > SM))       /*     is outside of the data segment */
                    MICRO_ABORT (trap_Bounds_Violation);        /*       then trap with a Bounds Violation */
                }

            else {                                              /* otherwise the move is from the code segment */
                class = program;                                /*   so set for program access */
                base = PB;                                      /*     and base the offset on PB */

                source = PB + RB & LA_MASK;                     /* determine the starting */
                source_end = source + RA - increment & LA_MASK; /*   and ending program source addresses */

                if (source < PB || source > PL                  /* if the starting or ending address */
                  || source_end < PB || source_end > PL)        /*   is outside of the code segment */
                    MICRO_ABORT (trap_Bounds_Violation);        /*     then trap with a Bounds Violation */
                }

            target = DB + RC & LA_MASK;                         /* calculate the starting */
            target_end = target + RA - increment & LA_MASK;     /*   and ending data target addresses */

            if (NPRV                                            /* if the mode is non-privileged */
              && (target < DL || target > SM                    /*   and the starting or ending target address */
              || target_end < DL || target_end > SM))           /*     is outside of the data segment */
                MICRO_ABORT (trap_Bounds_Violation);            /*       then trap with a Bounds Violation */

            status = move_words (class, base, data, DB,         /* move the words and adjust the stack */
                                 SDEC2 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC2 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 004:                                           /* MVB (none; STUN, STOV, BNDV) */
    case 005:
    case 006:
    case 007:
        while (SR > 3)                                  /* if more than three TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly three are left */

        if (RA != 0) {                                  /* if the byte count is non-zero */
            if (RA & D16_SIGN)                          /*   then if it is negative */
                increment = 0177777;                    /*     then the memory increment is negative */
            else                                        /*   otherwise */
                increment = 1;                          /*     the increment is positive */

            if (CIR & DB_FLAG) {                                /* if the move is from the data segment */
                class = data;                                   /*   then classify as a data access */
                source = cpu_byte_ea (data_checked, RB, RA);    /*     and convert and check the byte address */
                }

            else {                                              /* otherwise the move is from the code segment */
                class = program;                                /*   so classify as a program access */
                source = cpu_byte_ea (program_checked, RB, RA); /*     and convert and check the byte address */
                }

            target = cpu_byte_ea (data_checked, RC, RA);    /* convert the target byte address and check the bounds */

            while (RA != 0) {                               /* while there are bytes to move */
                cpu_read_memory (class, source, &operand);  /*   read a source word */

                if (RB & 1)                                 /* if the byte address is odd */
                    byte = LOWER_BYTE (operand);            /*   then get the lower byte */
                else                                        /* otherwise the address is even */
                    byte = UPPER_BYTE (operand);            /*   so get the upper byte */

                if ((RB & 1) == (HP_WORD) (increment == 1)) /* if the last byte of the source word was accessed */
                    source = source + increment & LA_MASK;  /*   then update the word address */

                cpu_read_memory (data, target, &operand);   /* read the target word */

                if (RC & 1)                                     /* if the byte address is odd */
                    operand = REPLACE_LOWER (operand, byte);    /*   then replace the lower byte */
                else                                            /* otherwise the address is even */
                    operand = REPLACE_UPPER (operand, byte);    /*   so replace the upper byte */

                cpu_write_memory (data, target, operand);   /* write the word back */

                if ((RC & 1) == (HP_WORD) (increment == 1)) /* if the last byte of the target word was accessed */
                    target = target + increment & LA_MASK;  /*   then update the word address */

                RA = RA - increment & R_MASK;               /* update the count */
                RB = RB + increment & R_MASK;               /*   and the source */
                RC = RC + increment & R_MASK;               /*     and destination offsets */

                if (cpu_interrupt_pending (&status))        /* if an interrupt is pending */
                    return status;                          /*   then return with an interrupt set up or an error */
                }
            }

        decrement_stack (SDEC2 (CIR));                  /* adjust the stack as indicated by the instruction */
        break;


    case 010:                                           /* MVBL (none; STUN, STOV, MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                  /* if the word count is non-zero */
            cpu_queue_down ();                          /*   then queue down so exactly three TOS registers are left */
            status = move_words (data, DB, stack, DL,   /*     and move the words and adjust the stack */
                                 SDEC2 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC2 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 011:                                           /* MABS (none; MODE, STUN) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                      /* if the word count is non-zero */
            cpu_read_memory (stack, SM, &target_bank);      /*   then get the target data bank number */

            status = move_words (absolute, TO_PA (RC, 0),   /* move the words and adjust the stack */
                                 absolute, TO_PA (target_bank, 0),
                                 SDEC3 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC3 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 012:                                           /* SCW (CCB, C; STUN, STOV, BNDV) */
    case 016:                                           /* SCU (C; STUN, STOV, BNDV) */
        while (SR > 2)                                  /* if more than two TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly two are left */

        test_byte     = LOWER_BYTE (RA);                /* get the test byte */
        terminal_byte = UPPER_BYTE (RA);                /*   and the terminal byte */

        source = cpu_byte_ea (data_checked, RB, 0);     /* convert the source byte address and check the bounds */

        cpu_read_memory (data, source, &operand);       /* read the first word */

        while (TRUE) {
            if (RB & 1) {                               /* if the byte address is odd */
                if (cpu_interrupt_pending (&status))    /*   then if an interrupt is pending */
                    return status;                      /*     then return with an interrupt set up or an error */

                byte = LOWER_BYTE (operand);            /* get the lower byte */
                source = source + 1 & LA_MASK;          /*   and update the word address */

                if (NPRV && source > SM)                    /* if non-privileged and the address is out of range */
                    MICRO_ABORT (trap_Bounds_Violation);    /*   then trap for a bounds violation */

                cpu_read_memory (data, source, &operand);   /* read the next word */
                }

            else                                        /* otherwise the address is even */
                byte = UPPER_BYTE (operand);            /*   so get the upper byte */

            if (operation == 012)                       /* if this is the "scan while" instruction */
                if (byte == test_byte)                  /*   then if the byte matches the test byte */
                    RB = RB + 1 & R_MASK;               /*     then update the byte offset and continue */

                else {                                  /*   otherwise the "while" condition fails */
                    SET_CARRY (byte == terminal_byte);  /*     so set carry if the byte matches the terminal byte */
                    SET_CCB (byte);                     /*       and set the condition code */
                    break;                              /*         and terminate the scan */
                    }
            else                                        /* otherwise this is the "scan until" instruction */
                if (byte == terminal_byte) {            /*   so if the byte matches the terminal byte */
                    STA |= STATUS_C;                    /*     then set the carry flag */
                    break;                              /*       and terminate the scan */
                    }

                else if (byte == test_byte) {           /*   otherwise if the byte matches the test byte */
                    STA &= ~STATUS_C;                   /*     then clear the carry flag */
                    break;                              /*       and terminate the scan */
                    }

                else                                    /*   otherwise neither byte matches */
                    RB = RB + 1 & R_MASK;               /*    so update the byte offset and continue */
            }

        decrement_stack (SDEC2 (CIR));                  /* adjust the stack as indicated by the instruction */
        break;


    case 013:                                           /* MTDS (none; MODE, DSTB, STUN, ABSDST) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                      /* if the word count is non-zero */
            cpu_setup_data_segment (RD, &bank, &offset);    /*   then get the target segment bank and address */

            status = move_words (data, DB,                  /* move the words and adjust the stack */
                                 absolute, TO_PA (bank, offset),
                                 SDEC3 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC3 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 014:                                           /* MVLB (none; STUN, STOV, MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                  /* if the word count is non-zero */
            cpu_queue_down ();                          /*   then queue down so exactly three TOS registers are left */

            status = move_words (stack, DL, data, DB,   /* move the words and adjust the stack */
                                 SDEC2 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC2 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 015:                                           /* MDS (none; MODE, DSTV, STUN, ABSDST) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                      /* if the word count is non-zero */
            cpu_read_memory (stack, SM, &operand);          /*   then get the target data segment number */

            cpu_setup_data_segment (operand, &target_bank,  /* get the target segment bank and address */
                                    &target);

            cpu_setup_data_segment (RC, &source_bank,       /*   and the source segment bank and address */
                                    &source);

            status = move_words (absolute, TO_PA (source_bank, source), /* move the words and adjust the stack */
                                 absolute, TO_PA (target_bank, target),
                                 SDEC3 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC3 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 017:                                           /* MFDS (none; MODE, DSTV, STUN, ABSDST) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (RA != 0) {                                          /* if the word count is non-zero */
            cpu_setup_data_segment (RC, &bank, &offset);        /*   then get the source segment bank and address */

            status = move_words (absolute, TO_PA (bank, offset),    /* move the words and adjust the stack */
                                 data, DB, SDEC3 (CIR));
            }

        else                                            /* otherwise there are no words to move */
            decrement_stack (SDEC3 (CIR));              /*   so just adjust the stack as indicated */
        break;


    case 020:                                           /* MVBW (CCB; STUN, STOV, BNDV) */
    case 021:
    case 022:
    case 023:
        while (SR > 2)                                  /* if more than two TOS registers are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly two are left */

        source = cpu_byte_ea (data_checked, RA, 0);     /* convert the source and target */
        target = cpu_byte_ea (data_checked, RB, 0);     /*   byte addresses and check the starting bounds */

        if (source > target) {                          /* if the source is closer to SM than the target */
            byte_count = (int32) (SM - source + 1) * 2; /*   then set the byte count from the source */

            if (RA & 1)                                 /* if starting with the lower byte */
                byte_count = byte_count - 1;            /*   then decrease the count by 1 */
            }

        else {                                          /* otherwise the target is closer to SM */
            byte_count = (int32) (SM - target + 1) * 2; /*   so set the byte count from the target */

            if (RB & 1)                                 /* if starting with the lower byte */
                byte_count = byte_count - 1;            /*   then decrease the count by 1 */
            }

        loop_condition = (CIR & MVBW_CCF) << MVBW_CCF_SHIFT;    /* get the loop condition code flags */

        while (TRUE) {                                  /* while the loop condition holds */
            cpu_read_memory (data, source, &operand);   /*   get the source word */

            if (RA & 1) {                               /* if the byte address is odd */
                byte = LOWER_BYTE (operand);            /*   then get the lower byte */
                source = source + 1 & LA_MASK;          /*     and update the word address */
                }

            else                                        /* otherwise the address is even */
                byte = UPPER_BYTE (operand);            /*   so get the upper byte */

            byte_class = cpu_ccb_table [byte];          /* classify the byte */

            if ((byte_class & loop_condition) == 0)     /* if the loop condition is false */
                break;                                  /*   then terminate the move */

            if (byte_class == CFE && CIR & MVBW_S_FLAG) /* if it's alphabetic and upshift is requested */
                byte = TO_UPPERCASE (byte);             /*   then upshift the character */

            if (byte_count == 0 && NPRV)                /* if source is beyond SM and not privileged */
                MICRO_ABORT (trap_Bounds_Violation);    /*   then trap for a bounds violation */

            cpu_read_memory (data, target, &operand);   /* read the target word */

            if (RB & 1)                                     /* if the byte address is odd */
                operand = REPLACE_LOWER (operand, byte);    /*   then replace the lower byte */
            else                                            /* otherwise the address is even */
                operand = REPLACE_UPPER (operand, byte);    /*   so replace the upper byte */

            cpu_write_memory (data, target, operand);   /* write the word back */

            if (RB & 1)                                 /* if the byte address is odd */
                target = target + 1 & LA_MASK;          /*   then update the word address */

            byte_count = byte_count - 1;                /* update the count */
            RA = RA + 1 & R_MASK;                       /*   and the source */
            RB = RB + 1 & R_MASK;                       /*     and destination offsets */

            if (cpu_interrupt_pending (&status))        /* if an interrupt is pending */
                return status;                          /*   then return with an interrupt set up or an error */
            }

        SET_CCB (byte);                                 /* set the condition code */

        decrement_stack (SDEC2 (CIR));                  /* adjust the stack as indicated by the instruction */
        break;


    case 024:                                           /* CMPB (CCx; STUN, STOV, BNDV) */
    case 025:
    case 026:
    case 027:
        while (SR > 3)                                  /* if more than three TOS register are valid */
            cpu_queue_down ();                          /*   then queue them down until exactly three are left */

        if (RA != 0) {                                  /* if the byte count is non-zero */
            if (RA & D16_SIGN)                          /*   then if it is negative */
                increment = 0177777;                    /*     then the memory increment is negative */
            else                                        /*   otherwise */
                increment = 1;                          /*     the increment is positive */

            if (CIR & DB_FLAG) {                                /* if the comparison is from the data segment */
                class = data;                                   /*   then classify as a data access */
                source = cpu_byte_ea (data_checked, RB, RA);    /*     and convert and check the byte address */
                }

            else {                                              /* otherwise the comparison is from the code segment */
                class = program;                                /*   so classify as a program access */
                source = cpu_byte_ea (program_checked, RB, RA); /*     and convert and check the byte address */
                }

            target = cpu_byte_ea (data_checked, RC, RA);    /* convert the target byte address and check the bounds */

            while (RA != 0) {                               /* while there are bytes to compare */
                cpu_read_memory (class, source, &operand);  /*   read a source word */

                if (RB & 1)                                 /* if the byte address is odd */
                    byte = LOWER_BYTE (operand);            /*   then get the lower byte */
                else                                        /* otherwise the address is even */
                    byte = UPPER_BYTE (operand);            /*   so get the upper byte */

                if ((RB & 1) == (HP_WORD) (increment == 1)) /* if the last byte of the source word was accessed */
                    source = source + increment & LA_MASK;  /*   then update the word address */

                cpu_read_memory (data, target, &operand);   /* read the target word */

                if (RC & 1)                                 /* if the byte address is odd */
                    test_byte = LOWER_BYTE (operand);       /*   then get the lower byte */
                else                                        /* otherwise the address is even */
                    test_byte = UPPER_BYTE (operand);       /*   so get the upper byte */

                if (test_byte != byte)                      /* if the bytes do not compare */
                    break;                                  /*   then terminate the loop */

                if ((RC & 1) == (HP_WORD) (increment == 1)) /* if the last byte of the target word was accessed */
                    target = target + increment & LA_MASK;  /*   then update the word address */

                RA = RA - increment & R_MASK;               /* update the count */
                RB = RB + increment & R_MASK;               /*   and the source */
                RC = RC + increment & R_MASK;               /*     and destination offsets */

                if (cpu_interrupt_pending (&status))        /* if an interrupt is pending */
                    return status;                          /*   then return with an interrupt set up or an error */
                }
            }

        if (RA == 0)                                    /* if the count expired */
            SET_CCE;                                    /*   then set condition code "equal" */

        else if (test_byte > byte)                      /* otherwise if the target byte > the source byte */
            SET_CCG;                                    /*   set condition code "greater than" */

        else                                            /* otherwise the target byte < the source byte */
            SET_CCL;                                    /*   so set condition code "less than" */

        decrement_stack (SDEC2 (CIR));                  /* adjust the stack as indicated by the instruction */
        break;


    case 030:                                           /* RSW and LLSH */
    case 031:
        if (CIR & 1) {                                  /* LLSH (CCx; STUN, MODE) */
            if (NPRV)                                   /* if the mode is not privileged */
                MICRO_ABORT (trap_Privilege_Violation); /*   then abort with a privilege violation */

            while (X > 0) {                             /* while the link count is non-zero */
                cpu_read_memory (absolute,              /*   read the target value */
                                 TO_PA (RB, RA + RD & LA_MASK),
                                 &target);

                if (target >= RC) {                     /* if the target is greater than or equal to the test word */
                    if (target == DV_UMAX)              /*   then if the target is the largest possible value */
                        SET_CCG;                        /*     then set condition code "greater than" */
                    else                                /*   otherwise */
                        SET_CCE;                        /*     set condition code "equal" */
                    break;                              /* end the search */
                    }

                address = TO_PA (RB, RA + 1 & LA_MASK); /* otherwise save the link offset address */

                cpu_read_memory (absolute, TO_PA (RB, RA), &RB);    /* read the next link bank */
                cpu_read_memory (absolute, address, &RA);           /*   and link offset */

                X = X - 1 & R_MASK;                     /* decrement the count */

                if (cpu_interrupt_pending (&status))    /* if an interrupt is pending */
                    return status;                      /*   then return with an interrupt set up or an error */
                }

            if (X == 0)                                 /* if the count expired */
                SET_CCL;                                /*   then set condition code "less than" */
            }

        else {                                          /* RSW (CCA; STUN, STOV) */
            cpu_push ();                                /* push the stack down */
            RA = SWCH;                                  /*   and set the TOS from the switch register */

            SET_CCA (RA, 0);                            /* set the condition code */
            }
        break;


    case 032:                                           /* PLDA and PSTA */
    case 033:
        if (PRIV)                                       /* if the mode is privileged */
            if (CIR & 1) {                              /* PSTA (none; STUN, MODE) */
                PREADJUST_SR (1);                       /* ensure a valid TOS register */
                cpu_write_memory (absolute_mapped,      /*   before writing the TOS to memory */
                                  X, RA);               /*     at address X */
                cpu_pop ();                             /*       and popping the stack */
                }

            else {                                      /* PLDA (CCA; STOV, MODE) */
                cpu_read_memory (absolute_mapped,       /* read the value at address X */
                                 X, &operand);
                cpu_push ();                            /* push the stack down */
                RA = operand;                           /*   and store the value on the TOS */

                SET_CCA (RA, 0);                        /* set the condition code */
                }

        else                                            /* otherwise the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   so abort with a privilege violation */
        break;


    case 034:                                           /* LSEA, SSEA, LDEA, and SDEA */
    case 035:
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        switch (SPECOP (CIR) & 3) {                     /* dispatch the special operation (bits 12-13 are not decoded) */

            case 000:                                   /* LSEA (CCA; STUN, STOV, MODE) */
                while (SR > 2)                          /* if more than two TOS register are valid */
                    cpu_queue_down ();                  /*   then queue them down until exactly two are left */

                address = TO_PA (RB, RA);               /* form the physical address */
                cpu_read_memory (absolute,              /*   and read the word from memory */
                                 address, &operand);

                cpu_push ();                            /* push the stack down */
                RA = operand;                           /*   and store the word on the TOS */

                SET_CCA (RA, 0);                        /* set the condition code */
                break;


            case 001:                                   /* SSEA (none; STUN, STOV, MODE) */
                PREADJUST_SR (3);                       /* ensure there are three valid TOS registers */

                while (SR > 3)                          /* if more than three TOS register are valid */
                    cpu_queue_down ();                  /*   then queue them down until exactly three are left */

                address = TO_PA (RC, RB);               /* form the physical address */
                cpu_write_memory (absolute,             /*   and write the word on the TOS to memory */
                                  address, RA);

                cpu_pop ();                             /* pop the TOS */
                break;


            case 002:                                   /* LDEA (CCA; STUN, STOV, MODE) */
                while (SR > 2)                          /* if more than two TOS register are valid */
                    cpu_queue_down ();                  /*   then queue them down until exactly two are left */

                address = TO_PA (RB, RA);               /* form the physical address */
                cpu_read_memory (absolute,              /*   and read the MSW from memory */
                                 address, &operand);

                cpu_push ();                            /* push the stack down */
                RA = operand;                           /*   and store the MSW on the TOS */

                address = TO_PA (RC, RB + 1 & LA_MASK); /* increment the physical address */
                cpu_read_memory (absolute,              /*   and read the LSW from memory */
                                 address, &operand);

                cpu_push ();                            /* push the stack down again */
                RA = operand;                           /*   and store the LSW on the TOS */

                SET_CCA (RB, RA);                       /* set the condition code */
                break;


            case 003:                                   /* SDEA (none; STUN, MODE) */
                PREADJUST_SR (4);                       /* ensure there are four valid TOS registers */

                address = TO_PA (RD, RC);               /* form the physical address */
                cpu_write_memory (absolute,             /* write the MSW from the NOS to memory */
                                  address, RB);

                address = TO_PA (RD, RC + 1 & LA_MASK); /* increment the physical address */
                cpu_write_memory (absolute,             /*   and write the LSW on the TOS to memory */
                                  address, RA);

                cpu_pop ();                             /* pop the TOS */
                cpu_pop ();                             /*   and the NOS */
                break;
            }                                           /* all cases are handled */
        break;


    case 036:                                           /* IXIT, LOCK, PCN, and UNLK */
    case 037:
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        switch (SPECOP (CIR)) {                         /* dispatch the special operation */

            case 000:                                   /* IXIT (none; MODE, STOV, CSTV, TRACE, ABS CST, BNDV) */
                SR = 0;                                 /* invalidate the TOS registers */

                cpu_read_memory (stack, Q, &delta_q);               /* read the stack marker link value */
                cpu_read_memory (absolute, ICS_Q, &ics_q);          /*   the stack marker initial value */
                cpu_read_memory (absolute, ics_q, &delta_qi);       /*     the dispatcher stack marker link */
                cpu_read_memory (absolute, ics_q - 18 & LA_MASK,    /*       and the dispatcher counter */
                                 &disp_counter);

                q_is_qi = (Q == ics_q);                     /* TRUE if Q = QI, i.e., a user process was interrupted */
                disp_active = (CPX1 & cpx1_DISPFLAG) != 0;  /* TRUE if the dispatcher is currently active */

                new_sm = 0;                                 /* these will be set by every path through IXIT */
                new_q  = 0;                                 /*   but the compiler doesn't realize this and so warns */

                if (!disp_active) {                         /* if not called by the dispatcher to start a process */
                    if (STATUS_CS (STA) > 1) {              /*   then if an external interrupt was serviced  */
                        cpu_read_memory (stack,             /*     then get the device number (parameter) */
                                         Q + 3 & LA_MASK,
                                         &device);

                        iop_direct_io (device, ioRIN, 0);   /* send a Reset Interrupt I/O order to the device */

                        if (CPX1 & cpx1_IOTIMER)                    /* if an I/O timeout occurred */
                            MICRO_ABORT (trap_SysHalt_IO_Timeout);  /*   then trap for a system halt */

                        if (iop_interrupt_request_set       /* if a hardware interrupt request is pending */
                          && STA & STATUS_I)                /*   and interrupts are enabled */
                            device = iop_poll ();           /*     then poll to see if it can be granted */

                        if (CPX1 & cpx1_EXTINTR) {          /* if a device is ready to interrupt */
                            CPX1 &= ~cpx1_EXTINTR;          /*   then handle it without exiting and restacking */

                            dprintf (cpu_dev, DEB_INSTR, BOV_FORMAT "  external interrupt\n",
                                     PBANK, P - 1 & R_MASK, device);

                            cpu_setup_irq_handler (irq_IXIT, device);   /* set up entry into the interrupt handler */
                            break;                                      /*   with the prior context still on the stack */
                            }
                        }

                    if (delta_q & STMK_D) {             /* if the dispatcher was interrupted */
                        CPX1 |= cpx1_DISPFLAG;          /*   then set the dispatcher-is-active flag */

                        new_q = ics_q;                  /* set the returning Q value */
                        new_sm = ics_q + 2 & R_MASK;    /*   and the returning SM value */

                        if (delta_qi & STMK_D           /* if the dispatcher is scheduled */
                          && disp_counter == 0) {       /*   and enabled */
                            cpu_start_dispatcher ();    /*     then restart it now */
                            break;                      /*       to redispatch */
                            }
                        }
                    }

                if (disp_active                                             /* if the dispatcher is launching a process */
                  || q_is_qi && ((delta_q & STMK_D) == 0                    /*   or a process was interrupted */
                                   || disp_counter != 0)) {                 /*     or the dispatcher is disabled */
                    cpu_read_memory (absolute, Q - 4 & LA_MASK, &stack_db); /*       then read the DB and stack bank */
                    cpu_read_memory (absolute, Q - 5 & LA_MASK, &SBANK);    /*         for the return process */

                    cpu_read_memory (absolute, Q - 7 & LA_MASK, &operand);  /* read the stack-DB-relative data limit */
                    DL = stack_db + operand & R_MASK;                       /*   and restore it */

                    cpu_read_memory (absolute, Q - 8 & LA_MASK, &operand);  /* read the stack-DB-relative stack limit */
                    Z = stack_db + operand & R_MASK;                        /*   and restore it */

                    cpu_write_memory (absolute, Q - 13, D16_UMAX);          /* set the trace flag to a non-zero value */

                    cpu_read_memory (absolute, Q - 6 & LA_MASK, &operand);  /* read the stack-DB-relative stack pointer */
                    Q = stack_db + operand - 2 & R_MASK;                    /*   and restore it */

                    cpu_read_memory (stack, Q, &delta_q);       /* read the relative frame pointer */

                    CPX1 &= ~(cpx1_ICSFLAG | cpx1_DISPFLAG);    /* clear the ICS and dispatcher-is-running flags */

                    new_sm = Q - 4 & R_MASK;                    /* set up the return TOS pointer */
                    new_q = Q - delta_q & R_MASK;               /*   and frame pointer */
                    }

                if (!disp_active                                /* if not launching a new process */
                  && !q_is_qi                                   /*   and returning */
                  && ((delta_q & STMK_D) == 0                   /*     to an interrupted interrupt handler */
                    || (delta_qi & STMK_D) == 0                 /*     or to the interrupted dispatcher */
                    || disp_counter != 0)) {                    /*     or to the dispatcher requesting a disabled redispatch */
                    new_sm = Q - 4 & R_MASK;                    /*       then set up the return TOS pointer */
                    new_q = Q - (delta_q & ~STMK_D) & R_MASK;   /*         and frame pointer */
                    }

                cpu_read_memory (stack, Q + 1 & LA_MASK, &DBANK);   /* restore the data bank */
                cpu_read_memory (stack, Q + 2 & LA_MASK, &DB);      /*   and data base values */

                cpu_exit_procedure (new_q, new_sm, 0);  /* set up the return code segment and stack */
                break;


            case 005:                                   /* these decode as LOCK in hardware */
            case 011:
            case 015:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }

            /* fall through into the LOCK executor */

            case 001:                                   /* LOCK (none; MODE) */
                if (UNIT_CPU_MODEL == UNIT_SERIES_II) { /* if the CPU is a Series II */
                    status = STOP_UNIMPL;               /*   THIS INSTRUCTION IS NOT IMPLEMENTED YET */
                    }

                else                                    /* otherwise the instruction */
                    status = STOP_UNIMPL;               /*   is not implemented on this machine */
                break;


            case 004:                                   /* these decode as PCN in hardware */
            case 006:
            case 010:
            case 012:
            case 014:
            case 016:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }

            /* fall through into the PCN executor */

            case 002:                                       /* PCN (none; STOV, MODE) */
                cpu_push ();                                /* push the stack down */

                if (UNIT_CPU_MODEL == UNIT_SERIES_II)       /* if the CPU is a Series II */
                    RA = PCN_SERIES_II;                     /*   then the CPU number is 1 */

                else if (UNIT_CPU_MODEL == UNIT_SERIES_III) /* if the CPU is a Series III */
                    RA = PCN_SERIES_III;                    /*   then the CPU number is 2 */

                else                                        /* if it's anything else */
                    status = SCPE_IERR;                     /*   then there's a problem! */
                break;


            case 007:                                   /* these decode as UNLK in hardware */
            case 013:
            case 017:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }                                   /* otherwise fall into the UNLK executor */

            /* fall through into the UNLK executor */

            case 003:                                   /* UNLK (none; MODE) */
                if (UNIT_CPU_MODEL == UNIT_SERIES_II) { /* if the CPU is a Series II */
                    status = STOP_UNIMPL;               /*   THIS INSTRUCTION IS NOT IMPLEMENTED YET */
                    }

                else                                    /* otherwise the instruction */
                    status = STOP_UNIMPL;               /*   is not implemented on this machine */
                break;
            }                                           /* all cases are handled */
        break;
    }                                                   /* all cases are handled */

return status;                                          /* return the execution status */
}


/* Execute a firmware extension instruction (subopcode 02, field 01).

   This routine is called to execute the DMUL, DDIV, or firmware extension
   instruction currently in the CIR.  Optional firmware extension instruction
   sets occupy instruction codes 020400-020777.  Two instructions in this range
   are base set instructions: DMUL (020570) and DDIV (020571).  The instruction
   formats are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   1   1   1 | 1   0   0 | x |  DMUL/DDIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1 | ext fp op |  Extended FP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 |   COBOL op    |  COBOL
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1 |  options  |  decimal op   |  Decimal
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   In hardware, optional instructions depend on the presence of the microcode
   that implements them.  All extension instructions initially check the
   "firmware option present" bit in CPX1 before branching to a calculated
   address for the extension microcode.  This bit is set by comparing jumpers
   W1-W8 on the CIR PCA to CIR bits 8-11.  If the "present" bit is clear, the
   firmware takes an Unimplemented Instruction trap.

   A machine with no options has all jumpers installed.  Removing jumpers sets
   the "firmware option present" bit for specific CIR ranges, as follows:

     Jumper  CIR 8-11    CIR Range    Option
     ------  --------  -------------  -----------------------------------------
       W1      0000    020400-020417  Extended Instruction Set (Floating Point)
       W2      0001    020420-020437  32105A APL Instruction Set
       W3      0010    020440-020457
       W4      0011    020460-020477  32234A COBOL II Extended Instruction Set
       W5      0100    020500-020517
       W6      0101    020520-020537
       W7      0110    020540-020557
       --      0111    020560-020577  Base Set (DMUL/DDIV)
       W8      1xxx    020600-020777  Extended Instruction Set (Decimal Arith)

   The range occupied by the base set has no jumper and is hardwired as
   "present".  In simulation, presence is determined by the settings of the CPU
   unit flags.


   Implementation notes:

    1. In simulation, the DDIV instruction must check for 32-bit overflow before
       dividing.  Otherwise, an Integer Overflow Exception may occur on the
       underlying machine instruction, aborting the simulator.
*/

static t_stat firmware_extension (void)
{
int32    dividend, divisor, quotient, remainder;
uint32   operation, suboperation;
t_int64  product;
t_uint64 check;
t_stat   status = SCPE_OK;

operation = FIRMEXTOP (CIR);                            /* get the operation from the instruction */

switch (operation) {                                    /* dispatch the operation */

    case 003:                                           /* COBOL II Extended Instruction Set */
        if (cpu_unit [0].flags & UNIT_CIS)              /* if the firmware is installed */
            status = cpu_cis_op ();                     /*   then call the CIS dispatcher */
        else                                            /* otherwise */
            status = STOP_UNIMPL;                       /*   the instruction range decodes as unimplemented */
        break;


    case 007:                                           /* base set */
        suboperation = FMEXSUBOP (CIR);                 /* get the suboperation from the instruction */

        switch (suboperation) {                         /* dispatch the suboperation */

            case 010:                                   /* DMUL (CCA, O; STUN, ARITH) */
                product =                               /* form a 64-bit product from a 32 x 32 multiplication */
                   INT32 (TO_DWORD (RD, RC)) * INT32 (TO_DWORD (RB, RA));

                check = (t_uint64) product & S32_OVFL_MASK;             /* check the top 33 bits and set overflow */
                SET_OVERFLOW (check != 0 && check != S32_OVFL_MASK);    /*   if they are not all zeros or all ones */

                cpu_pop ();                             /* pop two words */
                cpu_pop ();                             /*   from the stack */

                RB = UPPER_WORD (product);              /* move the 32-bit product */
                RA = LOWER_WORD (product);              /*   to the stack */

                SET_CCA (RB, RA);                       /* set the condition code */
                break;


            case 011:                                   /* DDIV (CCA, O; STUN, ARITH) */
                dividend = INT32 (TO_DWORD (RD, RC));   /* get the 32-bit signed dividend */
                divisor  = INT32 (TO_DWORD (RB, RA));   /*   and the 32-bit signed divisor from the stack */

                if (divisor == 0)                           /* if dividing by zero */
                    MICRO_ABORT (trap_Integer_Zero_Divide); /*   then trap or set the overflow flag */

                if (dividend == (int32) D32_SMIN && divisor == -1) {    /* if the division will overflow */
                    quotient  = dividend;                               /*   then set the quotient */
                    remainder = 0;                                      /*     and remainder explicitly */
                    SET_OVERFLOW (TRUE);                                /*       and trap or set overflow */
                    }

                else {                                  /* otherwise */
                    quotient  = dividend / divisor;     /*   form the 32-bit signed quotient */
                    remainder = dividend % divisor;     /*     and 32-bit signed remainder */
                    }

                RD = UPPER_WORD (quotient);             /* move the 32-bit quotient */
                RC = LOWER_WORD (quotient);             /*   to the stack */

                RB = UPPER_WORD (remainder);            /* move the 32-bit remainder */
                RA = LOWER_WORD (remainder);            /*   to the stack */

                SET_CCA (RD, RC);                       /* set the condition code */
                break;


            default:
                status = STOP_UNIMPL;                   /* the rest of the base set codes are unimplemented */
            }
        break;


    default:
        status = STOP_UNIMPL;                           /* the firmware extension instruction is unimplemented */
    }

return status;                                          /* return the execution status */
}


/* Execute an I/O or control instruction (subopcode 03, field 00).

   This routine is called to execute the I/O or control instruction currently in
   the CIR.  The instruction formats are:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  I/O opcode   |    K field    |  I/O
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  cntl opcode  | 0   0 | cn op |  Control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+



   Implementation notes:

    1. The PAUS instruction suspends instruction execution until an interrupt
       occurs.  It is intended to idle the CPU while suspending instruction
       fetches from memory to allow full-bandwidth access to the selector and
       multiplexer channels.

       If the simulation is stopped while a PAUS instruction is executing, it
       may be resumed after the PAUS by adding the -B switch to the STEP,
       CONTINUE, GO, or RUN command.  This corresponds in hardware to pressing
       the RUN/HALT switch twice.  Without the switch, execution will resume at
       the PAUS instruction.

       The CNTR register is set to the value of the SR register when the
       micromachine pauses.  This allows the SR value to be accessed by the
       diagnostics.  The top-of-stack registers are flushed to main memory when
       this occurs, which clears SR.  Resuming into a PAUS and then stopping the
       simulation again will show CNTR = 0.

    2. The SED instruction decodes bits 12-15, including the reserved bits
       12-14.  The canonical form has the reserved bits set to zero, and in
       hardware SED works correctly only if opcodes 030040 and 030041 are used.
       Opcodes 030042-030057 also decode as SED, but the status register is set
       improperly (the I bit is cleared, bits 12-15 are rotated right twice and
       then ORed into the status register).  If a non-canonical form is used in
       simulation, and the UNDEF stop is active, a simulation stop will occur.
       If the stop is bypassed or not set, then the instruction will execute as
       though the reserved bits were zero.

    3. The CMD instruction is simulated by assuming that the addressed module
       will send a return message to the CPU, causing a module interrupt.  If
       the module is the CPU, then the "return message" is the originating
       message, including whatever MOP was specified.  Memory modules return a
       no-operation MOP in response to a read or read/write ones MOP.  Sending a
       read/write ones MOP to a Series II memory module sets the addressed
       location to 177777 before the read value is returned.

    4. The module interrupt signal is qualified by the I-bit of the status
       register.  This is simulated by setting the cpx1_MODINTR bit in the CMD
       executor if the I-bit is set, by clearing the cpx1_MODINTR bit in the SED
       0 executor, and by setting the bit in the SED 1 executor if the MOD
       register is non-zero (indicating a pending module interrupt that has not
       been serviced).
*/

static t_stat io_control (void)
{
static const uint8 preadjustment [16] = {       /* stack preadjustment, indexed by operation */
    1, 0, 0, 2, 1, 0, 1, 1,                     /*   LST  PAUS SED  **** **** **** XEQ  SIO  */
    0, 1, 0, 1, 1, 2, 0, 0                      /*   RIO  WIO  TIO  CIO  CMD  SST  SIN  HALT */
    };

uint32  operation, address, offset, module;
HP_WORD operand, command, ics_q, delta_qi, disp_counter;
t_stat  status = SCPE_OK;

operation = IOCSUBOP (CIR);                             /* get the suboperation from the instruction */

PREADJUST_SR (preadjustment [operation]);               /* preadjust the TOS registers to the required number */

switch (operation) {                                    /* dispatch the I/O or control operation */

    case 000:                                           /* LST (CCA; STUN, STOV, MODE) */
        offset = IO_K (CIR);                            /* get the system table pointer offset */

        if (offset == 0) {                              /* if the specified offset is zero */
            cpu_read_memory (absolute,                  /*   then offset using the TOS */
                             RA + SGT_POINTER & LA_MASK,
                             &operand);
            cpu_pop ();                                 /* delete the TOS */
            }

        else                                            /* otherwise */
            cpu_read_memory (absolute,                  /*   use the specified offset */
                             offset + SGT_POINTER,      /*     which cannot overflow */
                             &operand);

        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        cpu_push ();                                    /* push the stack down */
        cpu_read_memory (absolute,                      /*   and read the table word onto the TOS */
                         X + operand + SGT_POINTER & LA_MASK,
                         &RA);

        SET_CCA (RA, 0);                                /* set the condition code */
      break;


    case 001:                                           /* PAUS (none; MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        CNTR = SR;                                      /* copy the stack register to the counter */
        cpu_flush ();                                   /*   and flush the TOS registers to memory */

        if (cpu_stop_flags & SS_PAUSE)                  /* if the pause stop is active */
            status = STOP_PAUS;                         /*   then stop the simulation */

        else if (!(cpu_stop_flags & SS_BYPASSED))       /* otherwise if stops are not bypassed */
            cpu_micro_state = paused;                   /*   then pause the micromachine */
        break;                                          /* otherwise bypass the pause */


    case 002:                                           /* SED (none; MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (IO_K (CIR) > 1                              /* if the K field is not 0 or 1 */
          && cpu_stop_flags & SS_UNDEF)                 /*   and the undefined instruction stop is active */
            status = STOP_UNIMPL;                       /*     then stop the simulator */

        else if (CIR & 1) {                             /* otherwise if bit 15 of the instruction is 1 */
            STA |= STATUS_I;                            /*   then enable interrupts */

            if (MOD != 0)                               /* if a module interrupt is pending */
                CPX1 |= cpx1_MODINTR;                   /*   then request it now */
            }

        else {                                          /* otherwise */
            STA &= ~STATUS_I;                           /*   disable interrupts */
            CPX1 &= ~cpx1_MODINTR;                      /*     and clear any indicated module interrupt */
            }
        break;


    case 003:                                           /* XCHD, PSDB, DISP, and PSEB */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        switch (CNTLOP (CIR)) {                         /* dispatch the control operation */

            case 000:                                   /* XCHD (none; STUN, MODE) */
                operand = RA;                           /* exchange the */
                RA = DB;                                /*   RA and */
                DB = operand;                           /*     DB values */

                operand = RB;                           /* exchange the */
                RB = DBANK;                             /*   RB and */
                DBANK = operand & BA_MASK;              /*     DBANK values */

                cpu_base_changed = TRUE;                /* this instruction changed the base registers */
                break;


            case 005:                                   /* these decode as PSDB in hardware */
            case 011:
            case 015:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }

            /* fall through into the PSDB executor */

            case 001:                                   /* PSDB (none; MODE) */
                cpu_read_memory (absolute, ICS_Q,       /* read the ICS stack marker pointer value */
                                 &ics_q);

                cpu_read_memory (absolute, ics_q - 18 & LA_MASK,    /* read the dispatcher counter */
                                 &disp_counter);

                cpu_write_memory (absolute, ics_q - 18 & LA_MASK,   /*  and increment it */
                                  disp_counter + 1 & DV_MASK);
                break;


            case 004:                                   /* these decode as DISP in hardware */
            case 006:
            case 010:
            case 012:
            case 014:
            case 016:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }

            /* fall through into the DISP executor */

            case 002:                                   /* DISP (CCx; MODE, CSTV, TRACE, ABS CST, BNDV) */
                cpu_read_memory (absolute, ICS_Q,       /* read the stack marker initial value */
                                 &ics_q);

                cpu_read_memory (absolute, ics_q - 18 & LA_MASK,    /* read the dispatcher counter */
                                 &disp_counter);

                cpu_write_memory (absolute, ics_q,                  /* set the dispatcher-is-scheduled flag */
                                  STMK_D);

                if (CPX1 & (cpx1_ICSFLAG | cpx1_DISPFLAG)   /* if the dispatcher is currently running */
                  || disp_counter > 0)                      /*   or the dispatcher is inhibited */
                    SET_CCG;                                /*     then set condition code "greater than" */

                else {                                      /* otherwise */
                    SET_CCE;                                /*   set condition code "equal" */
                    cpu_setup_ics_irq (irq_Dispatch, 0);    /*     and set up the ICS */

                    STA = STATUS_M;                         /* enter privileged mode with interrupts disabled */
                    cpu_start_dispatcher ();                /*   and start the dispatcher */
                    }
                break;


            case 007:                                   /* these decode as PSEB in hardware */
            case 013:
            case 017:
                if (cpu_stop_flags & SS_UNDEF) {        /* if the undefined instruction stop is active */
                    status = STOP_UNIMPL;               /*   then stop the simulator */
                    break;
                    }

            /* fall through into the PSEB executor */

            case 003:                                   /* PSEB (CCx; MODE, CSTV, TRACE, ABS CST, BNDV) */
                cpu_read_memory (absolute, ICS_Q,       /* read the stack marker initial value */
                                 &ics_q);

                cpu_read_memory (absolute, ics_q - 18 & LA_MASK,    /* read the dispatcher counter */
                                 &disp_counter);

                cpu_write_memory (absolute, ics_q - 18 & LA_MASK,   /*  and decrement it */
                                  disp_counter - 1 & DV_MASK);

                if (disp_counter == 0)                          /* if the dispatcher is already enabled */
                    MICRO_ABORT (trap_SysHalt_PSEB_Enabled);    /*   then trap for a system halt */

                else if (disp_counter > 1)              /* otherwise if the dispatcher is still inhibited */
                    SET_CCG;                            /*   then set condition code "greater than" */

                else if (CPX1 & cpx1_DISPFLAG) {            /* otherwise if the dispatcher is currently running */
                    cpu_write_memory (absolute, ics_q, 0);  /*   then clear any start dispatcher requests */
                    SET_CCG;                                /*     and set condition code "greater than" */
                    }

                else {                                  /* otherwise the dispatcher is ready to run */
                    cpu_read_memory (absolute, ics_q,   /* read the dispatcher's stack marker */
                                     &delta_qi);

                    if ((delta_qi & STMK_D) == 0        /* if the dispatcher is not scheduled */
                      || (CPX1 & cpx1_ICSFLAG))         /*   or if we're currently executing on the ICS */
                        SET_CCG;                        /*     then set condition code "greater than" */

                    else {                                      /* otherwise */
                        SET_CCE;                                /*   set condition code "equal" */
                        cpu_setup_ics_irq (irq_Dispatch, 0);    /*     and set up the ICS */

                        STA = STATUS_M;                         /* enter privileged mode with interrupts disabled */
                        cpu_start_dispatcher ();                /*   and start the dispatcher */
                        }
                    }
                break;
            }                                           /* all cases are handled */
        break;


    case 004:                                           /* SMSK and SCLK */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        if (CNTLOP (CIR) > 1                            /* if the reserved field is not 0 or 1 */
          && cpu_stop_flags & SS_UNDEF)                 /*   and the undefined instruction stop is active */
            status = STOP_UNIMPL;                       /*     then stop the simulator */

        else if (CNTLOP (CIR) == 0) {                   /* SMSK (CCx; STUN, MODE) */
            iop_direct_io (0, ioSMSK, RA);              /* send the "set mask" I/O order to all interfaces */

            if (CPX1 & cpx1_IOTIMER) {                  /* if an I/O timeout occurred */
                CPX1 &= ~cpx1_IOTIMER;                  /*   then clear the timer */
                SET_CCL;                                /*     and set condition code "greater than" */
                }

            else {                                          /* otherwise the mask was set */
                cpu_write_memory (absolute, INTERRUPT_MASK, /*   so write the interrupt mask to memory */
                                  RA);
                cpu_pop ();                                 /* pop the TOS */
                SET_CCE;                                    /*   and set condition code "equal" */
                }
            }

        else {                                          /* SCLK (none; STUN, MODE) */
            cpu_update_pclk ();                         /* update the process clock counter */
            PCLK = RA;                                  /*   and then set it */

            cpu_pop ();                                 /* delete the TOS */
            }
        break;


    case 005:                                           /* RMSK and RCLK */
        cpu_push ();                                    /* push the stack down */

        if (CNTLOP (CIR) > 1                            /* if the reserved field is not 0 or 1 */
          && cpu_stop_flags & SS_UNDEF)                 /*   and the undefined instruction stop is active */
            status = STOP_UNIMPL;                       /*     then stop the simulator */

        else if (CNTLOP (CIR) == 0)                     /* RMSK (STOV) */
            cpu_read_memory (absolute, INTERRUPT_MASK,  /* read the interrupt mask from memory */
                             &RA);

        else {                                          /* RCLK (none; STOV) */
            cpu_update_pclk ();                         /* update the process clock counter */
            RA = PCLK;                                  /*   and then read it */
            }
        break;


    case 006:                                           /* XEQ (none; BNDV) */
        address = SM + SR - IO_K (CIR) & LA_MASK;       /* get the address of the target instruction */

        if (address >= DB || PRIV) {                    /* if the address is not below DB or the mode is privileged */
            cpu_read_memory (stack, address, &NIR);     /*   then read the word at S - K into the NIR */

            P = P - 1 & R_MASK;                         /* decrement P so the instruction after XEQ is next */
            sim_interval = sim_interval + 1;            /*   but don't count the XEQ against a STEP count */
            }

        else                                            /* otherwise the address is below DB and not privileged */
            MICRO_ABORT (trap_Bounds_Violation);        /*   so trap with a bounds violation */
        break;


    case 007:                                           /* SIO (CCx; STUN, STOV, MODE) */
        operand = srw_io (ioSIO, SIO_OK);               /* send the SIO order to the device */

        if (operand)                                    /* if the start I/O operation succeeded */
            cpu_pop ();                                 /*   then delete the I/O program address */
        break;


    case 010:                                           /* RIO (CCX; STOV, MODE) */
        operand = srw_io (ioRIO, DIO_OK);               /* send the RIO order to the device */

        if (operand) {                                  /* if the read I/O operation succeeded */
            cpu_push ();                                /*   then push the stack down */
            RA = LOWER_WORD (operand);                  /*     and save the RIO response on the TOS */
            }
        break;


    case 011:                                           /* WIO (CCX; STUN, STOV, MODE) */
        operand = srw_io (ioWIO, DIO_OK);               /* send the WIO order to the device */

        if (operand)                                    /* if the write I/O operation succeeded */
            cpu_pop ();                                 /*   then delete the write value */
        break;


    case 012:                                           /* TIO (CCx; STOV, MODE) */
        operand = tcs_io (ioTIO);                       /* send the TIO order to the device */

        if (operand) {                                  /* if the test I/O operation succeeded */
            cpu_push ();                                /*   then push the stack down */
            RA = LOWER_WORD (operand);                  /*     and save the I/O response on the TOS */
            }
        break;


    case 013:                                           /* CIO (CCx; STUN, MODE) */
        operand = tcs_io (ioCIO);                       /* send the CIO order to the device */

        if (operand)                                    /* if the control operation succeeded */
            cpu_pop ();                                 /*   then delete the control value */
        break;


    case 014:                                           /* CMD (none; STUN, MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        address = SM + SR - IO_K (CIR) & LA_MASK;       /* get the location of the command word */
        cpu_read_memory (stack, address, &command);     /*   and read it from the stack or TOS registers */

        module = CMD_TO (command);                      /* get the addressed (TO) module number */

        if (module == MODULE_PORT_CNTLR                 /* if the selector channel port controller */
          || module >= MODULE_UNDEFINED)                /*   or an undefined module is addressed */
            CPX1 |= cpx1_CPUTIMER;                      /*     then a module timeout occurs */

        else if (module == MODULE_CPU)                  /* otherwise if the CPU is addressing itself */
            MOD = MOD_CPU_1                             /*   then set the MOD register */
                    | TO_MOD_FROM (module)              /*     FROM field to the TO address */
                    | TO_MOD_MOP (CMD_MOP (command));   /*       and include the MOP field value */

        else if (UNIT_CPU_MODEL == UNIT_SERIES_II)      /* otherwise if a Series II memory module is addressed */
            if (module >= MODULE_MEMORY_UPPER           /*   then if the upper module is addressed */
              && MEMSIZE < 128 * 1024)                  /*     but it's not present */
                CPX1 |= cpx1_CPUTIMER;                  /*       then it will not respond */

            else {                                                  /* otherwise the module address is valid */
                if (CMD_MOP (command) == MOP_READ_WRITE_ONES) {     /* if the operation is read/write ones */
                    address = TO_PA (module, RA);                   /*   then get the bank and address */
                    cpu_write_memory (absolute, address, D16_UMAX); /*     and set the addressed word to all one bits */
                    }

                MOD = MOD_CPU_1                         /* set the MOD register */
                        | TO_MOD_FROM (module)          /*   FROM field to the TO address */
                        | TO_MOD_MOP (MOP_NOP);         /*     and the module operation to NOP */
                }

        else if (UNIT_CPU_MODEL == UNIT_SERIES_III)     /* otherwise if a Series III memory module is addressed */
            if (module >= MODULE_MEMORY_UPPER           /*   then if the upper module is addressed */
              && MEMSIZE < 512 * 1024)                  /*     but it's not present */
                CPX1 |= cpx1_CPUTIMER;                  /*       then it will not respond */

            else                                        /* otherwise the module address is valid */
                MOD = MOD_CPU_1                         /*   so set the MOD register */
                        | TO_MOD_FROM (module)          /*     FROM field to the TO address */
                        | TO_MOD_MOP (MOP_NOP);         /*       and the module operation to NOP */

        if (MOD != 0 && STA & STATUS_I)                 /* if a module interrupt is indicated and enabled */
            CPX1 |= cpx1_MODINTR;                       /*   then request it */

        cpu_pop ();                                     /* delete the TOS */
        break;


    case 015:                                           /* SST (none; STUN, MODE) */
        offset = IO_K (CIR);                            /* get the system table pointer offset */

        if (offset == 0) {                              /* if the specified offset is zero */
            cpu_read_memory (absolute,                  /*   then offset using the TOS */
                             RA + SGT_POINTER & LA_MASK,
                             &operand);
            cpu_pop ();                                 /* delete the TOS */
            }

        else                                            /* otherwise */
            cpu_read_memory (absolute,                  /*   use the specified offset */
                             offset + SGT_POINTER,      /*     which cannot overflow */
                             &operand);

        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        cpu_write_memory (absolute,                     /* write the TOS value into the table */
                          X + operand + SGT_POINTER & LA_MASK,
                          RA);

        cpu_pop ();                                     /* delete the TOS */
        break;


    case 016:                                           /* SIN (CCx; MODE) */
        tcs_io (ioSIN);                                 /* send the SIN order to the device */
        break;


    case 017:                                           /* HALT (none; MODE) */
        if (NPRV)                                       /* if the mode is not privileged */
            MICRO_ABORT (trap_Privilege_Violation);     /*   then abort with a privilege violation */

        CNTR = SR;                                      /* copy the stack register to the counter */
        cpu_flush ();                                   /*   and flush the TOS registers to memory */

        CPX2 &= ~cpx2_RUN;                              /* clear the run flip-flop */
        status = STOP_HALT;                             /*   and stop the simulator */
        break;
    }                                                   /* all cases are handled */

return status;                                          /* return the execution status */
}
