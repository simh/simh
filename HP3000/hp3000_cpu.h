/* hp3000_cpu.h: HP 3000 CPU declarations

   Copyright (c) 2016, J. David Bryan

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

   12-Sep-16    JDB     Added the PCN_SERIES_II and PCN_SERIES_III constants
   02-Sep-16    JDB     Added the POWER_STATE enumeration type, the UNIT_PFARS
                        flag, and the "cpu_power_state" external declaration
   24-Aug-16    JDB     Fixed the UNIT_CPU_MODEL test macro
   23-Aug-16    JDB     Added the MOD (module control) register
   12-Jul-16    JDB     Renamed "loading" EXEC_STATE to "waiting"
   21-Mar-16    JDB     Changed cpu_ccb_table type from uint16 to HP_WORD
   14-Feb-16    JDB     First release version
   11-Dec-12    JDB     Created


   This file provides the declarations for interoperation between the CPU and
   its supporting modules.  It provides the symbols that allow direct
   manipulation of the CPU registers and determination of currently installed
   features.
*/



#include <setjmp.h>



/* Architectural constants.

   The type used to represent a main memory word value is defined.  An array of
   this type is used to simulate the CPU main memory.


   Implementation notes:

    1. The MEMORY_WORD type is a 16-bit unsigned type, corresponding with the
       16-bit main memory in the HP 3000.  Unlike the general data type, which
       is a 32-bit type for speed, main memory does not benefit from the faster
       32-bit execution on IA-32 processors, as only one instruction in the
       cpu_read_memory and cpu_write_memory routines has an operand override
       that invokes the slower instruction fetch path.  There is a negligible
       difference in the Memory Pattern Test diagnostic execution speeds for the
       uint32 vs. uint16 definition, whereas the VM requirements are doubled for
       the former.
*/

typedef uint16              MEMORY_WORD;        /* HP 16-bit memory word representation */


/* Supported breakpoint switches */

#define BP_EXEC             (SWMASK ('E'))      /* an execution breakpoint */
#define BP_SUPPORTED        (BP_EXEC)           /* the list of supported breakpoint types */


/* Unit flags and accessors */

#define UNIT_MODEL_SHIFT    (UNIT_V_UF + 0)     /* the CPU model (1 bit) */
#define UNIT_EIS_SHIFT      (UNIT_V_UF + 1)     /* the Extended Instruction Set firmware option */
#define UNIT_CALTIME_SHIFT  (UNIT_V_UF + 2)     /* the process clock timing mode */
#define UNIT_PFARS_SHIFT    (UNIT_V_UF + 3)     /* the power-fail auto-restart mode */

#define UNIT_MODEL_MASK     0000001u            /* model ID mask */

#define UNIT_MODEL          (UNIT_MODEL_MASK << UNIT_MODEL_SHIFT)

#define UNIT_SERIES_III     (0u << UNIT_MODEL_SHIFT)    /* the CPU is a Series III */
#define UNIT_SERIES_II      (1u << UNIT_MODEL_SHIFT)    /* the CPU is a Series II */
#define UNIT_EIS            (1u << UNIT_EIS_SHIFT)      /* the Extended Instruction Set is installed */
#define UNIT_CALTIME        (1u << UNIT_CALTIME_SHIFT)  /* the process clock is calibrated to wall time */
#define UNIT_PFARS          (1u << UNIT_PFARS_SHIFT)    /* the system will auto-restart after a power failure */

#define UNIT_CPU_MODEL      (cpu_unit [0].flags & UNIT_MODEL)

#define CPU_MODEL(f)        ((f) >> UNIT_MODEL_SHIFT & UNIT_MODEL_MASK)

#define MEMSIZE             (cpu_unit [0].capac)        /* the current memory size in 16-bit words */


/* CPU debug flags */

#define DEB_MDATA           (1u << 0)           /* trace memory data accesses */
#define DEB_INSTR           (1u << 1)           /* trace instruction execution */
#define DEB_FETCH           (1u << 2)           /* trace instruction fetches */
#define DEB_REG             (1u << 3)           /* trace register values */
#define DEB_PSERV           (1u << 4)           /* trace PCLK service events */

#define BOV_FORMAT          "%02o.%06o  %06o  " /* bank-offset-value trace format string */


/* CPU stop flags */

#define SS_LOOP             (1u << 0)           /* stop on infinite loop */
#define SS_PAUSE            (1u << 1)           /* stop on PAUS instruction */
#define SS_UNDEF            (1u << 2)           /* stop on undefined instruction */
#define SS_UNIMPL           (1u << 3)           /* stop on unimplemented instruction */
#define SS_BYPASSED         (1u << 31)          /* stops are bypassed for this instruction */


/* System power state.

   The HP 3000 power supply uses two signals to indicate its state: PON (power
   on) and PFW (power-fail warning).  PON is asserted when the DC power levels
   are within their operating ranges.  PFW is asserted when AC power is lost.
   When a power failure occurs, PFW will be asserted at least three milliseconds
   before PON is denied.  When power is restored, PFW denies immediately, but
   PON does not assert until the DC output voltages have stabilized, and the
   machine is ready to resume execution.

   In simulation, the four states of these two signals are modeled with
   enumeration constants, as follows:

     PON  PFW  State            Simulator Action
     ---  ---  ---------------  ----------------------------
      1    0   power on         executing normally
      1    1   power failing    executing with cpx1_PFINTR
      0    1   power off        will not execute
      0    0   power returning  executing with trap_Power_On
*/

typedef enum {
    power_on,
    power_failing,
    power_off,
    power_returning
    } POWER_STATE;


/* Micromachine execution state */

typedef enum {
    running,                                    /* the micromachine is running */
    paused,                                     /* a PAUS instruction has been executed */
    waiting,                                    /* a cold load or dump is in progress */
    halted                                      /* a programmed or front panel HALT has been executed */
    } EXEC_STATE;


/* Memory access classifications.

   The access classification determines which bank register is used with the
   supplied offset to access memory, and whether or not the access is bounds
   checked.


   Implementation notes:

    1. The "_iop" and "_sel" classifications are identical.  The only difference
       is which device's trace flag is checked to print debugging information.
       All of the other classifications check the CPU's trace flags.
*/

typedef enum {
    absolute_iop,                               /* absolute bank, IOP request */
    dma_iop,                                    /* DMA channel bank, IOP request */
    absolute_sel,                               /* absolute bank, selector channel request */
    dma_sel,                                    /* DMA channel bank, selector channel request */
    absolute,                                   /* absolute bank */
    absolute_checked,                           /* absolute bank, bounds checked */
    fetch,                                      /* program bank, instruction fetch */
    fetch_checked,                              /* program bank, instruction fetch, bounds checked */
    program,                                    /* program bank, data access */
    program_checked,                            /* program bank, data access, bounds checked */
    data,                                       /* data bank, data access */
    data_checked,                               /* data bank, data access, bounds checked */
    stack,                                      /* stack bank, data access */
    stack_checked                               /* stack bank, data access, bounds checked */
    } ACCESS_CLASS;


/* CPX register flags.

   The CPX1 register contains flags that designate the run-time interrupts.  The
   CPX2 register contains flags for halt-time interrupts.


   Implementation notes:

    1. These are implemented as enumeration types to allow the "gdb" debugger to
       display the CPX register values as bit sets.
*/

typedef enum {
    cpx1_INTOVFL  = 0100000u,                   /* integer overflow */
    cpx1_BNDVIOL  = 0040000u,                   /* bounds violation */
    cpx1_ILLADDR  = 0020000u,                   /* illegal address */
    cpx1_CPUTIMER = 0010000u,                   /* CPU timer */
    cpx1_SYSPAR   = 0004000u,                   /* system parity error */
    cpx1_ADDRPAR  = 0002000u,                   /* address parity error */
    cpx1_DATAPAR  = 0001000u,                   /* data parity error */
    cpx1_MODINTR  = 0000400u,                   /* module interrupt */
    cpx1_EXTINTR  = 0000200u,                   /* external interrupt */
    cpx1_PFINTR   = 0000100u,                   /* power fail interrupt */
/*  cpx1_UNUSED   = 0000040u,                      unused, always 0 */
    cpx1_ICSFLAG  = 0000020u,                   /* ICS flag */
    cpx1_DISPFLAG = 0000010u,                   /* dispatcher-is-active flag */
    cpx1_EMULATOR = 0000004u,                   /* emulator-in-use flag */
    cpx1_IOTIMER  = 0000002u,                   /* I/O timeout */
    cpx1_OPTION   = 0000001u                    /* option present */
    } CPX1FLAG;

#define CPX1_IRQ_SET        (cpx1_INTOVFL | cpx1_BNDVIOL  | \
                             cpx1_ILLADDR | cpx1_CPUTIMER | \
                             cpx1_SYSPAR  | cpx1_ADDRPAR  | \
                             cpx1_DATAPAR | cpx1_MODINTR  | \
                             cpx1_EXTINTR | cpx1_PFINTR)        /* the set of CPX1 interrupt requests */

typedef enum {
    cpx2_RUNSWCH  = 0100000u,                   /* RUN switch */
    cpx2_DUMPSWCH = 0040000u,                   /* DUMP switch */
    cpx2_LOADSWCH = 0020000u,                   /* LOAD switch */
    cpx2_LOADREG  = 0010000u,                   /* load register */
    cpx2_LOADADDR = 0004000u,                   /* load address */
    cpx2_LOADMEM  = 0002000u,                   /* load memory */
    cpx2_DISPMEM  = 0001000u,                   /* display memory */
    cpx2_SNGLINST = 0000400u,                   /* single instruction */
    cpx2_EXECSWCH = 0000200u,                   /* EXECUTE switch */
    cpx2_INCRADDR = 0000100u,                   /* increment address */
    cpx2_DECRADDR = 0000040u,                   /* decrement address */
/*  cpx2_UNUSED   = 0000020u,                      unused, always 0 */
/*  cpx2_UNUSED   = 0000010u,                      unused, always 0 */
    cpx2_INHPFARS = 0000004u,                   /* inhibit power-fail auto-restart */
    cpx2_SYSHALT  = 0000002u,                   /* system halt */
    cpx2_RUN      = 0000001u                    /* run flip-flop */
    } CPX2FLAG;

#define CPX2_IRQ_SET        (cpx2_RUNSWCH  | cpx2_DUMPSWCH | \
                             cpx2_LOADSWCH | cpx2_LOADREG  | \
                             cpx2_LOADADDR | cpx2_LOADMEM  | \
                             cpx2_DISPMEM  | cpx2_SNGLINST | \
                             cpx2_EXECSWCH)                     /* the set of CPX2 interrupt requests */


/* Interrupt classifications.

   Interrupts are generated by setting CPX1 bits, by microcode aborts (traps),
   or by the DISP or IXIT instructions to run the OS dispatcher or in response
   to an external interrupt.  Each interrupt source is serviced by a procedure
   in the Segment Transfer Table of segment 1, and the classification values are
   chosen to match the STT numbers.


   Implementation notes:

    1. The STT numbers are relevant only for interrupts that come from setting
       the CPX1 bits (except for bit 8).  The enumeration values for external,
       trap, DISP, and IXIT interrupts are not significant.
*/

typedef enum {
/*  identifier                STT        Source  Description               */
/*  -----------------------   ---        ------  ------------------------- */
    irq_Integer_Overflow    = 000,    /* CPX1.0  Integer Overflow          */
    irq_Bounds_Violation    = 001,    /* CPX1.1  Bounds Violation          */
    irq_Illegal_Address     = 002,    /* CPX1.2  Illegal Memory Address    */
    irq_Timeout             = 003,    /* CPX1.3  Non-Responding Module     */
    irq_System_Parity       = 004,    /* CPX1.4  System Parity Error       */
    irq_Address_Parity      = 005,    /* CPX1.5  Address Parity Error      */
    irq_Data_Parity         = 006,    /* CPX1.6  Data Parity Error         */
    irq_Module              = 007,    /* CPX1.7  Module Interrupt          */
    irq_External            = 010,    /* CPX1.8  External Interrupt        */
    irq_Power_Fail          = 011,    /* CPX1.9  Power Fail Interrupt      */
    irq_Trap                = 012,    /* uABORT  System or user trap       */
    irq_Dispatch            = 013,    /* DISP    Run the dispatcher        */
    irq_IXIT                = 014     /* IXIT    External interrupt        */
    } IRQ_CLASS;


/* Trap classifications.

   Except for the power-on trap, all traps result from microcode aborts.  In
   hardware, these result in microcode jumps to the appropriate trap handlers.
   In simulation, a MICRO_ABORT executes a "longjmp" to the trap handler just
   before the main instruction loop.  As with interrupts, each trap is serviced
   by a procedure in the Segment Transfer Table of segment 1, and the
   classification values are chosen to match the STT numbers.

   Traps are invoked by the MICRO_ABORT macro, which takes as its parameter a
   trap classification value.  Some traps require an additional parameter and
   must be invoked by the MICRO_ABORTP macro, which takes a trap classification
   value and a trap-specific value as parameters.

   Accessors are provided to separate the TRAP_CLASS and the parameter from the
   combined longjmp value.


   Implementation notes:

    1. Trap classifications must be > 0 for longjmp compatibility.

    2. A System Halt trap does not call an OS procedure but instead stops the
       simulator.  The parameter number indicates the reason for the halt.

    3. The User trap is subdivided into traps for a number of arithmetic
       conditions.  These are indicated by their corresponding parameter numbers
       in the upper words of the longjmp values.
*/

typedef enum {
/*  identifier                 STT        Source  Description               */
/*  ------------------------   ---        ------  ------------------------- */
    trap_None                = 000,    /*   --    (none)                    */
    trap_Bounds_Violation    = 001,    /* ucode   Bounds Violation          */
    trap_Unimplemented       = 020,    /* ucode   Unimplemented Instruction */
    trap_STT_Violation       = 021,    /* ucode   STT Violation             */
    trap_CST_Violation       = 022,    /* ucode   CST Violation             */
    trap_DST_Violation       = 023,    /* ucode   DST Violation             */
    trap_Stack_Underflow     = 024,    /* ucode   Stack Underflow           */
    trap_Privilege_Violation = 025,    /* ucode   Privileged Mode Violation */
    trap_Stack_Overflow      = 030,    /* ucode   Stack Overflow            */
    trap_User                = 031,    /* ucode   User Trap                 */
    trap_CS_Absent           = 037,    /* ucode   Absent Code Segment       */
    trap_Trace               = 040,    /* ucode   Trace                     */
    trap_Uncallable          = 041,    /* ucode   STT Entry Uncallable      */
    trap_DS_Absent           = 042,    /* ucode   Absent Data Segment       */
    trap_Power_On            = 043,    /*  hdwe   Power On                  */
    trap_Cold_Load           = 044,    /* ucode   Cold Load                 */
    trap_System_Halt         = 045     /* ucode   System Halt               */
    } TRAP_CLASS;

#define trap_Integer_Overflow       TO_DWORD (001, trap_User)
#define trap_Float_Overflow         TO_DWORD (002, trap_User)
#define trap_Float_Underflow        TO_DWORD (003, trap_User)
#define trap_Integer_Zero_Divide    TO_DWORD (004, trap_User)
#define trap_Float_Zero_Divide      TO_DWORD (005, trap_User)
#define trap_Ext_Float_Overflow     TO_DWORD (010, trap_User)
#define trap_Ext_Float_Underflow    TO_DWORD (011, trap_User)
#define trap_Ext_Float_Zero_Divide  TO_DWORD (012, trap_User)
#define trap_Decimal_Overflow       TO_DWORD (013, trap_User)
#define trap_Invalid_ASCII_Digit    TO_DWORD (014, trap_User)
#define trap_Invalid_Decimal_Digit  TO_DWORD (015, trap_User)
#define trap_Invalid_Word_Count     TO_DWORD (016, trap_User)
#define trap_Word_Count_Overflow    TO_DWORD (017, trap_User)
#define trap_Decimal_Zero_Divide    TO_DWORD (020, trap_User)

#define trap_SysHalt_STTV_1         TO_DWORD ( 1, trap_System_Halt)
#define trap_SysHalt_Absent_ICS     TO_DWORD ( 2, trap_System_Halt)
#define trap_SysHalt_Absent_1       TO_DWORD ( 3, trap_System_Halt)
#define trap_SysHalt_Overflow_ICS   TO_DWORD ( 4, trap_System_Halt)
#define trap_SysHalt_IO_Timeout     TO_DWORD ( 6, trap_System_Halt)
#define trap_SysHalt_PSEB_Enabled   TO_DWORD ( 9, trap_System_Halt)
#define trap_SysHalt_CSTV_1         TO_DWORD (13, trap_System_Halt)
#define trap_SysHalt_LOCK_EI        TO_DWORD (23, trap_System_Halt)
#define trap_SysHalt_Trace_1        TO_DWORD (33, trap_System_Halt)

#define PARAM(i)            (uint32)     UPPER_WORD (i)
#define TRAP(i)             (TRAP_CLASS) LOWER_WORD (i)

#define MICRO_ABORT(t)      longjmp (cpu_save_env, (t))
#define MICRO_ABORTP(t,p)   longjmp (cpu_save_env, TO_DWORD ((p),(t)))


/* Central Data Bus module definitions */

#define MODULE_MEMORY_LOWER     0               /* lower memory MCL address */
#define MODULE_MEMORY_UPPER     2               /* upper memory MCL address */
#define MODULE_MEMORY           3               /* upper bound of MCL addresses */
#define MODULE_PORT_CNTLR       4               /* selector channel port controller address */
#define MODULE_CPU              5               /* CPU MCU address */
#define MODULE_UNDEFINED        6               /* addresses 6-7 are unused */

#define MOP_NOP                 0               /* module operation 00 = no operation */
#define MOP_WRITE               1               /* module operation 01 = write */
#define MOP_READ                2               /* module operation 10 = read */
#define MOP_READ_WRITE_ONES     3               /* module operation 11 = read/write ones */


/* Module control register accessors.

   The module control register, MOD, has this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0 |  MOP  | 0 |   FROM    | 0   0   0   0 | B | A | 0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     MOP  = module operation
     FROM = source module address
     B    = this CPU is CPU #2
     A    = this CPU is CPU #1
*/

#define MOD_MOP_MASK        0030000u            /* MOD register MOP field mask */
#define MOD_FROM_MASK       0003400u            /* MOD register FROM field mask */
#define MOD_CPU_2           0000010u            /* CPU number 2 MCU */
#define MOD_CPU_1           0000004u            /* CPU number 1 MCU */

#define MOD_MOP_SHIFT       12                  /* MOD register MOP field alignment shift */
#define MOD_FROM_SHIFT      8                   /* MOD register FROM field alignment shift */

#define TO_MOD_MOP(v)       ((v) << MOD_MOP_SHIFT  & MOD_MOP_MASK)
#define TO_MOD_FROM(v)      ((v) << MOD_FROM_SHIFT & MOD_FROM_MASK)


/* Status register accessors.

   The CPU status register, STA, has this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | I | T | R | O | C | ccode |  current code segment number  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = user mode/protected mode (0/1)
     I = external interrupts are enabled
     T = user traps are enabled
     R = right-hand stack operation executes next
     O = arithmetic overflow has occurred
     C = arithmetic carry has occurred

   Condition Code:

     00 = CCL (less than)
     01 = CCE (equal to)
     10 = CCG (greater than)
     11 = invalid
*/

#define STATUS_M            0100000u            /* mode flag */
#define STATUS_I            0040000u            /* interrupt flag */
#define STATUS_T            0020000u            /* trap flag */
#define STATUS_R            0010000u            /* right-hand stack op flag */
#define STATUS_O            0004000u            /* overflow flag */
#define STATUS_C            0002000u            /* carry flag */

#define STATUS_CCG          0000000u            /* condition code greater than */
#define STATUS_CCL          0000400u            /* condition code less than */
#define STATUS_CCE          0001000u            /* condition code equal to */

#define STATUS_CC_MASK      0001400u            /* condition code mask */
#define STATUS_CC_SHIFT     8                   /* condition code alignment */

#define STATUS_CS_MASK      0000377u            /* code segment mask */
#define STATUS_CS_WIDTH     8                   /* code segment mask width */

#define STATUS_OVTRAP       (STATUS_T | STATUS_O)
#define STATUS_NPRV         (STATUS_T | STATUS_O | STATUS_C | STATUS_CC_MASK)

#define TO_CCN(s)           (((s) & STATUS_CC_MASK) >> STATUS_CC_SHIFT)


/* Status register mode tests */

#define PRIV                (STA & STATUS_M)            /* current mode is privileged */
#define NPRV                (!PRIV)                     /* current mode is non-privileged */


/* Condition code flags.

   Several instructions define "condition code flags" that specify condition
   code tests to be performed.  These flags are ANDed with the condition code
   from the status register to establish whether the test passes.  CCE and CCL
   are encoded in the status register by bits 6 and 7, respectively, but CCG is
   encoded as 00, causing a direct AND to fail.  This is resolved in microcode
   by the "CC" S-bus micro-order, which copies bits 6 and 7 to bits 8 and 9 of
   the S-bus register and also sets bit 7 if bits 8 and 9 are zero (CCG).  The
   result is a single bit set for each condition code that may be ANDed with the
   CCF field of the instruction.

   The MVBW (Move Bytes While) instruction moves bytes from the source to the
   destination while the bytes are alphabetic, numeric, or either, depending on
   the A and N bits in the instruction.  The CCB classification table is used to
   determine the type of each byte moved.  If both A and N bits are set, then a
   table entry of either CCG or CCE will succeed.  Matching via a single AND
   operation is possible only if the encoding of all three conditions is
   disjoint.

   We implement this scheme without the two-bit right-shift, so that ANDing a
   CCF with STATUS_CC_MASK will produce the correctly aligned status register
   value.  The TO_CCF macro converts the current condition code in the status
   register to a condition code flag.
*/

#define CFL                 0000400u            /* condition code flag less than */
#define CFE                 0001000u            /* condition code flag equal to */
#define CFG                 0002000u            /* condition code flag greater than */

#define TO_CCF(s)           ((s) & STATUS_CC_MASK ? (s) & STATUS_CC_MASK : CFG)


/* Condition code patterns.

   Machine instructions typically set the condition code field of the status
   register to reflect the values of their operands, which may be on the top of
   the stack, in the X register, or in memory.  Each instruction that affects
   the condition code field commonly uses one of the following patterns to set
   the field:

     CCA (arithmetic) sets CC to:
       - CCG (00) if the operand is > 0
       - CCL (01) if the operand is < 0
       - CCE (10) if the operand is = 0

     CCB (byte) sets CC to:
       - CCG (00) if the operand is numeric (byte value is 060-071)
       - CCL (01) if the operand is any other character
       - CCE (10) if the operand is alphabetic (0101-0132 or 0141-0172)

     CCC (comparison) sets CC to:
       - CCG (00) if operand 1 is > operand 2
       - CCL (01) if operand 1 is < operand 2
       - CCE (10) if operand 1 is = operand 2

     CCD (direct I/O) sets CC to:
       - CCG (00) if the device is not ready
       - CCL (01) if the device controller is not responding
       - CCE (10) if the device is ready and controller is responding

   The operand(s) may be a byte, word, double word, triple word, or quadruple
   word, and may be signed (integer) or unsigned (logical).

   Statement macros are provided to set the condition code explicitly, as well
   as to set the code using patterns CCA, CCB, or CCC.  The CCA macro takes two
   parameters: an upper word and a lower word.  The CCB macro takes a single
   byte parameter.  The CCC macro takes four parameters: an upper and lower word
   for each of the two operands.

   No macro is provided for pattern CCD, as that pattern reflects the
   operational status of the target device and interface, rather than the
   condition of an operand.  The I/O instructions use the explicit macros to set
   the codes for pattern D.
*/

#define SET_CCG             STA = STA & ~STATUS_CC_MASK | STATUS_CCG
#define SET_CCL             STA = STA & ~STATUS_CC_MASK | STATUS_CCL
#define SET_CCE             STA = STA & ~STATUS_CC_MASK | STATUS_CCE


/* Condition code pattern CCA (arithmetic).

   Semantics:

     if operand > 0
       then CCG
     else if operand < 0
       then CCL
     else
       CCE

   Macro:

     SET_CCA (upper, lower)

   Implementation:

     if upper = 0 or lower = 0
       then CCE
     else if upper.sign = 1
       then CCL
     else CCG

   Usage:

     SET_CCA (RA, 0)            -- a 16-bit integer value
     SET_CCA (0, RA)            -- a 16-bit logical value
     SET_CCA (RB, RA)           -- a 32-bit integer value
     SET_CCA (0, RB | RA)       -- a 32-bit logical value
     SET_CCA (RC, RB | RA)      -- a 48-bit integer value
     SET_CCA (RD, RC | RB | RA) -- a 64-bit integer value


   Implementation notes:

    1. When either "upper" or "lower" is 0, the corresponding test is optimized
       away.
*/

#define SET_CCA(u,l)        STA = STA & ~STATUS_CC_MASK | \
                              (((u) | (l)) == 0 \
                                ? STATUS_CCE \
                                : (u) & D16_SIGN \
                                    ? STATUS_CCL \
                                    : STATUS_CCG)


/* Condition code pattern CCB (byte).

   Semantics:

     if operand in 060 .. 071
       then CCG
     else if operand in 0101 .. 0132 or operand in 0141 .. 0172
       then CCE
     else
       CCL

   Macro:

     SET_CCB (byte)

   Implementation:

     CCx = ccb_lookup_table [byte]

   Usage:

     SET_CCB (RA)


   Implementation notes:

    1. The byte parameter is not masked before being used as an array index, so
       the caller must ensure that the value is between 0 and 255.

    2. The table value must be masked if it is to be stored in the status
       register because CCG is returned as 100 (base 2) to ensure that all
       values are disjoint for the MVBW and Bcc instructions.
*/

#define SET_CCB(b)          STA = STA & ~STATUS_CC_MASK | \
                              cpu_ccb_table [(b)] & STATUS_CC_MASK


/* Condition code pattern CCC (conditional).

   Semantics:

     if operand_1 > operand_2
       then CCG
     else if operand_1 < operand_2
       then CCL
     else
       CCE

   Macro:

     SET_CCC (upper_1, lower_1, upper_2, lower_2)

   Implementation:

     if upper_1 = upper_2
       then
         if lower_1 = lower_2
           then CCE
         else if lower_1 < lower_2
           then CCL
         else CCG
     else if |upper_1| < |upper_2|
       then CCL
     else CCG

   Usage:

     SET_CCC (RA,  0, RB,  0) -- 16-bit integer comparison
     SET_CCC ( 0, RA,  0, RB) -- 16-bit logical comparison
     SET_CCC (RB, RA, RD, RC) -- 32-bit integer comparison


   Implementation notes:

    1. When any of the parameters are 0, the corresponding tests are optimized
       away.

    2. Complementing the signs of the upper words allows an unsigned comparison
       to substitute for a signed comparison.
*/

#define SET_CCC(u1,l1,u2,l2)  STA = STA & ~STATUS_CC_MASK | \
                                ((u1) == (u2) \
                                  ? ((l1) == (l2) \
                                      ? STATUS_CCE \
                                      : ((l1) < (l2) \
                                          ? STATUS_CCL \
                                          : STATUS_CCG)) \
                                  : (((u1) ^ D16_SIGN) < ((u2) ^ D16_SIGN) \
                                      ? STATUS_CCL \
                                      : STATUS_CCG))

/* Set carry and overflow.

   These macros are used by arithmetic operations to set the carry and overflow
   bits in the status register.  In addition to setting the overflow bit, if
   user traps are enabled, the Integer Overflow bit in the CPX1 register is set,
   which will cause an interrupt
*/

#define SET_CARRY(b)        STA = ((b) ? STA | STATUS_C : STA & ~STATUS_C)

#define SET_OVERFLOW(b)     if (b) { \
                                STA |= STATUS_O; \
                                if (STA & STATUS_T) \
                                    CPX1 |= cpx1_INTOVFL; \
                                } \
                            else \
                                STA &= ~STATUS_O

/* SR preadjust.

   The PREADJUST_SR macro is called to ensure that the correct number of TOS
   registers are valid prior to instruction execution.
*/

#define PREADJUST_SR(n)     if (SR < (n)) \
                                cpu_adjust_sr (n)


/* Machine instruction bit-field accessors */

#define BITS_0_3_MASK       0170000u            /* bits 0-3 mask */
#define BITS_0_3_SHIFT      12                  /* bits 0-3 alignment shift */

#define BITS_0_3(v)         (((v) & BITS_0_3_MASK) >> BITS_0_3_SHIFT)

#define BITS_4_5_MASK       0006000u            /* bits 4-5 mask */
#define BITS_4_5_SHIFT      10                  /* bits 4-5 alignment shift */

#define BITS_4_5(v)         (((v) & BITS_4_5_MASK) >> BITS_4_5_SHIFT)

#define BITS_4_7_MASK       0007400u            /* bits 4-7 mask */
#define BITS_4_7_SHIFT      8                   /* bits 4-7 alignment shift */

#define BITS_4_7(v)         (((v) & BITS_4_7_MASK) >> BITS_4_7_SHIFT)

#define BITS_4_9_MASK       0007700u            /* bits 4-9 mask */
#define BITS_4_9_SHIFT      6                   /* bits 4-9 alignment shift */

#define BITS_4_9(v)         (((v) & BITS_4_9_MASK) >> BITS_4_9_SHIFT)

#define BITS_5_9_MASK       0003700u            /* bits 5-9 mask */
#define BITS_5_9_SHIFT      6                   /* bits 5-9 alignment shift */

#define BITS_5_9(v)         (((v) & BITS_5_9_MASK) >> BITS_5_9_SHIFT)

#define BITS_8_11_MASK      0000360u            /* bits 8-11 mask */
#define BITS_8_11_SHIFT     4                   /* bits 8-11 alignment shift */

#define BITS_8_11(v)        (((v) & BITS_8_11_MASK) >> BITS_8_11_SHIFT)

#define BITS_8_12_MASK      0000370u            /* bits 8-12 mask */
#define BITS_8_12_SHIFT     3                   /* bits 8-12 alignment shift */

#define BITS_8_12(v)        (((v) & BITS_8_12_MASK) >> BITS_8_12_SHIFT)

#define BITS_10_15_MASK     0000077u            /* bits 10-15 mask */
#define BITS_10_15_SHIFT    0                   /* bits 10-15 alignment shift */

#define BITS_10_15(v)       (((v) & BITS_10_15_MASK) >> BITS_10_15_SHIFT)

#define BITS_12_15_MASK     0000017u            /* bits 12-15 mask */
#define BITS_12_15_SHIFT    0                   /* bits 12-15 alignment shift */

#define BITS_12_15(v)       (((v) & BITS_12_15_MASK) >> BITS_12_15_SHIFT)


/* Instruction-class accessors */

#define SUBOP_MASK          BITS_0_3_MASK       /* subopcode mask */
#define SUBOP_SHIFT         BITS_0_3_SHIFT      /* subopcode alignment shift */
#define SUBOP(v)            BITS_0_3(v)         /* subopcode accessor */

#define STACKOP_A_MASK      BITS_4_9_MASK       /* stack operation A mask */
#define STACKOP_A_SHIFT     BITS_4_9_SHIFT      /* stack operation A alignment shift */
#define STACKOP_A(v)        BITS_4_9(v)         /* stack operation A accessor */

#define STACKOP_B_MASK      BITS_10_15_MASK     /* stack operation B mask */
#define STACKOP_B_SHIFT     BITS_10_15_SHIFT    /* stack operation B alignment shift */
#define STACKOP_B(v)        BITS_10_15(v)       /* stack operation B accessor */

#define SBBOP_MASK          BITS_5_9_MASK       /* shift/branch/bit operation mask */
#define SBBOP_SHIFT         BITS_5_9_SHIFT      /* shift/branch/bit operation alignment shift */
#define SBBOP(v)            BITS_5_9(v)         /* shift/branch/bit operation accessor */

#define MSFIFROP_MASK       BITS_4_7_MASK       /* move/special/firmware/immediate/field/register operation mask */
#define MSFIFROP_SHIFT      BITS_4_7_SHIFT      /* move/special/firmware/immediate/field/register operation alignment shift */
#define MSFIFROP(v)         BITS_4_7(v)         /* move/special/firmware/immediate/field/register operation accessor */

#define MSSUBOP_MASK        BITS_8_12_MASK      /* move/special suboperation mask */
#define MSSUBOP_SHIFT       BITS_8_12_SHIFT     /* move/special suboperation alignment shift */
#define MSSUBOP(v)          BITS_8_12(v)        /* move/special suboperation accessor */

#define SPECOP_MASK         BITS_12_15_MASK     /* special operation mask */
#define SPECOP_SHIFT        BITS_12_15_SHIFT    /* special operation alignment shift */
#define SPECOP(v)           BITS_12_15(v)       /* special operation accessor */

#define IOCPIMOP_MASK       BITS_4_7_MASK       /* I-O/control/program/immediate/memory operation mask */
#define IOCPIMOP_SHIFT      BITS_4_7_SHIFT      /* I-O/control/program/immediate/memory operation alignment shift */
#define IOCPIMOP(v)         BITS_4_7(v)         /* I-O/control/program/immediate/memory operation accessor */

#define IOCSUBOP_MASK       BITS_8_11_MASK      /* I-O/control suboperation mask */
#define IOCSUBOP_SHIFT      BITS_8_11_SHIFT     /* I-O/control suboperation alignment shift */
#define IOCSUBOP(v)         BITS_8_11(v)        /* I-O/control suboperation accessor */

#define CNTLOP_MASK         BITS_12_15_MASK     /* control operation mask */
#define CNTLOP_SHIFT        BITS_12_15_SHIFT    /* control operation alignment shift */
#define CNTLOP(v)           BITS_12_15(v)       /* control operation accessor */

#define MLBOP_MASK          BITS_0_3_MASK       /* memory/loop/branch operation mask */
#define MLBOP_SHIFT         BITS_0_3_SHIFT      /* memory/loop/branch operation alignment shift */
#define MLBOP(v)            BITS_0_3(v)         /* memory/loop/branch operation accessor */

#define FIRMEXTOP_MASK      BITS_8_11_MASK      /* firmware extension operation mask */
#define FIRMEXTOP_SHIFT     BITS_8_11_SHIFT     /* firmware extension operation alignment shift */
#define FIRMEXTOP(v)        BITS_8_11(v)        /* firmware extension operation accessor */

#define FMEXSUBOP_MASK      BITS_12_15_MASK     /* firmware extension suboperation mask */
#define FMEXSUBOP_SHIFT     BITS_12_15_SHIFT    /* firmware extension suboperation alignment shift */
#define FMEXSUBOP(v)        BITS_12_15(v)       /* firmware extension suboperation accessor */


/* Specific instruction accessors */

#define IOOP_K_MASK         0000017u            /* I/O K-field mask */
#define IOOP_K_SHIFT        0000000u            /* I/O K-field alignment shift */
#define IO_K(v)             (((v) & IOOP_K_MASK) >> IOOP_K_SHIFT)

#define X_FLAG              0004000u            /* index flag in bit 4 */
#define I_FLAG_BIT_4        0004000u            /* indirect flag in bit 4 */
#define I_FLAG_BIT_5        0002000u            /* indirect flag in bit 5 */
#define M_FLAG              0001000u            /* memory subop flag in bit 6 */

#define START_BIT_MASK      0000360u            /* start bit mask for bit field instructions */
#define START_BIT_SHIFT     4                   /* start bit alignment shift */
#define START_BIT(v)        (((v) & START_BIT_MASK) >> START_BIT_SHIFT)

#define BIT_COUNT_MASK      0000017u            /* bit count mask for bit field instructions */
#define BIT_COUNT_SHIFT     0                   /* bit count alignment shift */
#define BIT_COUNT(v)        (((v) & BIT_COUNT_MASK) >> BIT_COUNT_SHIFT)

#define BIT_POSITION_MASK   0000077u            /* bit position mask for bit test instructions */
#define BIT_POSITION_SHIFT  0                   /* bit position alignment shift */
#define BIT_POSITION(v)     (((v) & BIT_POSITION_MASK) >> BIT_POSITION_SHIFT)

#define SHIFT_COUNT_MASK    0000077u            /* shift count mask for shift instructions */
#define SHIFT_COUNT_SHIFT   0                   /* shift count alignment shift */
#define SHIFT_COUNT(v)      (((v) & SHIFT_COUNT_MASK) >> SHIFT_COUNT_SHIFT)

#define SHIFT_RIGHT_FLAG    0000100u            /* shift instructions left/right (0/1) flag */

#define MODE_DISP_MASK      0001777u            /* memory-reference mode and displacement mask */
#define MODE_MASK           0001700u            /* memory-reference mode mask */
#define MODE_SHIFT          6                   /* memory-reference mode alignment shift */

#define DISPL_31_SIGN       0000040u            /* sign bit for 0-31 displacements */
#define DISPL_255_SIGN      0000400u            /* sign bit for 0-255 displacements */

#define DISPL_31_MASK       0000037u            /* mask for 0-31 displacements */
#define DISPL_63_MASK       0000077u            /* mask for 0-63 displacements */
#define DISPL_127_MASK      0000177u            /* mask for 0-127 displacements */
#define DISPL_255_MASK      0000377u            /* mask for 0-255 displacements */

#define DISPL_P_FLAG        0001000u            /* P-relative displacement flag */
#define DISPL_DB_FLAG       0000400u            /* DB-relative displacement flag */
#define DISPL_QPOS_FLAG     0000200u            /* positive Q-relative displacement flag */
#define DISPL_QNEG_FLAG     0000100u            /* negative Q-relative displacement flag */

#define IMMED_MASK          0000377u            /* mask for immediate values */

#define SDEC2_MASK          0000003u            /* two-bit S-decrement mask for move instructions */
#define SDEC3_MASK          0000007u            /* three-bit S-decrement mask for move instructions */
#define SDEC_SHIFT          0                   /* S-decrement alignment shift */
#define SDEC2(v)            (((v) & SDEC2_MASK) >> SDEC_SHIFT)
#define SDEC3(v)            (((v) & SDEC3_MASK) >> SDEC_SHIFT)

#define DB_FLAG             0000020u            /* PB/DB base flag */

#define MVBW_CCF            0000030u            /* MVBW condition code flags */
#define MVBW_N_FLAG         0000020u            /* MVBW numeric flag */
#define MVBW_A_FLAG         0000010u            /* MVBW alphabetic flag */
#define MVBW_S_FLAG         0000004u            /* MVBW upshift flag */
#define MVBW_CCF_SHIFT      6                   /* CCF alignment in MVBW instruction */

#define NABS_FLAG           0000100u            /* CVDA negative absolute value flag */
#define ABS_FLAG            0000040u            /* CVDA absolute value flag */

#define EIS_SDEC_SHIFT      4                   /* EIS S-decrement alignment shift */


/* Explicit instruction opcodes and accessors */

#define NOP                 0000000u            /* no operation */
#define QASR                0015700u            /* quadruple arithmetic right shift */
#define DMUL                0020570u            /* double integer multiply */
#define DDIV                0020571u            /* double integer divide */
#define SED_1               0030041u            /* set enable interrupt */
#define HALT_10             0030370u            /* halt 10 */

#define MTFDS_MASK          0177730u            /* move to/from data segment mask */
#define MTFDS               0020130u            /* move to/from data segment */

#define EXIT_MASK           0177400u            /* exit procedure mask */
#define EXIT                0031400u            /* exit procedure */

#define PAUS_MASK           0177760u            /* pause mask */
#define PAUS                0030020u            /* pause */

#define BR_MASK             0173000u            /* conditional and unconditional branch mask */
#define BR_DBQS_I           0143000u            /* branch unconditionally DB/Q/S-relative indirect */
#define BCC                 0141000u            /* branch conditionally */
#define BCC_CCF_SHIFT       2                   /* CCF alignment in BCC instruction */

#define LSDX_MASK           0175000u            /* load/store double-word indexed mask */
#define LDD_X               0155000u            /* load double-word indexed */
#define STD_X               0165000u            /* store double-word indexed */

#define TBR_MASK            0177000u            /* test and branch mask */
#define TBA                 0050000u            /* test and branch, limit in A */
#define MTBA                0052000u            /* modify, test and branch, limit in A */
#define TBX                 0054000u            /* test and branch, limit in X */
#define MTBX                0056000u            /* modify, test and branch, limit in X */

#define CMD_TO_MASK         0000007u            /* CMD command word TO field mask */
#define CMD_MOP_MASK        0000060u            /* CMD command word MOP field mask */

#define CMD_TO_SHIFT        0                   /* CMD command word TO field alignment shift */
#define CMD_MOP_SHIFT       4                   /* CMD command word MOP field alignment shift */

#define CMD_TO(v)           (((v) & CMD_TO_MASK)  >> CMD_TO_SHIFT)
#define CMD_MOP(v)          (((v) & CMD_MOP_MASK) >> CMD_MOP_SHIFT)


/* PSHR/SETR instruction accessors */

#define PSR_RL_MASK         0000001u            /* PSHR/SETR register right-to-left mask */
#define PSR_LR_MASK         0000200u            /* PSHR/SETR register left-to-right mask */

#define PSR_SBANK           0000200u            /* Stack bank register */
#define PSR_DB_DBANK        0000100u            /* Data base and data bank registers */
#define PSR_DL              0000040u            /* Data limit register */
#define PSR_Z               0000020u            /* Stack limit register */
#define PSR_STA             0000010u            /* Status register */
#define PSR_X               0000004u            /* Index register */
#define PSR_Q               0000002u            /* Frame pointer */
#define PSR_S               0000001u            /* Stack pointer */

#define PSR_PRIV            (PSR_SBANK | PSR_DB_DBANK | PSR_DL | PSR_Z)


/* PCN instruction result values */

#define PCN_SERIES_II       1                   /* CPU number for the Series II */
#define PCN_SERIES_III      2                   /* CPU number for the Series III */


/* Reserved memory addresses */

#define CSTB_POINTER        0000000u            /* code segment table base pointer */
#define CSTX_POINTER        0000001u            /* code segment table extension pointer */
#define DST_POINTER         0000002u            /* data segment table pointer */
#define ICS_Q               0000005u            /* interrupt control stack marker pointer (QI) */
#define ICS_Z               0000006u            /* interrupt control stack limit (ZI) */
#define INTERRUPT_MASK      0000007u            /* interrupt mask */
#define SGT_POINTER         0001000u            /* system global tables pointer */


/* Code Segment Table accessors */

#define CST_A_BIT           0100000u            /* code segment is absent */
#define CST_M_BIT           0040000u            /* code segment is privileged */
#define CST_R_BIT           0020000u            /* code segment has been referenced flag */
#define CST_T_BIT           0010000u            /* code segment is to be traced */
#define CST_SEGLEN_MASK     0007777u            /* code segment length mask */
#define CST_BANK_MASK       0000017u            /* code segment bank mask */

#define CST_RESERVED        0000300u            /* number of CST entries reserved for the system */


/* Data Segment Table accessors */

#define DST_A_BIT           0100000u            /* data segment is absent */
#define DST_C_BIT           0040000u            /* data segment is clean (not modified) */
#define DST_R_BIT           0020000u            /* data segment has been referenced */
#define DST_SEGLEN_MASK     0017777u            /* data segment length mask */
#define DST_BANK_MASK       0000017u            /* data segment bank mask */


/* Segment Transfer Table accessors */

#define STT_LENGTH_MASK     0000377u            /* STT length mask */

#define STT_LENGTH_SHIFT    0                   /* STT length alignment shift */

#define STT_LENGTH(l)       (((l) & STT_LENGTH_MASK) >> STT_LENGTH_SHIFT)


/* Program label accessors */

#define LABEL_EXTERNAL      0100000u            /* external program label flag */
#define LABEL_STTN_MASK     0077400u            /* external program label STT number mask */
#define LABEL_SEGMENT_MASK  0000377u            /* external program label segment mask */

#define LABEL_STTN_SHIFT    8                   /* STT number alignment shift */
#define LABEL_SEGMENT_SHIFT 0                   /* segment number alignment shift */

#define LABEL_UNCALLABLE    0040000u            /* local program label uncallable flag */
#define LABEL_ADDRESS_MASK  0037777u            /* local program label address mask */

#define STT_NUMBER(l)       (((l) & LABEL_STTN_MASK) >> LABEL_STTN_SHIFT)
#define STT_SEGMENT(l)      (((l) & LABEL_SEGMENT_MASK) >> LABEL_SEGMENT_SHIFT)

#define ISR_SEGMENT         1                   /* segment number containing interrupt service routines */

#define LABEL_IRQ           (LABEL_EXTERNAL | ISR_SEGMENT)          /* label for interrupt requests */
#define LABEL_STTN_MAX      (LABEL_STTN_MASK >> LABEL_STTN_SHIFT)   /* STT number maximum value */

#define TO_LABEL(s,n)       ((s) | (n) << LABEL_STTN_SHIFT)


/* Stack marker accessors */

#define STMK_D              0100000u            /* dispatcher flag */
#define STMK_T              0100000u            /* trace flag */
#define STMK_M              0040000u            /* mapped flag */
#define STMK_RTN_ADDR       0037777u            /* PB-relative return address */


/* CPU registers */

extern HP_WORD CIR;                             /* Current Instruction Register */
extern HP_WORD NIR;                             /* Next Instruction Register */
extern HP_WORD PB;                              /* Program Base Register */
extern HP_WORD P;                               /* Program Counter */
extern HP_WORD PL;                              /* Program Limit Register */
extern HP_WORD PBANK;                           /* Program Segment Bank Register */
extern HP_WORD DL;                              /* Data Limit Register */
extern HP_WORD DB;                              /* Data Base Register */
extern HP_WORD DBANK;                           /* Data Segment Bank Register */
extern HP_WORD Q;                               /* Stack Marker Register */
extern HP_WORD SM;                              /* Stack Memory Register */
extern HP_WORD SR;                              /* Stack Register Counter */
extern HP_WORD Z;                               /* Stack Limit Register */
extern HP_WORD SBANK;                           /* Stack Segment Bank Register */
extern HP_WORD TR [4];                          /* Top of Stack Registers */
extern HP_WORD X;                               /* Index Register */
extern HP_WORD STA;                             /* Status Register */
extern HP_WORD SWCH;                            /* Switch Register */
extern HP_WORD CPX1;                            /* Run-Mode Interrupt Flags Register */
extern HP_WORD CPX2;                            /* Halt-Mode Interrupt Flags Register */
extern HP_WORD MOD;                             /* Module Control Register */
extern HP_WORD PCLK;                            /* Process Clock Register */
extern HP_WORD CNTR;                            /* Microcode Counter */


/* Top of stack register names */

#define RA                  TR [0]
#define RB                  TR [1]
#define RC                  TR [2]
#define RD                  TR [3]


/* CPU state */

extern jmp_buf     cpu_save_env;                /* saved environment for microcode aborts */
extern POWER_STATE cpu_power_state;             /* power supply state */
extern EXEC_STATE  cpu_micro_state;             /* micromachine execution state */
extern uint32      cpu_stop_flags;              /* set of simulation stop flags */
extern t_bool      cpu_base_changed;            /* TRUE if any base register has been changed */
extern UNIT        cpu_unit [];                 /* CPU unit array (needed for memory size) */


/* Condition Code B mapping table */

extern const HP_WORD cpu_ccb_table [256];       /* byte-value to condition-code map */


/* Global CPU functions */

extern void cpu_push       (void);
extern void cpu_pop        (void);
extern void cpu_queue_up   (void);
extern void cpu_queue_down (void);
extern void cpu_flush      (void);
extern void cpu_adjust_sr  (uint32 target);
extern void cpu_mark_stack (void);

extern void cpu_ea (HP_WORD mode_disp, ACCESS_CLASS *classification, HP_WORD *offset, BYTE_SELECTOR *selector);

extern void cpu_setup_irq_handler  (IRQ_CLASS class, HP_WORD parameter);
extern void cpu_setup_ics_irq      (IRQ_CLASS class, TRAP_CLASS trap);
extern void cpu_run_mode_interrupt (HP_WORD device_number);
extern void cpu_setup_code_segment (HP_WORD label, HP_WORD* status, HP_WORD *entry_0);
extern void cpu_setup_data_segment (HP_WORD segment_number, HP_WORD *bank, HP_WORD *address);
extern void cpu_call_procedure     (HP_WORD label);
extern void cpu_exit_procedure     (HP_WORD new_q, HP_WORD new_sm, HP_WORD parameter);
extern void cpu_start_dispatcher   (void);
extern void cpu_update_pclk        (void);


/* Global CPU instruction execution routines */

extern t_stat cpu_branch_short (t_bool check_loop);

extern HP_WORD cpu_add_16 (HP_WORD augend,       HP_WORD addend);
extern HP_WORD cpu_sub_16 (HP_WORD minuend,      HP_WORD subtrahend);
extern HP_WORD cpu_mpy_16 (HP_WORD multiplicand, HP_WORD multiplier);

extern t_stat cpu_stack_op                      (void);
extern t_stat cpu_shift_branch_bit_op           (void);
extern t_stat cpu_move_spec_fw_imm_field_reg_op (void);
extern t_stat cpu_io_cntl_prog_imm_mem_op       (void);
