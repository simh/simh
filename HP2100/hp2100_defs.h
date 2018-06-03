/* hp2100_defs.h: HP 2100 System architectural declarations

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018  J. David Bryan

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

   07-May-18    JDB     Added NOTE_SKIP, simplified setSKF macro
   02-May-18    JDB     Added "SIRDEV" for first device to receive the SIR signal
   16-Oct-17    JDB     Suppressed logical-not-parentheses warning on clang
   30-Aug-17    JDB     Replaced POLL_WAIT with POLL_PERIOD
   07-Aug-17    JDB     Added "hp_attach"
   20-Jul-17    JDB     Removed STOP_OFFLINE, STOP_PWROFF stop codes
   11-Jul-17    JDB     Moved "ibl_copy" to hp2100_cpu.h
   26-Jun-17    JDB     Moved I/O instruction subopcode constants to hp2100_cpu.c
   14-Jun-17    JDB     Renamed STOP_RSRV to STOP_UNIMPL (unimplemented instruction)
   15-Mar-17    JDB     Added global trace flags
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   15-Feb-17    JDB     Deleted unneeded guard macro definition
   16-Jan-17    JDB     Added tracing and console output macros
   13-Jan-17    JDB     Added fprint_cpu
   10-Jan-17    JDB     Added architectural constants
   05-Aug-16    JDB     Removed PC_Global renaming; P register is now "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   19-Jun-15    JDB     Conditionally use PC_Global for PC for version 4.0 and on
   30-Dec-14    JDB     Added S-register parameters to ibl_copy, more IBL constants
   28-Dec-14    JDB     Changed suppression from #pragma GCC to #pragma clang
   05-Feb-13    JDB     Added declaration for hp_fprint_stopped
   18-Mar-13    JDB     Added "-Wdangling-else" to the suppression pragmas
                        Removed redundant extern declarations
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   14-Dec-12    JDB     Added "-Wbitwise-op-parentheses" to the suppression pragmas
   12-May-12    JDB     Added pragmas to suppress logical operator precedence warnings
   10-Feb-12    JDB     Added hp_setsc, hp_showsc functions to support SC modifier
   28-Mar-11    JDB     Tidied up signal handling
   29-Oct-10    JDB     DMA channels renamed from 0,1 to 1,2 to match documentation
   27-Oct-10    JDB     Revised I/O signal enum values for concurrent signals
                        Revised I/O macros for new signal handling
   09-Oct-10    JDB     Added DA and DC device select code assignments
   07-Sep-08    JDB     Added POLL_FIRST to indicate immediate connection attempt
   15-Jul-08    JDB     Rearranged declarations with hp2100_cpu.h
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   25-Jun-08    JDB     Added PIF device
   17-Jun-08    JDB     Declared fmt_char() function
   26-May-08    JDB     Added MPX device
   24-Apr-08    JDB     Added I_MRG_I, I_JSB, I_JSB_I, and I_JMP instruction masks
   14-Apr-08    JDB     Changed TMR_MUX to TMR_POLL for idle support
                        Added POLLMODE, sync_poll() declaration
                        Added I_MRG, I_ISZ, I_IOG, I_STF, and I_SFS instruction masks
   07-Dec-07    JDB     Added BACI device
   10-Nov-07    JDB     Added 16/32-bit unsigned-to-signed conversions
   11-Jan-07    JDB     Added 12578A DMA byte packing to DMA structure
   28-Dec-06    JDB     Added CRS backplane signal as I/O pseudo-opcode
                        Added DMASK32 32-bit mask value
   21-Dec-06    JDB     Changed MEM_ADDR_OK for 21xx loader support
   12-Sep-06    JDB     Define NOTE_IOG to recalc interrupts after instr exec
                        Rename STOP_INDINT to NOTE_INDINT (not a stop condition)
   30-Dec-04    JDB     Added IBL_DS_HEAD head number mask
   19-Nov-04    JDB     Added STOP_OFFLINE, STOP_PWROFF stop codes
   25-Apr-04    RMS     Added additional IBL definitions
                        Added DMA EDT I/O pseudo-opcode
   25-Apr-03    RMS     Revised for extended file support
   24-Oct-02    RMS     Added indirect address interrupt
   08-Feb-02    RMS     Added DMS definitions
   01-Feb-02    RMS     Added terminal multiplexor support
   16-Jan-02    RMS     Added additional device support
   30-Nov-01    RMS     Added extended SET/SHOW support
   15-Oct-00    RMS     Added dynamic device numbers
   14-Apr-99    RMS     Changed t_addr to unsigned

   The [original] author gratefully acknowledges the help of Jeff Moffat in
   answering questions about the HP2100; and of Dave Bryan in adding features
   and correcting errors throughout the simulator.


   This file provides the general declarations used throughout the HP 2100
   simulator.  It is required by all modules.


   -----------------------------------------------------
   Implementation Note -- Compiling the Simulator as C++
   -----------------------------------------------------

   Although simulators are written in C, the SIMH project encourages developers
   to compile them with a C++ compiler to obtain the more careful type checking
   provided.  To obtain successful compilations, the simulator must be written
   in the subset of C that is also valid C++.  Using valid C features beyond
   that subset, as the HP 2100 simulator does, will produce C++ compiler errors.

   The standard C features used by the simulator that prevent error-free C++
   compilation are:

    1. Incomplete types.

       In C, mutually recursive type definitions are allowed by the use of
       incomplete type declarations, such as "DEVICE ms_dev;" followed later by
       "DEVICE ms_dev {...};".  Several HP device simulators use this feature to
       place a pointer to the device structure in the "desc" field of an MTAB
       array element, typically when the associated validation or display
       routine handles multiple devices.  As the DEVICE contains a pointer to
       the MTAB array, and an MTAB array element contains a pointer to the
       DEVICE, the definitions are mutually recursive, and incomplete types are
       employed.  C++ does not permit incomplete types.

    2. Implicit conversion of ints to enums.

       In C, enumeration types are compatible with integer types, and its
       members are constants having type "int".  As such, they are semantically
       equivalent to and may be used interchangeably with integers.  For the
       developer, though, C enumerations have some advantages.  In particular,
       the compiler may check a "switch" statement to ensure that all of the
       enumeration cases are covered.  Also, a mathematical set may be modeled
       by an enumeration type with disjoint enumerator values, with the bitwise
       integer OR and AND operators modeling the set union and intersection
       operations.  The latter has direct support in the "gdb" debugger, which
       will display an enumerated type value as a union of the various
       enumerators.  The HP simulator makes extensive use of both features to
       model hardware signal buses (e.g., INBOUND_SET, OUTBOUND_SET) and so
       performs bitwise integer operations on the enumerations to model signal
       assertion and denial.  In C++, implicit conversion from enumerations to
       integers is allowed, but conversion from integers to enumerations is
       illegal without explicit casts.  Therefore, the idiom employed by the
       simulator to assert a signal (e.g., "outbound_signals |= INTREQ") is
       rejected by the C++ compiler.

    3. Implicit increment operations on enums.

       Because enums are compatible with integers in C, no special enumerator
       increment operator is provided.  To cycle through the range of an
       enumeration type, e.g. in a "for" statement, the standard integer
       increment operator, "++", is used.  In C++, the "++" operator must be
       overloaded with a version specific to the enumeration type; applying the
       integer "++" to an enumeration is illegal.

    4. Use of C++ keywords as variable names.

       C++ reserves a number of additional keywords beyond those reserved by C.
       Use of any of these keywords as a variable or type name is legal C but
       illegal C++.  The HP simulator uses variables named "class" and
       "operator", which are keywords in C++.

   The HP simulator is written in ISO standard C and will compile cleanly with a
   compiler implementing the 1999 C standard.  Compilation as C++ is not a goal
   of the simulator and cannot work, given the incompatibilities listed above.
*/



#include "sim_rev.h"
#include "sim_defs.h"



/* The following pragmas quell clang and Microsoft Visual C++ warnings that are
   on by default but should not be, in my opinion.  They warn about the use of
   perfectly valid code and require the addition of redundant parentheses and
   braces to silence them.  Rather than clutter up the code with scores of extra
   symbols that, in my view, make the code harder to read and maintain, I elect
   to suppress these warnings.

   VC++ 2008 warning descriptions:

    - 4114: "same type qualifier used more than once" [legal per C99]

    - 4554: "check operator precedence for possible error; use parentheses to
            clarify precedence"

    - 4996: "function was declared deprecated"
*/

#if defined (__clang__)
  #pragma clang diagnostic ignored "-Wlogical-not-parentheses"
  #pragma clang diagnostic ignored "-Wlogical-op-parentheses"
  #pragma clang diagnostic ignored "-Wbitwise-op-parentheses"
  #pragma clang diagnostic ignored "-Wshift-op-parentheses"
  #pragma clang diagnostic ignored "-Wdangling-else"

#elif defined (_MSC_VER)
  #pragma warning (disable: 4114 4554 4996)

#endif


/* Device register display mode flags */

#define REG_X               REG_VMIO                    /* permit symbolic display overrides */

#define REG_A               (1u << REG_V_UF + 0)        /* default format is -A (one ASCII character) */
#define REG_C               (1u << REG_V_UF + 1)        /* default format is -C (two ASCII characters) */
#define REG_M               (1u << REG_V_UF + 2)        /* default format is -M (mnemonic) */


/* Global tracing flags.

   Global tracing flags are allocated in descending order, as they may be used
   by modules that allocate their own private flags in ascending order.  No
   check is made for overlapping values.
*/

#define TRACE_CMD           (1u << 31)          /* trace interface or controller commands */
#define TRACE_INCO          (1u << 30)          /* trace interface or controller command initiations and completions */
#define TRACE_CSRW          (1u << 29)          /* trace interface control, status, read, and write actions */
#define TRACE_STATE         (1u << 28)          /* trace state changes */
#define TRACE_SERV          (1u << 27)          /* trace unit service scheduling calls and entries */
#define TRACE_PSERV         (1u << 26)          /* trace periodic unit service scheduling calls and entries */
#define TRACE_XFER          (1u << 25)          /* trace data receptions and transmissions */
#define TRACE_IOBUS         (1u << 24)          /* trace I/O bus signals and data words received and returned */

#define DEB_CMDS            (1u << 23)          /* (old) trace command initiations and completions */
#define DEB_CPU             (1u << 22)          /* (old) trace words received from and sent to the CPU */
#define DEB_BUF             (1u << 21)          /* (old) trace data read from and written to the FIFO */
#define DEB_XFER            (1u << 20)          /* (old) trace data receptions and transmissions */
#define DEB_RWS             (1u << 19)          /* (old) trace tape reads, writes, and status returns */
#define DEB_RWSC            (1u << 18)          /* (old) trace disc read/write/status/control commands */
#define DEB_SERV            (1u << 17)          /* (old) trace unit service scheduling calls and entries */


/* Tracing and console output.

   "tprintf" is used to write tracing messages.  It does an "fprintf" to the
   debug output stream if the stream is open and the trace "flag" is currently
   enabled in device "dev".  Otherwise, it's a NOP.  "..." is the format string
   and associated values.

   "tpprintf" is identical to "tprintf", except that a device pointer is passed
   instead of a device structure.

   "TRACING" and "TRACINGP" implement the test conditions for device and device
   pointer tracing, respectively.  They are used explicitly only when several
   trace statements employing the same flag are required, and it is desirable to
   avoid repeating the stream and flag test for each one.

   "cprintf", "cputs", and "cputc" are used to write messages to the console
   and, if console logging is enabled, to the log output stream.  They do
   "(f)printf", "fputs", or "(f)putc", respectively.  "..." is the format string
   and associated values, "str" is the string to write, and "ch" is the
   character to write.


   Implementation notes:

    1. The "cputs" macro uses "fputs" for both console and log file output
       because "puts" appends a newline, whereas "fputs" does not.
*/

#define TRACING(d,f)        (sim_deb != NULL && ((d).dctrl & (f)))

#define TRACINGP(d,f)       (sim_deb != NULL && ((d)->dctrl & (f)))

#define tprintf(dev, flag, ...) \
          if (TRACING (dev, flag)) \
              hp_trace (&(dev), (flag), __VA_ARGS__); \
          else \
              (void) 0

#define tpprintf(dptr, flag, ...) \
          if (TRACINGP (dptr, flag)) \
              hp_trace ((dptr), (flag), __VA_ARGS__); \
          else \
              (void) 0

#define cprintf(...) \
          do { \
              printf (__VA_ARGS__); \
              if (sim_log) \
                  fprintf (sim_log, __VA_ARGS__); \
              } \
          while (0)

#define cputs(str) \
          do { \
              fputs (str, stdout); \
              if (sim_log) \
                  fputs (str, sim_log); \
              } \
          while (0)

#define cputc(ch) \
          do { \
              putc (ch); \
              if (sim_log) \
                  fputc (ch, sim_log); \
              } \
          while (0)


/* Simulation stop and notification codes.

   The STOP_* status codes stop the simulator.  The "sim_stop_messages" array in
   "hp2100_sys.c" contains the message strings that correspond one-for-one with
   the stop codes.

   The NOTE_* status codes do not stop the simulator.  Instead, they inform the
   instruction execution loop of special situations that occurred while
   processing the current machine instruction..


   Implementation notes:

    1. Codes before STOP_RERUN cause the instruction to be rerun, so P is backed
       up twice.  For codes after, P points to the next instruction to be
       executed (which is the current instruction for an infinite loop stop).
*/

#define STOP_UNIMPL         1                   /* unimplemented instruction stop */
#define STOP_UNSC           2                   /* stop on I/O to an unassigned select code */
#define STOP_UNDEF          3                   /* undefined instruction stop */
#define STOP_INDIR          4                   /* stop on an indirect loop */

#define STOP_RERUN          4                   /* stops above here cause the instruction to be re-run */

#define STOP_HALT           5                   /* programmed halt */
#define STOP_BRKPNT         6                   /* breakpoint */
#define STOP_NOCONN         7                   /* no connection */
#define STOP_NOTAPE         8                   /* no tape */
#define STOP_EOT            9                   /* end of tape */

#define NOTE_IOG            10                  /* an I/O instruction was executed */
#define NOTE_INDINT         11                  /* an interrupt occurred while resolving an indirect address */
#define NOTE_SKIP           12                  /* the SKF signal was asserted by an I/O interface */


/* Modifier validation identifiers */

#define MTAB_XDV            (MTAB_XTD | MTAB_VDV)
#define MTAB_XUN            (MTAB_XTD | MTAB_VUN)


/* I/O event timing.

   I/O events are scheduled for future service by specifying the desired delay
   in units of event ticks.  Typically, one event tick represents the execution
   of one CPU instruction, and this is the way event ticks are defined in the
   current simulator implementation.  However, the various CPUs themselves not
   only vary in speed, but the actual instruction times vary greatly, due to the
   presence of block move, compare, and scan instructions.  Variations of an
   order of magnitude are common, and two orders or more are possible for longer
   blocks.

   The 24296-90001 Diagnostic Configurator provides a one millisecond timer for
   use by the diagnostic programs.  The timer is a two-instruction software
   loop, plus four instructions of entry/exit overhead, based on the processor
   type.  The values provided are:

             Loop     Instr
     CPU     Count    /msec
     ------  -----    -----
     2114      246      496
     2115      246      496
     2116      309      622
     2100      252      508
     1000-M    203      410
     1000-E   1573 *   1577

     * The E-Series TIMER instruction is used instead of a software loop.  TIMER
       re-executes an internal decrement until the supplied value reaches zero.

   To pass diagnostics that time peripheral operations, the simulator assumes
   the E-Series execution rate for all devices (0.634 microseconds per event
   tick), although this results in needlessly long delays for normal operation.
   A correct implementation would change the timing base depending on the
   currently selected CPU.

   To accommodate possible future variable instruction timing, I/O service
   activation times must not assume a constant 0.634 microseconds per event
   tick.  Delays should be defined in terms of the "uS" (microseconds), "mS"
   (milliseconds), and "S" (seconds) macros below.
*/

#define USEC_PER_EVENT      0.634               /* average CPU instruction time in microseconds */

#define uS(t)               (uint32) ((t) > USEC_PER_EVENT ? (t) / USEC_PER_EVENT + 0.5 : 1)
#define mS(t)               (uint32) (((t) * 1000.0)    / USEC_PER_EVENT + 0.5)
#define S(t)                (uint32) (((t) * 1000000.0) / USEC_PER_EVENT + 0.5)


/* Architectural constants.

   These macros specify the width, sign location, value mask, and minimum and
   maximum signed and unsigned values for the data sizes supported by the
   simulator.  In addition, masks for 16-bit and 32-bit overflow are defined (an
   overflow is indicated if the masked bits are not all ones or all zeros).

   The HP_WORD type is used to declare variables that represent 16-bit registers
   or buses in hardware.


   Implementation notes:

    1. The HP_WORD type is a 32-bit unsigned type, instead of the more logical
       16-bit unsigned type.  There are two reasons for this.  First, SCP
       requires that scalars referenced by REG (register) entries be 32 bits in
       size.  Second, IA-32 processors execute instructions with 32-bit operands
       much faster than those with 16-bit operands.

       Using 16-bit operands omits the masking required for 32-bit values.  For
       example, the code generated for the following operations is as follows:

         uint16 a, b, c;
         a = b + c & 0xFFFF;

            movzwl  _b, %eax
            addw    _c, %ax
            movw    %ax, _a

         uint32 x, y, z;
         x = y + z & 0xFFFF;

            movl    _z, %eax
            addl    _y, %eax
            andl    $65535, %eax
            movl    %eax, _x

       However, the first case uses operand override prefixes, which require
       substantially more time to decode (6 clock cycles vs. 1 clock cycle).
       This time outweighs the additional 32-bit AND instruction, which executes
       in 1 clock cycle.

    2. The MEMORY_WORD type is a 16-bit unsigned type, corresponding with the
       16-bit main memory in the HP 21xx/1000.  Unlike the general data type,
       which is a 32-bit type for speed, main memory does not benefit from the
       faster 32-bit execution on IA-32 processors, as only one instruction in
       the mem_read and mem_write routines has an operand override that invokes
       the slower instruction fetch path.  There is a negligible difference in
       the Memory Pattern Test diagnostic execution speeds for the uint32 vs.
       uint16 definition, whereas the VM requirements are doubled for the
       former.
*/

typedef uint32              HP_WORD;                    /* HP 16-bit data word representation */
typedef uint16              MEMORY_WORD;                /* HP 16-bit memory word representation */

#define D4_WIDTH            4                           /* 4-bit data bit width */
#define D4_MASK             0017u                       /* 4-bit data mask */

#define D8_WIDTH            8                           /* 8-bit data bit width */
#define D8_MASK             0377u                       /* 8-bit data mask */
#define D8_UMAX             0377u                       /* 8-bit unsigned maximum value */
#define D8_SMAX             0177u                       /* 8-bit signed maximum value */
#define D8_SMIN             0200u                       /* 8-bit signed minimum value */
#define D8_SIGN             0200u                       /* 8-bit sign */

#define D16_WIDTH           16                          /* 16-bit data bit width */
#define D16_MASK            0177777u                    /* 16-bit data mask */
#define D16_UMAX            0177777u                    /* 16-bit unsigned maximum value */
#define D16_SMAX            0077777u                    /* 16-bit signed maximum value */
#define D16_SMIN            0100000u                    /* 16-bit signed minimum value */
#define D16_SIGN            0100000u                    /* 16-bit sign */

#define D32_WIDTH           32                          /* 32-bit data bit width */
#define D32_MASK            037777777777u               /* 32-bit data mask */
#define D32_UMAX            037777777777u               /* 32-bit unsigned maximum value */
#define D32_SMAX            017777777777u               /* 32-bit signed maximum value */
#define D32_SMIN            020000000000u               /* 32-bit signed minimum value */
#define D32_SIGN            020000000000u               /* 32-bit sign */

#define D48_WIDTH           48                          /* 48-bit data bit width */
#define D48_MASK            07777777777777777uL         /* 48-bit data mask */
#define D48_UMAX            07777777777777777uL         /* 48-bit unsigned maximum value */
#define D48_SMAX            03777777777777777uL         /* 48-bit signed maximum value */
#define D48_SMIN            04000000000000000uL         /* 48-bit signed minimum value */
#define D48_SIGN            04000000000000000uL         /* 48-bit sign */

#define D64_WIDTH           64                          /* 64-bit data bit width */
#define D64_MASK            01777777777777777777777uL   /* 64-bit data mask */
#define D64_UMAX            01777777777777777777777uL   /* 64-bit unsigned maximum value */
#define D64_SMAX            00777777777777777777777uL   /* 64-bit signed maximum value */
#define D64_SMIN            01000000000000000000000uL   /* 64-bit signed minimum value */
#define D64_SIGN            01000000000000000000000uL   /* 64-bit sign */

#define S16_OVFL_MASK       ((uint32) D16_UMAX << D16_WIDTH | \
                              D16_SIGN)                 /* 16-bit signed overflow mask */

#define S32_OVFL_MASK       ((t_uint64) D32_UMAX << D32_WIDTH | \
                              D32_SIGN)                 /* 32-bit signed overflow mask */

#define LSB                 1u                          /* least-significant bit */
#define D16_SIGN_LSB        (D16_SIGN | LSB)            /* bit 15 and bit 0 */

#define R_MASK              0177777u                    /* 16-bit register mask */


/* Memory constants */

#define OF_WIDTH            10                              /* offset bit width */
#define OF_MASK             ((1u << OF_WIDTH) - 1)          /* offset mask (2 ** 10 - 1) */
#define OF_MAX              ((1u << OF_WIDTH) - 1)          /* offset maximum (2 ** 10 - 1) */

#define PG_WIDTH            10                              /* page bit width */
#define PG_MASK             ((1u << PG_WIDTH) - 1)          /* page mask (2 ** 10 - 1) */
#define PG_MAX              ((1u << PG_WIDTH) - 1)          /* page maximum (2 ** 10 - 1) */

#define LA_WIDTH            15                              /* logical address bit width */
#define LA_MASK             ((1u << LA_WIDTH) - 1)          /* logical address mask (2 ** 15 - 1) */
#define LA_MAX              ((1u << LA_WIDTH) - 1)          /* logical address maximum (2 ** 15 - 1) */

#define PA_WIDTH            20                              /* physical address bit width */
#define PA_MASK             ((1u << PA_WIDTH) - 1)          /* physical address mask (2 ** 20 - 1) */
#define PA_MAX              ((1u << PA_WIDTH) - 1)          /* physical address maximum (2 ** 20 - 1) */

#define DV_WIDTH            16                              /* data value bit width */
#define DV_MASK             ((1u << DV_WIDTH) - 1)          /* data value mask (2 ** 16 - 1) */
#define DV_SIGN             ( 1u << (DV_WIDTH - 1))         /* data value sign (2 ** 15) */
#define DV_UMAX             ((1u << DV_WIDTH) - 1)          /* data value unsigned maximum (2 ** 16 - 1) */
#define DV_SMAX             ((1u << (DV_WIDTH - 1)) - 1)    /* data value signed maximum  (2 ** 15 - 1) */


/* Memory address macros.

   These macros convert between logical and physical addresses.  The functions
   provided are:

     - PAGE   -- extract the page number part of a physical address
     - OFFSET -- extract the offset part of a physical address
     - TO_PA  -- merge a page number and offset into a physical address
*/

#define PAGE(p)             ((p) >> PG_WIDTH & PG_MASK)
#define OFFSET(p)           ((p) & OF_MASK)
#define TO_PA(b,o)          (((uint32) (b) & PG_MASK) << PG_WIDTH | (uint32) (o) & OF_MASK)


/* Memory access classifications.

   The access classification determines the DMS map set and protections to use
   when reading or writing memory words.  The classification also is used to
   label each data access when tracing is enabled.  When DMS is disabled, or
   when the CPU is one of the 21xx models, the classification is irrelevant.
*/

typedef enum {
    Fetch,                                      /* instruction fetch, current map */
    Data,                                       /* data access, current map */
    Data_Alternate,                             /* data access, alternate map */
    Data_System,                                /* data access, system map */
    Data_User,                                  /* data access, user map */
    DMA_Channel_1,                              /* DMA channel 1, port A map */
    DMA_Channel_2                               /* DMA channel 2, port B map */
    } ACCESS_CLASS;


/* Portable conversions.

   SIMH is written with the assumption that the defined-size types (e.g.,
   uint16) are at least the required number of bits but may be larger.
   Conversions that otherwise would make inherent size assumptions must instead
   be coded explicitly.  For example, doing:

     negative_value_32 = (int32) negative_value_16;

   ...will not guarantee that the upper 16 bits of "negative_value_32" are all
   ones, whereas the supplied sign-extension macro will.

   The conversions available are:

     - SEXT8  -- signed 8-bit value sign-extended to int32
     - SEXT16 -- signed 16-bit value sign-extended to int32
     - NEG8   -- signed 8-bit value negated
     - NEG16  -- signed 16-bit value negated
     - NEG32  -- signed 32-bit value negated
     - INT16  -- uint16 to int16
     - INT32  -- uint32 to int32


   Implementation notes:

    1. The SEXTn and INTn routines assume that their values are masked to
       exactly n bits before invoking.
*/

#define SEXT8(x)        ((int32) ((x) & D8_SIGN  ? (x) | ~D8_MASK  : (x)))
#define SEXT16(x)       ((int32) ((x) & D16_SIGN ? (x) | ~D16_MASK : (x)))

#define NEG8(x)         ((~(x) + 1) & D8_MASK)
#define NEG16(x)        ((~(x) + 1) & D16_MASK)
#define NEG32(x)        ((~(x) + 1) & D32_MASK)

#define INT16(u)        ((u) > D16_SMAX ? (-(int16) (D16_UMAX - (u)) - 1) : (int16) (u))
#define INT32(u)        ((u) > D32_SMAX ? (-(int32) (D32_UMAX - (u)) - 1) : (int32) (u))


/* Byte accessors.

   These macros extract the upper and lower bytes from a word and form a word
   from upper and lower bytes.  Replacement of a byte within a word is also
   provided, as is an enumeration type that defines byte selection.

   The accessors are:

     - UPPER_BYTE     -- return the byte from the upper position of a word value
     - LOWER_BYTE     -- return the byte from the lower position of a word value
     - TO_WORD        -- return a word with the specified upper and lower bytes

     - REPLACE_UPPER  -- replace the upper byte of the word value
     - REPLACE_LOWER  -- replace the lower byte of the word value

*/

typedef enum {
    upper,                                      /* upper byte selected */
    lower                                       /* lower byte selected */
    } BYTE_SELECTOR;

#define UPPER_BYTE(w)       (uint8)   ((w) >> D8_WIDTH & D8_MASK)
#define LOWER_BYTE(w)       (uint8)   ((w) &  D8_MASK)
#define TO_WORD(u,l)        (HP_WORD) (((u) & D8_MASK) << D8_WIDTH | (l) & D8_MASK)

#define REPLACE_UPPER(w,b)  ((w) & D8_MASK | ((b) & D8_MASK) << D8_WIDTH)
#define REPLACE_LOWER(w,b)  ((w) & D8_MASK << D8_WIDTH | (b) & D8_MASK)


/* Double-word accessors */

#define UPPER_WORD(d)       (HP_WORD) ((d) >> D16_WIDTH & D16_MASK)
#define LOWER_WORD(d)       (HP_WORD) ((d) &  D16_MASK)

#define TO_DWORD(u,l)       ((uint32) (u) << D16_WIDTH | (l))


/* CPU instruction symbolic source.

   The memory-reference group (MRG) instructions do not specify full logical
   addresses of their targets.  Instead, they specify offsets from either the
   base page or the current page.  Instructions specifying base-page offsets are
   always displayed with target addresses between 0000-1777.  The display and
   parsing of instructions specifying current-page offsets depends on the source
   of the instructions.

   If the current-page CPU instruction is contained in main memory, the current
   page is taken from the address of the word containing the instruction, and
   the full target address between 00000-77777 is displayed or parsed.  However,
   if the instruction is contained in a device buffer, e.g., a disc drive sector
   buffer, the destination memory address is unknown until the instruction is
   transferred to memory.  In this case, the target address is displayed or
   parsed as the offset prefixed with the letter "C" (e.g., "LDA C 1200").  In
   order to present the proper symbolic behavior, the mnemonic formatter and
   parser must know the source of the request.

   Additionally, display requests from the EXAMINE command have preloaded a
   value array with the maximum number of words required to encode the longest
   instruction.  This is inefficient, as only a fraction of the instruction set
   requires more than one word.  For EXAMINE commands entered by the user at the
   SCP prompt, this is unimportant.  However, for calls from the CPU instruction
   trace routine, the overhead is significant.  In the latter case, the array is
   loaded only with a single word, and the mnemonic formatter loads additional
   words if the specific instruction to be displayed requires them.
*/

typedef enum {
    Device_Symbol,                              /* called for an EXAMINE <device> or DEPOSIT <device> command */
    CPU_Symbol,                                 /* called for an EXAMINE CPU or DEPOSIT CPU command */
    CPU_Trace                                   /* called for a CPU trace command */
    } SYMBOL_SOURCE;


/* Memory constants (deprecated) */

#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define VA_N_SIZE       15                              /* virtual addr size */
#define VASIZE          (1 << VA_N_SIZE)
#define VAMASK          077777                          /* virt addr mask */
#define PA_N_SIZE       20                              /* phys addr size */
#define PASIZE          (1 << PA_N_SIZE)
#define PAMASK          (PASIZE - 1)                    /* phys addr mask */

/* Architectural constants (deprecated) */

#define SIGN32          020000000000                    /* 32b sign */
#define DMASK32         037777777777                    /* 32b data mask/maximum value */
#define DMAX32          017777777777                    /* 32b maximum signed value */
#define SIGN            0100000                         /* 16b sign */
#define DMASK           0177777                         /* 16b data mask/maximum value */
#define DMAX            0077777                         /* 16b maximum signed value */
#define DMASK8          0377                            /* 8b data mask/maximum value */

/* Timers */

#define TMR_CLK         0                               /* clock */
#define TMR_POLL        1                               /* input polling */

#define POLL_RATE       100                             /* poll 100 times per second */
#define POLL_FIRST      1                               /* first poll is "immediate" */
#define POLL_PERIOD     mS (10)                         /* 10 millisecond poll period */

typedef enum { INITIAL, SERVICE } POLLMODE;             /* poll synchronization modes */


/* I/O devices - fixed select code assignments */

#define CPU             000                             /* interrupt control */
#define OVF             001                             /* overflow */
#define DMALT1          002                             /* DMA 1 alternate */
#define DMALT2          003                             /* DMA 2 alternate */
#define PWR             004                             /* power fail */
#define PRO             005                             /* parity/mem protect */
#define DMA1            006                             /* DMA channel 1 */
#define DMA2            007                             /* DMA channel 2 */

/* I/O devices - variable select code assignment defaults */

#define PTR             010                             /* 12597A-002 paper tape reader */
#define TTY             011                             /* 12531C teleprinter */
#define PTP             012                             /* 12597A-005 paper tape punch */
#define CLK             013                             /* 12539C time-base generator */
#define LPS             014                             /* 12653A line printer */
#define LPT             015                             /* 12845A line printer */
#define MTD             020                             /* 12559A data */
#define MTC             021                             /* 12559A control */
#define DPD             022                             /* 12557A data */
#define DPC             023                             /* 12557A control */
#define DQD             024                             /* 12565A data */
#define DQC             025                             /* 12565A control */
#define DRD             026                             /* 12610A data */
#define DRC             027                             /* 12610A control */
#define MSD             030                             /* 13181A data */
#define MSC             031                             /* 13181A control */
#define IPLI            032                             /* 12566B link in */
#define IPLO            033                             /* 12566B link out */
#define DS              034                             /* 13037A control */
#define BACI            035                             /* 12966A Buffered Async Comm Interface */
#define MPX             036                             /* 12792A/B/C 8-channel multiplexer */
#define PIF             037                             /* 12620A/12936A Privileged Interrupt Fence */
#define MUXL            040                             /* 12920A lower data */
#define MUXU            041                             /* 12920A upper data */
#define MUXC            042                             /* 12920A control */
#define DI_DA           043                             /* 12821A Disc Interface with Amigo disc devices */
#define DI_DC           044                             /* 12821A Disc Interface with CS/80 disc and tape devices */

#define OPTDEV          002                             /* start of optional devices */
#define SIRDEV          004                             /* start of devices that receive SIR */
#define CRSDEV          006                             /* start of devices that receive CRS */
#define VARDEV          010                             /* start of variable assignments */
#define MAXDEV          077                             /* end of select code range */


/* I/O backplane signals.

   The IOSIGNAL declarations mirror the hardware I/O backplane signals.  A set
   of one or more signals forms an IOCYCLE that is sent to a device IOHANDLER
   for action.  The CPU and DMA dispatch one signal set to the target device
   handler per I/O cycle.  A CPU cycle consists of either one or two signals; if
   present, the second signal will be CLF.  A DMA cycle consists of from two to
   five signals.  In addition, a front-panel PRESET or power-on reset dispatches
   two or three signals, respectively.

   In hardware, signals are assigned to one or more specific I/O T-periods, and
   some signals are asserted concurrently.  For example, a programmed STC sc,C
   instruction asserts the STC and CLF signals together in period T4.  Under
   simulation, signals are ORed to form an I/O cycle; in this example, the
   signal handler would receive an IOCYCLE value of "ioSTC | ioCLF".

   Hardware allows parallel action for concurrent signals.  Under simulation, a
   "concurrent" set of signals is processed sequentially by the signal handler
   in order of ascending numerical value.  Although assigned T-periods differ
   between programmed I/O and DMA I/O cycles, a single processing order is used.
   The order of execution generally follows the order of T-period assertion,
   except that ioSIR is processed after all other signals that may affect the
   interrupt request chain.

   Implementation notes:

    1. The ioCLF signal must be processed after ioSFS/ioSFC to ensure that a
       true skip test generates ioSKF before the flag is cleared, and after
       ioIOI/ioIOO/ioSTC/ioCLC to meet the requirement that executing an
       instruction having the H/C bit set is equivalent to executing the same
       instruction with the H/C bit clear and then a CLF instruction.

    2. The ioSKF signal is never sent to an I/O handler.  Rather, it is returned
       from the handler if the SFC or SFS condition is true.  If the condition
       is false, ioNONE is returned instead.  As the ioSKF value is returned in
       the upper 16 bits of the returned value, its assigned value must be >=
       200000 octal.

    3. An I/O handler will receive ioCRS as a result of a CLC 0 instruction,
       ioPOPIO and ioCRS as a result of a RESET command, and ioPON, ioPOPIO, and
       ioCRS as a result of a RESET -P command.

    4. An I/O handler will receive ioNONE when a HLT instruction is executed
       that has the H/C bit clear (i.e., no CLF generated).

    5. In hardware, the SIR signal is generated unconditionally every T5 period
       to time the setting of the IRQ flip-flop.  Under simulation, ioSIR
       indicates that the I/O handler must set the PRL, IRQ, and SRQ signals as
       required by the interface logic.  ioSIR must be included in the I/O cycle
       if any of the flip-flops affecting these signals are changed and the
       interface supports interrupts or DMA transfers.

    6. In hardware, the ENF signal is unconditionally generated every T2 period
       to time the setting of the flag flip-flop and to reset the IRQ flip-flop.
       If the flag buffer flip-flip is set, then flag will be set by ENF.  If
       the flag buffer is clear, ENF will not affect flag.  Under simulation,
       ioENF is sent to set the flag buffer and flag flip-flops.  For those
       interfaces where this action is identical to that provided by STF, the
       ioENF handler may simply fall into the ioSTF handler.

    7. In hardware, the PON signal is asserted continuously while the CPU is
       operating.  Under simulation, ioPON is asserted only at simulator
       initialization or when processing a RESET -P command.
*/

typedef enum { ioNONE  = 0000000,                       /* -- -- -- -- -- no signal asserted */
               ioPON   = 0000001,                       /* T2 T3 T4 T5 T6 power on normal */
               ioENF   = 0000002,                       /* T2 -- -- -- -- enable flag */
               ioIOI   = 0000004,                       /* -- -- T4 T5 -- I/O data input (CPU)
                                                           T2 T3 -- -- -- I/O data input (DMA) */
               ioIOO   = 0000010,                       /* -- T3 T4 -- -- I/O data output */
               ioSFS   = 0000020,                       /* -- T3 T4 T5 -- skip if flag is set */
               ioSFC   = 0000040,                       /* -- T3 T4 T5 -- skip if flag is clear */
               ioSTC   = 0000100,                       /* -- -- T4 -- -- set control flip-flop (CPU)
                                                           -- T3 -- -- -- set control flip-flop (DMA) */
               ioCLC   = 0000200,                       /* -- -- T4 -- -- clear control flip-flop (CPU)
                                                           -- T3 T4 -- -- clear control flip-flop (DMA) */
               ioSTF   = 0000400,                       /* -- T3 -- -- -- set flag flip-flop */
               ioCLF   = 0001000,                       /* -- -- T4 -- -- clear flag flip-flop (CPU)
                                                           -- T3 -- -- -- clear flag flip-flop (DMA) */
               ioEDT   = 0002000,                       /* -- -- T4 -- -- end data transfer */
               ioCRS   = 0004000,                       /* -- -- -- T5 -- control reset */
               ioPOPIO = 0010000,                       /* -- -- -- T5 -- power-on preset to I/O */
               ioIAK   = 0020000,                       /* -- -- -- -- T6 interrupt acknowledge */
               ioSIR   = 0040000,                       /* -- -- -- T5 -- set interrupt request */

               ioSKF   = 0200000 } IOSIGNAL;            /* -- T3 T4 T5 -- skip on flag */


typedef uint32 IOCYCLE;                                 /* a set of signals forming one I/O cycle */

#define IOIRQSET        (ioSTC | ioCLC | ioENF | \
                         ioSTF | ioCLF | ioIAK | \
                         ioCRS | ioPOPIO | ioPON)       /* signals that may affect interrupt state */


/* Flip-flops */

typedef enum {
    CLEAR = 0,                                  /* the flip-flop is clear */
    SET   = 1                                   /* the flip-flop is set */
    } FLIP_FLOP;

#define TOGGLE(ff)          ff = (FLIP_FLOP) (ff ^ 1)   /* toggle a flip-flop variable */

#define D_FF(b)             (FLIP_FLOP) ((b) != 0)      /* use a Boolean expression for a D flip-flop */


/* I/O structures.

   The Device Information Block (DIB) allows devices to be relocated in the
   machine's I/O space.  Each DIB contains a pointer to the device interface
   routine, a value corresponding to the location of the interface card in the
   CPU's I/O card cage (which determines the card's select code), and a card
   index if the interface routine services multiple cards.


   Implementation notes:

    1. The select_code and card_index fields could be smaller than the defined
       32-bit sizes, but IA-32 processors execute instructions with 32-bit
       operands much faster than those with 16- or 8-bit operands.

    2. The DIB_REGS macro provides hidden register entries needed to save and
       restore the state of a DIB.  Only the potentially variable fields are
       referenced.  In particular, the "io_interface" field must not be saved,
       as the address of the device's interface routine may change from version
       to version of the simulator.
*/

#define SC_MAX              077                 /* the maximum select code */
#define SC_MASK             077u                /* the mask for the select code */
#define SC_BASE             8                   /* the radix for the select code */

typedef struct dib DIB;                         /* an incomplete definition */

typedef uint32 IOHANDLER                        /* the I/O device interface function prototype */
    (DIB     *dibptr,                           /*   a pointer to the device information block */
     IOCYCLE signal_set,                        /*   a set of inbound signals */
     uint32  stat_data);                        /*   a 32-bit inbound value */

struct dib {                                    /* the Device Information Block */
    IOHANDLER *io_handler;                      /*   the device's I/O interface function pointer */
    uint32    select_code;                      /*   the device's select code (02-77) */
    uint32    card_index;                       /*   the card index if multiple interfaces are supported */
    };

#define DIB_REGS(dib) \
/*    Macro   Name     Location                    Width  Flags              */ \
/*    ------  -------  --------------------------  -----  -----------------  */ \
    { ORDATA (DIBSC,   dib.select_code,             32),  PV_LEFT | REG_HRO }


/* I/O signal and status macros.

   The following macros are useful in I/O signal handlers and unit service
   routines.  The parameter definition symbols employed are:

     I = an IOCYCLE value
     E = a t_stat error status value
     D = a uint16 data value
     C = a uint32 combined status and data value
     P = a pointer to a DIB structure
     B = a Boolean test value

   Implementation notes:

    1. The IONEXT macro isolates the next signal in sequence to process from the
       I/O cycle I.

    2. The IOADDSIR macro adds an ioSIR signal to the I/O cycle I if it
       contains signals that might change the interrupt state.

    3. The IORETURN macro forms the combined status and data value to be
       returned by a handler from the t_stat error code E and the 16-bit data
       value D.

    4. The IOSTATUS macro isolates the t_stat error code from a combined status
       and data value value C.

    5. The IODATA macro isolates the 16-bit data value from a combined status
       and data value value C.

    6. The IOPOWERON macro calls signal handler P->H with DIB pointer P to
       process a power-on reset action.

    7. The IOPRESET macro calls signal handler P->H with DIB pointer P to
       process a front-panel PRESET action.

    8. The IOERROR macro returns t_stat error code E from a unit service routine
       if the Boolean test B is true.
*/

#define IONEXT(I)       (IOSIGNAL) ((I) & ~(I) + 1)                         /* extract next I/O signal to handle */
#define IOADDSIR(I)     ((I) & IOIRQSET ? (I) | ioSIR : (I))                /* add SIR if IRQ state might change */

#define IORETURN(E,D)   ((uint32) ((E) << D16_WIDTH | (D) & D16_MASK))      /* form I/O handler return value */
#define IOSTATUS(C)     ((t_stat) ((C) >> D16_WIDTH) & D16_MASK)            /* extract I/O status from combined value */
#define IODATA(C)       ((uint16) ((C) & D16_MASK))                         /* extract data from combined value */

#define IOPOWERON(P)    (P)->io_handler ((P), ioPON | ioPOPIO | ioCRS, 0)   /* send power-on signals to handler */
#define IOPRESET(P)     (P)->io_handler ((P), ioPOPIO | ioCRS, 0)           /* send PRESET signals to handler */
#define IOERROR(B,E)    ((B) ? (E) : SCPE_OK)                               /* stop on I/O error if enabled */


/* I/O signal logic macros.

   The following macros implement the logic for the SKF, PRL, IRQ, and SRQ
   signals.  Both standard and general logic macros are provided.  The parameter
   definition symbols employed are:

     S = a uint32 select code value
     B = a Boolean test value
     N = a name of a structure containing the standard flip-flops

   Implementation notes:

    1. The setSKF macro sets the Skip on Flag signal in the return data value if
       the Boolean value B is true.

    2. The setPRL macro sets the Priority Low signal for select code S to the
       Boolean value B.

    3. The setIRQ macro sets the Interrupt Request signal for select code S to
       the Boolean value B.

    4. The setSRQ macro sets the Service Request signal for select code S to the
       Boolean value B.

    5. The PRL macro returns the Priority Low signal for select code S as a
       Boolean value.

    6. The IRQ macro returns the Interrupt Request signal for select code S as a
       Boolean value.

    7. The SRQ macro returns the Service Request signal for select code S as a
       Boolean value.

    8. The setstdSKF macro sets Skip on Flag signal in the return data value if
       the flag state in structure N matches the current skip test condition.

    9. The setstdPRL macro sets the Priority Low signal for the select code
       referenced by "dibptr" using the standard logic and the control and flag
       states in structure N.

   10. The setstdIRQ macro sets the Interrupt Request signal for the select code
       referenced by "dibptr" using the standard logic and the control, flag,
       and flag buffer states in structure N.

   11. The setstdSRQ macro sets the Service Request signal for the select code
       referenced by "dibptr" using the standard logic and the flag state in
       structure N.
*/

#define BIT_V(S)        ((S) & 037)                                     /* convert select code to bit position */
#define BIT_M(S)        (1u << BIT_V (S))                               /* convert select code to bit mask */

#define setSKF(B)       stat_data = ((B) ? ioSKF : ioNONE)

#define setPRL(S,B)     dev_prl[(S)/32] = dev_prl[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))
#define setIRQ(S,B)     dev_irq[(S)/32] = dev_irq[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))
#define setSRQ(S,B)     dev_srq[(S)/32] = dev_srq[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))

#define PRL(S)          ((dev_prl[(S)/32] >> BIT_V (S)) & 1)
#define IRQ(S)          ((dev_irq[(S)/32] >> BIT_V (S)) & 1)
#define SRQ(S)          ((dev_srq[(S)/32] >> BIT_V (S)) & 1)

#define setstdSKF(N)    setSKF ((signal == ioSFC) && !N.flag || \
                                (signal == ioSFS) && N.flag)

#define setstdPRL(N)    setPRL (dibptr->select_code, !(N.control & N.flag));
#define setstdIRQ(N)    setIRQ (dibptr->select_code, N.control & N.flag & N.flagbuf);
#define setstdSRQ(N)    setSRQ (dibptr->select_code, N.flag);


/* Bitset formatting.

   See the comments at the "fmt_bitset" function (hp2100_sys.c) for details of
   the specification of bitset names and format structures.
*/

typedef enum {                                  /* direction of interpretation */
    msb_first,                                  /*   left-to-right */
    lsb_first                                   /*   right-to-left */
    } BITSET_DIRECTION;

typedef enum {                                  /* alternate names */
    no_alt,                                     /*   no alternates are present in the name array */
    has_alt                                     /*   the name array contains alternates */
    } BITSET_ALTERNATE;

typedef enum {                                  /* trailing separator */
    no_bar,                                     /*   omit a trailing separator */
    append_bar                                  /*   append a trailing separator */
    } BITSET_BAR;

typedef const char *const BITSET_NAME;          /* a bit name string pointer */

typedef struct {                                /* bit set format descriptor */
    uint32            name_count;               /*   count of bit names */
    BITSET_NAME       *names;                   /*   pointer to an array of bit names */
    uint32            offset;                   /*   offset from LSB to first bit */
    BITSET_DIRECTION  direction;                /*   direction of interpretation */
    BITSET_ALTERNATE  alternate;                /*   alternate interpretations presence */
    BITSET_BAR        bar;                      /*   trailing separator choice */
    } BITSET_FORMAT;

/* Bitset format specifier initialization */

#define FMT_INIT(names,offset,dir,alt,bar) \
          sizeof (names) / sizeof (names) [0], \
          (names), (offset), (dir), (alt), (bar)


/* System interface global data structures */

extern const HP_WORD odd_parity [256];          /* a table of parity bits for odd parity */


/* System interface global SCP support routines declared in scp.h

extern t_stat sim_load   (FILE *fptr, CONST char *cptr, CONST char *fnam, int flag);
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val, UNIT *uptr, int32 sw);
extern t_stat parse_sym  (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw);
*/

/* System interface global SCP support routines */

extern t_stat hp_attach   (UNIT *uptr, CONST char *cptr);
extern t_stat hp_set_dib  (UNIT *uptr, int32 count, CONST char *cptr, void *desc);
extern t_stat hp_show_dib (FILE *st, UNIT *uptr, int32 count, CONST void *desc);


/* System interface global utility routines */

extern t_stat fprint_cpu (FILE *ofile, t_addr addr, t_value *val, uint32 radix, SYMBOL_SOURCE source);

extern const char *fmt_char   (uint32 charval);
extern const char *fmt_bitset (uint32 bitset, const BITSET_FORMAT bitfmt);

extern void   hp_trace           (DEVICE *dptr, uint32 flag, ...);
extern t_bool hp_device_conflict (void);
extern void   hp_enbdis_pair     (DEVICE *ccptr, DEVICE *dcptr);


/* I/O state */

extern uint32 dev_prl [2], dev_irq [2], dev_srq [2];    /* I/O signal vectors */


/* Device-specific functions */

extern int32 sync_poll (POLLMODE poll_mode);
