/* hp3000_mpx.c: HP 3000 30036B Multiplexer Channel simulator

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

   MPX          HP 3000 Series III Multiplexer Channel

   24-Oct-16    JDB     Renamed SEXT macro to SEXT16
   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   15-Jul-16    JDB     Fixed the word count display for DREADSTB trace
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   07-Jun-16    JDB     Corrected ACKSR assertion in State A for chained orders
   16-May-16    JDB     abort_channel parameter is now a pointer-to-constant
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   06-Oct-15    JDB     First release version
   11-Sep-14    JDB     Passes the multiplexer channel diagnostic (D422A)
   10-Feb-13    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - HP 3000 Series III Reference/Training Manual
         (30000-90143, February 1980)
     - 30035A Multiplexer Channel Maintenance Manual
         (30035-90001, September 1972)
     - Stand-Alone HP 30036A/B Multiplexer Channel Diagnostic
         (30036-90001, July 1978)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, Apr-1980)


   The HP 30036B Multiplexer Channel provides high-speed data transfer between
   from one to sixteen devices and main memory.  Concurrent transfers for
   multiple devices are multiplexed on a per-word basis, dependent on the
   service request priorities assigned to the participating interfaces.
   Interfaces must have additional hardware to be channel-capable, as the
   channel uses separate control and data signals from those used for direct
   I/O.  In addition, the multiplexer and selector channels differ somewhat in
   their use of the signals, so interfaces are generally designed for use with
   one or the other (the Selector Channel Maintenance Board is a notable
   exception that uses jumpers to indicate which channel to use).

   The transfer rate of the Series III multiplexer channel is poorly documented.
   Various rates are quoted in different publications: a uniform 990 KB/second
   rate in one, a 1038 KB/second inbound rate and a 952 KB/second outbound rate
   in another.  Main memory access time is given as 300 nanoseconds, and the
   cycle time is 700 nanoseconds.  The multiplexer channel passes data to and
   from main memory via the I/O Processor.

   Once started by an SIO instruction, the channel executes I/O programs
   independently of the CPU.  Program words are read, and device status is
   written back, by calls to the I/O Processor.

   32-bit I/O program words are formed from a 16-bit I/O control word (IOCW) and
   a 16-bit I/O address word (IOAW) in this general format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | C |   order   | X |       control word 1/word count           |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 control word 2/status/address                 |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Most orders are fully decoded by bits 1-3, but a few use bit 4 to extend the
   definition where bits 4-15 are not otherwise used.  I/O programs always
   reside in memory bank 0.  The current I/O program pointer resides in word 0
   of the Device Reference Table entry for the active interface.

   The Jump and Jump Conditional orders use this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 0   0   0 | C | -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      jump target address                      |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   ...where C is 0 for an unconditional jump and 1 for a conditional jump.  An
   unconditional jump is handled entirely within the channel.  A conditional
   jump asserts the SETJMP signal to the interface.  If the interface returns
   JMPMET, the jump will occur; otherwise, execution continues with the next
   program word.

   The Return Residue order uses this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 0   0   1   0 | -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     residue of word count                     |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The remaining word count from the last transfer will be returned in the IOAW
   as a two's-complement value.  If the transfer completed normally, the
   returned value will be zero.

   The Set Bank order uses this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 0   0   1   1 | -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   - |     bank      |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   This establishes the memory bank to be used for subsequent Write or Read
   orders.  Program addresses always use bank 0.

   The Interrupt order uses this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 0   1   0 | -   -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The SETINT signal is asserted to the interface for this order.

   The End and End with Interrupt orders use this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 0   1   1 | I | -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         device status                         |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   ...where I is 0 for an End and 1 for an End with Interrupt.  The PSTATSTB
   signal is asserted to the interface to obtain the device status, which is
   stored in the IOAW location.  If the I bit is set, SETINT will also be
   asserted,

   The Control order uses this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 1   0   0 |                control word 1                 |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        control word 2                         |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Both control words are sent to the interface.  The full IOCW containing
   control word 1 is sent with the PCMD1 signal asserted.  It is followed by the
   IOAW with PCONTSTB asserted.

   The Sense order uses this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 1   0   1 | -   -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         device status                         |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The PSTATSTB signal is asserted to the interface to obtain the device status,
   which is stored in the IOAW location.

   The Write and Read orders use these formats:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | C | 1   1   0 |         negative word count to write          |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | C | 1   1   1 |          negative word count to read          |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       transfer address                        |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The C bit is the "data chain" flag.  If it is set, then this transfer is a
   continuation of a previous Write or Read transfer.  This is used to
   circumvent the transfer size limitation inherent in the 12-bit word count
   allocated in the IOCW.  For single transfers larger than 4K words, multiple
   contiguous Write or Read orders are used, with all but the last order having
   their data chain bits set.

   In simulation, IOCW bits 1-4 are used to index into a 16-element lookup table
   to produce the final I/O order (because some of the orders define IOCW bit 4
   as "don't care", there are only thirteen distinct orders).

   Channel-capable interfaces connect via the multiplexer channel bus and
   request channel service by asserting one of the sixteen Service Request
   signals (SR0 through SR15).  Jumpers on the interface establish which SR
   number to use.  When multiple devices request service simultaneously, the
   channel grants access to the lowest-numbered request.

   In simulation, an interface is connected to the channel by setting the
   "service_request" field in the DIB to a value between 0 and 15, representing
   the SR number signal to assert.  If the field is set to the SRNO_UNUSED
   value, then it is not connected to the channel.


   The channel contains a diagnostic interface that provides the capability to
   check the operation independently of channel program execution.  The
   interface responds to direct I/O instructions, as follows:

   Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | - |  RAM address  | A | O | S | L | I | -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M  = master reset
     A  = select the Address RAM and Register
     O  = select the Order RAM and Register
     S  = select the State RAM and Register
     L  = load the registers from the RAMs during the next read
     I  = increment the Address or Word Count Registers after the next read

   The control word establishes the address and enable(s) to read or write from
   a given RAM location.  The RAM address is stored in the control word register
   and is used in lieu of the service request encoding whenever an I/O order
   references the multiplexer device number, effectively providing a
   programmable service request number.  The A/O/S/L/I bits enable the
   corresponding actions for the next WIO or RIO instruction.


   Status Word Format (TIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | - | E |  RAM address  | -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     Key:

       S = SIO OK (always 0)
       D = direct read/write I/O OK (always 1)
       E = a state parity error exists

   A state parity error occurs when the state register contains a value other
   than one of the four defined states.  An error causes the RAM address and E
   bit to be stored in the error register, which is then gated to form the
   status return value.  The error register is cleared by an IORESET or master
   reset.


   Write Word Format (WIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            address                            |  Address RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     order     |                  word count                   |  Order RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   - | A | B | C | D | -   -   -   -   -   - |  bank number  |  State RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     A = set the state to State A
     B = set the state to State B
     C = set the state to State C
     D = set the state to State D

   The address, order, or state RAM value is written to the specified register
   and RAM address set by the last control word.  If multiple registers/RAMs
   were selected, then the value is written to all of them.

   Setting more than one state bit at a time will generate a state parity error.


   Read Word Format (RIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            address                            |  Address RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     order     |                  word count                   |  Order RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   - |  bank number  | T | A | B | C | D | E | P | S |  State RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     T = the transfer complete flip-flop value
     A = the state is State A
     B = the state is State B
     C = the state is State C
     D = the state is State D
     E = the end-of-transfer flip-flop value
     P = address parity (odd parity for the address register)
     S = a state parity error exists

   The diagnostic tests address parity and state parity.  State parity also
   asserts the XFERERROR signal, which aborts a transfer in progress.


   Implementation notes:

    1. The multiplexer channel must execute more than one I/O order per CPU
       instruction in order to meet the timing requirements of the diagnostic.
       The timing is modeled by establishing a count of channel clock pulses at
       poll entry and then executing orders until the count is exhausted.  If
       the clock count was exceeded, the excess count is saved and then
       subtracted from the next entry's count, so that the typical execution
       time is preserved over a number of entries.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_io.h"
#include "hp3000_mem.h"



/* IOP device */

extern DEVICE iop_dev;                          /* I/O Processor */


/* Memory access macros */

#define iop_read_memory(c,o,v)      mem_read  (&iop_dev, c, o, v)
#define iop_write_memory(c,o,v)     mem_write (&iop_dev, c, o, v)


/* Program constants.

   The multiplexer channel clock period is 175 nanoseconds.  The channel runs
   concurrently with the CPU, which executes instructions in an average of
   2.57 microseconds, so multiple cycles are executed per CPU instruction.

   In simulation, the channel is called from the instruction execution loop
   after every instruction, and sometimes additionally within instructions that
   have long execution times (e.g., MOVE).  The number of event ticks that have
   elapsed since the last call are passed to the channel; this determines the
   number of channel cycles to execute.


   Implementation notes:

    1. The number of cycles consumed by the channel for various operations are
       educated guesses.  There is no documentation available that details the
       cycle timing.

    2. The MPX_STATE values match the values supplied in bits 2-5 of the "write
       state RAM" command.

    3. State "parity" is 1 for an illegal state and 0 for a valid state.
*/

#define INTRF_COUNT         (SRNO_MAX + 1)      /* count of interfaces handled by the multiplexer channel */

#define NS_PER_CYCLE        175                 /* each clock cycle is 175 nanoseconds */

#define CYCLES_PER_STATE    2
#define CYCLES_PER_READ     9
#define CYCLES_PER_WRITE    9

#define CYCLES_PER_EVENT    (int32) (USEC_PER_EVENT * 1000 / NS_PER_CYCLE)


typedef enum {                                  /* multiplexer channel sequencer states */
    State_Idle = 000,
    State_D    = 001,
    State_C    = 002,
    State_B    = 004,
    State_A    = 010
    } MPX_STATE;

static const char *const state_name [16] = {    /* indexed by MPX_STATE */
    "Idle State",
    "State D",
    "State C",
    "invalid state 0011",
    "State B",
    "invalid state 0101",
    "invalid state 0110",
    "invalid state 0111",
    "State A",
    "invalid state 1001",
    "invalid state 1010",
    "invalid state 1011",
    "invalid state 1100",
    "invalid state 1101",
    "invalid state 1110",
    "invalid state 1111"
    };

static const uint8 state_parity [16] = {        /* State RAM parity */
    1, 0, 0, 1,                                 /*   0000, 0001, 0010, 0011 */
    0, 1, 1, 1,                                 /*   0100, 0101, 0110, 0111 */
    0, 1, 1, 1,                                 /*   1000, 1001, 1010, 1011 */
    1, 1, 1, 1                                  /*   1100, 1101, 1110, 1111 */
    };


/* Debug flags */

#define DEB_CSRW        (1u << 0)               /* trace diagnostic and channel command initiations and completions */
#define DEB_PIO         (1u << 1)               /* trace programmed I/O commands */
#define DEB_IOB         (1u << 2)               /* trace I/O bus signals and data words */
#define DEB_STATE       (1u << 3)               /* trace state changes */
#define DEB_SR          (1u << 4)               /* trace service requests */


/* Control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | - |  RAM address  | A | O | S | L | I | -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* (M) master reset */
#define CN_RAM_ADDR_MASK    0036000u            /* RAM address mask */
#define CN_ADDR_RAM         0001000u            /* (A) select the address RAM and register */
#define CN_ORDER_RAM        0000400u            /* (O) select the order RAM and register */
#define CN_STATE_RAM        0000200u            /* (S) select the state RAM and register */
#define CN_LOAD_REGS        0000100u            /* (L) load registers from RAM */
#define CN_INCR_REGS        0000040u            /* (I) increment registers */

#define CN_RAM_ADDR_SHIFT   10                  /* RAM address alignment shift */

#define CN_RAM_ADDR(c)      (((c) & CN_RAM_ADDR_MASK) >> CN_RAM_ADDR_SHIFT)

static const BITSET_NAME control_names [] = {   /* Control word names */
    "master reset",                             /*   bit  0 */
    NULL,                                       /*   bit  1 */
    NULL,                                       /*   bit  2 */
    NULL,                                       /*   bit  3 */
    NULL,                                       /*   bit  4 */
    NULL,                                       /*   bit  5 */
    "address RAM",                              /*   bit  6 */
    "order RAM",                                /*   bit  7 */
    "state RAM",                                /*   bit  9 */
    "load registers",                           /*   bit  9 */
    "increment registers"                       /*   bit 10 */
    };

static const BITSET_FORMAT control_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (control_names, 5, msb_first, no_alt, append_bar) };


/* Status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | D | - | E |  RAM address  | -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_DIO_OK           0040000u            /* (D) direct I/O OK (always set) */
#define ST_STATE_PARITY     0010000u            /* (E) a state error exists */
#define ST_RAM_ADDR_MASK    0007400u            /* RAM address mask */

#define ST_RAM_ADDR_SHIFT   8                   /* RAM address alignment shift */

#define ST_RAM_ADDR(c)      ((c) << ST_RAM_ADDR_SHIFT & ST_RAM_ADDR_MASK)

#define ST_TO_RAM_ADDR(s)   (((s) & ST_RAM_ADDR_MASK) >> ST_RAM_ADDR_SHIFT)

static const BITSET_NAME status_names [] = {    /* Status word names */
    "DIO OK",                                   /*   bit  1 */
    NULL,                                       /*   bit  2 */
    "state error"                               /*   bit  3 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 12, msb_first, no_alt, append_bar) };


/* Write word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            address                            |  Address RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     order     |                  word count                   |  Order RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   - | A | B | C | D | -   -   -   -   -   - |  bank number  |  State RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define WR_ORDER_MASK       0170000u            /* order mask */
#define WR_COUNT_MASK       0007777u            /* word count mask */
#define WR_STATE_MASK       0036000u            /* state mask */
#define WR_BANK_MASK        0000017u            /* bank number mask */

#define WR_ORDER_SHIFT      12                  /* order alignment shift */
#define WR_COUNT_SHIFT      0                   /* word count alignment shift */
#define WR_STATE_SHIFT      10                  /* state alignment shift */
#define WR_BANK_SHIFT       0                   /* bank number alignment shift */

#define WR_ORDER(c)         (((c) & WR_ORDER_MASK) >> WR_ORDER_SHIFT)
#define WR_COUNT(c)         (((c) & WR_COUNT_MASK) >> WR_COUNT_SHIFT)
#define WR_STATE(c)         (((c) & WR_STATE_MASK) >> WR_STATE_SHIFT)
#define WR_BANK(c)          (((c) & WR_BANK_MASK)  >> WR_BANK_SHIFT)


/* Read word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            address                            |  Address RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     order     |                  word count                   |  Order RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   - |  bank number  | T | A | B | C | D | E | P | S |  State RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define RD_ADDR_MASK        0177777u            /* address mask */
#define RD_ORDER_MASK       0170000u            /* order mask */
#define RD_COUNT_MASK       0007777u            /* word count mask */
#define RD_BANK_MASK        0007400u            /* bank number mask */
#define RD_XFER_COMPLETE    0000200u            /* (T) transfer complete */
#define RD_STATE_MASK       0000170u            /* (A/B/C/D) state mask */
#define RD_XFER_END         0000004u            /* (E) end of transfer */
#define RD_ADDR_PARITY      0000002u            /* (P) address parity */
#define RD_STATE_PARITY     0000001u            /* (S) state parity */

#define RD_ORDER_SHIFT      12                  /* order alignment shift */
#define RD_COUNT_SHIFT      0                   /* word count alignment shift */
#define RD_BANK_SHIFT       8                   /* bank number alignment shift */
#define RD_STATE_SHIFT      3                   /* state alignment shift */

#define RD_ORDER(c)         ((c) << RD_ORDER_SHIFT & RD_ORDER_MASK)
#define RD_COUNT(c)         ((c) << RD_COUNT_SHIFT & RD_COUNT_MASK)
#define RD_BANK(c)          ((c) << RD_BANK_SHIFT  & RD_BANK_MASK)
#define RD_STATE(c)         ((c) << RD_STATE_SHIFT & RD_STATE_MASK)

#define RD_SIO_ORDER(o,c)   IOCW_ORDER ((o) << RD_ORDER_SHIFT | (c) & RD_COUNT_MASK)

static const BITSET_NAME read_names [] = {      /* Read word names */
    "terminal count",                           /*   bit  8 */
    "A",                                        /*   bit  9 */
    "B",                                        /*   bit 10 */
    "C",                                        /*   bit 11 */
    "D",                                        /*   bit 12 */
    "end of transfer",                          /*   bit 13 */
    "address parity",                           /*   bit 14 */
    "state parity"                              /*   bit 15 */
    };

static const BITSET_FORMAT read_format =        /* names, offset, direction, alternates, bar */
    { FMT_INIT (read_names, 0, msb_first, no_alt, append_bar) };


/* Channel RAMs.

   In hardware, control information for a transfer-in-progress is stored in one
   of sixteen RAM locations, corresponding the to assigned service request
   number.  The RAM is 42 bits wide, partitioned as follows:

     - a 4-bit state RAM
     - a 6-bit auxiliary RAM
     - a 16-bit address RAM
     - a 16-bit order RAM

   In simulation, the 16-bit order RAM is split into a 5-bit order RAM and a
   12-bit counter RAM.  The order RAM stores the Data Chain bit and the four-bit
   translated SIO order, rather than the DC and three-bit basic channel order.
   This allows direct interpretation of the I/O order, rather than sometimes
   depending on the leading bit of the counter RAM.

   Values within the RAMs are formatted as follows:

       0   1 | 2   3   4 | 5   6   7
     +---+---+---+---+---+---+---+---+
     | -   -   -   - |     state     |  State RAM
     +---+---+---+---+---+---+---+---+
     | -   - | B | T |     bank      |  Auxiliary RAM
     +---+---+---+---+---+---+---+---+
     | -   -   - | C |     order     |  Order RAM
     +---+---+---+---+---+---+---+---+

   Where:

     B = the transfer is within a block
     T = the terminal word count has been reached
     C = the I/O order specifies data chaining

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   - |                  word count                   |  Counter RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            address                            |  Address RAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define AUX_IB              040u                /* auxiliary RAM in-block flag */
#define AUX_TC              020u                /* auxiliary RAM terminal count flag */
#define AUX_BANK_MASK       017u                /* auxiliary RAM bank mask */

#define AUX_BANK(r)         ((r) & AUX_BANK_MASK)

#define ORDER_DC            020u                /* order RAM data chain flag */
#define ORDER_MASK          017u                /* order RAM current order mask */

#define CNTR_MASK           0007777u            /* counter RAM word count mask */
#define CNTR_MAX            0007777u            /* counter RAM word count maximum value */

static const BITSET_NAME aux_names [] = {       /* Auxiliary RAM word */
    "in block",                                 /* bit  2 */
    "terminal count"                            /* bit  3 */
    };

static const BITSET_FORMAT aux_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (aux_names, 4, msb_first, no_alt, append_bar) };


/* Channel global state */

t_bool mpx_is_idle     = TRUE;                  /* TRUE if the multiplexer channel is idle */
uint32 mpx_request_set = 0;                     /* set of service request bits */


/* Channel local state */

static DIB    *srs [INTRF_COUNT];               /* indexed by service request number for channel requests */
static uint32 active_count  = 0;                /* count of active transfers */
static  int32 excess_cycles = 0;                /* count of cycles in excess of allocation */

static HP_WORD   control_word = 0;              /* diagnostic control word */
static HP_WORD   status_word  = 0;              /* diagnostic status word */
static FLIP_FLOP rollover     = CLEAR;          /* SET if the transfer word count rolls over */
static FLIP_FLOP device_end   = CLEAR;          /* SET if DEVEND is asserted by the device */


/* Channel per-interface state.

   The per-interface state for a transfer-in-progress is stored in the RAM
   location corresponding to the interface's assigned service request number.
   The RAM values are loaded into registers at the start of a channel I/O cycle
   and stored back into the RAM at the end of the cycle.


   Implementation notes:

    1. SCP requires that arrayed register elements be sized to match their width
       in bits.  We want to display multiplexer state RAM entries as four-bit
       values, so state_ram must have 8-bit elements.  However, because the
       MPX_STATE enum size is implementation-dependent, state_ram cannot be of
       type MPX_STATE.
*/

static uint8   state_ram [INTRF_COUNT];         /* state RAM */
static uint8   aux_ram   [INTRF_COUNT];         /* auxiliary RAM */
static uint8   order_ram [INTRF_COUNT];         /* I/O order RAM */
static HP_WORD cntr_ram  [INTRF_COUNT];         /* counter RAM */
static HP_WORD addr_ram  [INTRF_COUNT];         /* I/O address RAM */

static uint8   state_reg;                       /* state register */
static uint8   aux_reg;                         /* auxiliary register */
static uint8   order_reg;                       /* order register */
static HP_WORD cntr_reg;                        /* word counter register */
static HP_WORD addr_reg;                        /* address register */


/* Channel local SCP support routines */

static CNTLR_INTRF mpx_interface;
static t_stat      mpx_reset     (DEVICE *dptr);


/* Channel local utility routines */

static uint8        next_state    (uint8 current_state, SIO_ORDER order, t_bool abort);
static void         end_channel   (DIB   *dibptr);
static SIGNALS_DATA abort_channel (DIB   *dibptr, const char *reason);


/* Channel SCP data structures */


/* Device information block */

static DIB mpx_dib = {
    &mpx_interface,                             /* device interface */
    127,                                        /* device number */
    SRNO_UNUSED,                                /* service request number */
    INTPRI_UNUSED,                              /* interrupt priority */
    INTMASK_UNUSED                              /* interrupt mask */
    };


/* Unit list */

static UNIT mpx_unit [] = {                     /* a dummy unit to satisfy SCP requirements */
    { UDATA (NULL, 0, 0) }
    };


/* Register list.


   Implementation notes:

    1. The "mpx_request_set" and "srs" variables need not be SAVEd or RESTOREd,
       as they are rebuilt during the instruction execution prelude.

    2. The state RAM register array cannot be named "STATE", because SCP uses
       "STATE" to display all of the registers, and it checks the keyword before
       checking for a register of the same name.
*/

static REG mpx_reg [] = {
/*    Macro   Name    Location       Radix  Width     Depth           Flags        */
/*    ------  ------  -------------  -----  -----  -----------  -----------------  */
    { FLDATA (IDLE,   mpx_is_idle,            0)                                   },
    { DRDATA (COUNT,  active_count,          32),               PV_LEFT            },
    { DRDATA (EXCESS, excess_cycles,         32),               PV_LEFT            },

    { ORDATA (CNTL,   control_word,          16),               REG_FIT            },
    { ORDATA (STAT,   status_word,           16),               REG_FIT            },
    { FLDATA (ROLOVR, rollover,               0)                                   },
    { FLDATA (DEVEND, device_end,             0)                                   },

    { BRDATA (STATR,  state_ram,       2,     4,   INTRF_COUNT)                    },
    { BRDATA (AUX,    aux_ram,         8,     6,   INTRF_COUNT)                    },
    { BRDATA (ORDER,  order_ram,       8,     4,   INTRF_COUNT)                    },
    { BRDATA (CNTR,   cntr_ram,        8,    12,   INTRF_COUNT)                    },
    { BRDATA (ADDR,   addr_ram,        8,    16,   INTRF_COUNT)                    },

    { ORDATA (STAREG, state_reg,              8),               REG_FIT | REG_HRO  },
    { ORDATA (AUXREG, aux_reg,                8),               REG_FIT | REG_HRO  },
    { ORDATA (ORDREG, order_reg,              8),               REG_FIT | REG_HRO  },
    { ORDATA (CTRREG, cntr_reg,              16),               REG_FIT | REG_HRO  },
    { ORDATA (ADRREG, addr_reg,              16),               REG_FIT | REG_HRO  },

      DIB_REGS (mpx_dib),

    { NULL }
    };


/* Modifier list */

static MTAB mpx_mod [] = {
/*    Entry Flags  Value      Print String  Match String  Validation   Display       Descriptor        */
/*    -----------  ---------  ------------  ------------  -----------  ------------  ----------------- */
    { MTAB_XDV,    VAL_DEVNO, "DEVNO",      "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &mpx_dib },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB mpx_deb [] = {
    { "CSRW",  DEB_CSRW  },                     /* channel control, status, read, and write actions */
    { "PIO",   DEB_PIO   },                     /* programmed I/O commands executed */
    { "STATE", DEB_STATE },                     /* channel state changes executed */
    { "SR",    DEB_SR    },                     /* service requests received */
    { "IOBUS", DEB_IOB   },                     /* interface I/O bus signals and data words */
    { NULL,    0         }
    };


/* Device descriptor */

DEVICE mpx_dev = {
    "MPX",                                      /* device name */
    mpx_unit,                                   /* unit array */
    mpx_reg,                                    /* register array */
    mpx_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    DV_WIDTH,                                   /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mpx_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mpx_dib,                                   /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    mpx_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Channel global routines */



/* Initialize the channel.

   This routine is called in the CPU instruction execution prelude to allow the
   service request numbers of interfaces to be reassigned.  It sets up the "srs"
   DIB pointer array and the "mpx_request_set" bit vector from the service
   request values in the device DIBs.

   The "srs" dispatch table is used to send signals to the interfaces that
   request service by asserting their SR numbers.  The request set contains the
   set of interfaces currently requesting channel service.
*/

void mpx_initialize (void)
{
uint32 idx;
DIB    *dibptr;
const  DEVICE *dptr;

mpx_request_set = 0;                                    /* set all requests inactive */

memset (srs, 0, sizeof srs);                            /* clear the service requests table */

for (idx = 0; sim_devices [idx] != NULL; idx++) {       /* loop through the device table */
    dptr = sim_devices [idx];                           /* get the device pointer */
    dibptr = (DIB *) dptr->ctxt;                        /*   and the associated DIB pointer */

    if (dibptr                                              /* if an interface handler exists */
      && !(dptr->flags & DEV_DIS)                           /*   and the device is enabled */
      && dibptr->service_request_number != SRNO_UNUSED) {   /*     and it is connected to the multiplexer channel */
        srs [dibptr->service_request_number] = dibptr;      /*       then set the DIB pointer into the dispatch table */

        if (dibptr->service_request)                    /* if the controller has asserted its service request line */
            mpx_request_set |=                          /*   then set the associated request bit */
              1u << dibptr->service_request_number;
        }
    }

return;
}


/* Start an I/O program.

   This routine is called by a device interface in response to a Start I/O (SIO)
   instruction to request that the multiplexer channel begin an I/O program.  It
   corresponds in hardware to asserting the REQ signal.

   On entry, the service request number from the device's DIB is used as the RAM
   index.  The state RAM entry corresponding to the SR number is set to State C,
   and the other RAM entries are cleared.  The count of active I/O programs is
   incremented.


   Implementation notes:

    1. Setting "excess_cycles" to the negative number of cycles per event
       effectively doubles the available state execution time of the first
       multiplexer poll.  This is necessary to pass the Stand-Alone HP 30115A
       (7970B/E) Magnetic Tape Diagnostic (D433) steps 252, 255, 260, and 263,
       which check for command rejects.  The diagnostic does an SIO / BNE / TIO
       sequence and expects reject status to be set.  However, the two available
       multiplexer state execution opportunities (between the instructions) are
       insufficient to execute the C, A, and B states that are necessary for the
       tape controller to reject the command.  We therefore lengthen the first
       opportunity, so that all three states are completed before the TIO
       instruction checks for command reject.
*/

void mpx_assert_REQ (DIB *dibptr)
{
const uint32 srn = dibptr->service_request_number;      /* get the SR number for the RAM index */

dprintf (mpx_dev, DEB_CSRW, "Device number %u asserted REQ for channel initialization\n",
         dibptr->device_number);

state_ram [srn] = State_C;                              /* set up the initial sequencer state */
aux_ram   [srn] = 0;                                    /* clear */
order_ram [srn] = sioEND;                               /*   the rest */
cntr_ram  [srn] = 0;                                    /*     of the RAM */
addr_ram  [srn] = 0;                                    /*       entries */

excess_cycles = - CYCLES_PER_EVENT;                     /* preset the excess cycle count */

mpx_is_idle = FALSE;                                    /* indicate that the channel is busy */
active_count = active_count + 1;                        /* bump reference counter */

return;
}


/* Request channel service.

   This routine is called by a device interface to request service from the
   channel.  It is called either directly by the interface or indirectly by the
   IOP in response to an SRn signal returned by the interface.  A direct call is
   needed for asynchronous assertion, e.g., in response to an event service
   call.  Synchronous assertion, i.e., in response to an interface call, is made
   by returning the SRn signal to the IOP.  The routine corresponds in hardware
   to asserting the SRn signal associated with the interface to the multiplexer.

   On entry, the service_request field in the device's DIB is set to TRUE, and
   the request set bit corresponding the service_request_number field in the DIB
   is set.  This enables the channel to service the interface on the next
   multiplexer poll call, assuming that the interface has priority.
*/

void mpx_assert_SRn (DIB *dibptr)
{
if (dibptr->service_request == FALSE)
    dprintf (mpx_dev, DEB_SR, "Device number %u asserted SR%u\n",
             dibptr->device_number, dibptr->service_request_number);

dibptr->service_request = TRUE;                         /* set the service request flag */
mpx_request_set |= 1 << dibptr->service_request_number; /*   and the associated request bit */

return;
}


/* Poll the interfaces on the multiplexer channel bus for service requests.

   This routine is called in the CPU instruction execution loop to service a
   request from the highest-priority device interface.  It corresponds in
   hardware to asserting HSREQ to the IOP, receiving the DATAPOLL IN signal from
   the IOP, and then denying DATAPOLL OUT to the next multiplexer channel in the
   chain.  It executes one or more channel cycles for the associated device
   interface and resets the service request flag in the DIB.

   The multiplexer channel clock period is 175 nanoseconds.  The channel runs
   concurrently with the CPU, which executes instructions in an average of
   2.57 microseconds, so multiple cycles are executed per CPU instruction.

   This routine is called after every instruction, and sometimes additionally
   within instructions that have long execution times (e.g., MOVE).  The number
   of event ticks that have elapsed since the last call are passed in; this
   determines the number of channel cycles available to execute.

   In hardware, the multiplexer priority-encodes the 16 service request lines,
   selecting the highest-priority request for servicing.  In simulation, a
   service request sets the request set bit corresponding to the SR number.
   When a poll is performed, the device corresponding to the highest-priority
   (lowest-order) bit will be the recipient of the current multiplexer channel
   cycles.

   On entry, the routine determines the highest-priority interface that is
   requesting service and then executes the next state in the transfer for that
   interface, based on the values in the RAM.  The number of multiplexer clock
   counts consumed for the specified state execution is subtracted from the
   number of clock counts available.  If more time remains, and one or more
   service requests are still active, another channel cycle is run for
   the (possibly different) interface.

   The multiplexer obtains the current state from the State RAM entry
   corresponding to the service request number.  If the current state is
   invalid, i.e., not one of the four defined states, the channel aborts the
   transfer by asserting XFERERROR to the interface.  Otherwise, control
   branches to one of the four state handlers before returning.

   A transfer can be in one of four defined states:

     - State A: fetch the first word (IOCW) of the I/O program word
     - State B: fetch or store the second word (IOAW) of the I/O program word
     - State C: fetch or store the I/O program pointer (IOPP)
     - State D: transfer data to or from the interface

   All I/O orders except Set Bank, Read, and Write execute states C, A, and B,
   in that order.  The Set Bank order executes state C, A, and D.  The Read and
   Write orders execute states C, A, B, and then one D state for each word
   transferred.  Some actions are dependent on external signals (JMPMET or
   DEVEND) or internal conditions (terminal count reached [TC] or in a chained
   block transfer [IB]).

   The actions for the orders are:

     Jump (sioJUMP)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW read   --
         C                     IOPP write  DEVNODB


     Conditional Jump (sioJUMPC)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW read   SETJMP
       / C    ~ JMPMET         IOPP read   DEVNODB
       \ C      JMPMET         IOPP write  DEVNODB


     Return Residue (sioRTRES)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW write  --
         C                     IOPP read   DEVNODB


     Set Bank (sioSBANK)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         D                     IOAW read   --
         C                     IOPP read   DEVNODB


     Interrupt (sioINTRP)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW read   SETINT
         C                     IOPP read   DEVNODB


     End (sioEND)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW write  TOGGLESR | PSTATSTB | TOGGLESIOOK
       idle


     End with Interrupt (sioENDIN)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW write  TOGGLESR | SETINT | PSTATSTB | TOGGLESIOOK
       idle


     Control (sioCNTL)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   TOGGLESR | PCMD1
         B                     IOAW read   ACKSR | PCONTSTB
         C                     IOPP read   ACKSR | TOGGLESR | DEVNODB


     Sense (sioSENSE)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
         B                     IOAW write  PSTATSTB
         C                     IOPP read   DEVNODB


     Write (sioWRITE)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   ACKSR
       / B    ~ IB             IOAW read   TOGGLESR | TOGGLEOUTXFER
       \ B      IB             IOAW read   TOGGLESR

       / D    ~ TC             data write  ACKSR | PWRITESTB
       \ D      TC             data write  ACKSR | PWRITESTB | EOT | TOGGLEOUTXFER
       / D      DEVEND * ~ TC  IOPP read   ACKSR | TOGGLESR | EOT | TOGGLEOUTXFER
       \ D      DEVEND *   TC  IOPP read   ACKSR | TOGGLESR

       / C    ~ DEVEND         IOPP read   ACKSR | TOGGLESR | DEVNODB
       / A    ~ DEVEND         IOCW read   ACKSR
       \ A      DEVEND         IOCW read   ACKSR


     Write Chained (sioWRITEC)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
       / B    ~ IB             IOAW read   TOGGLESR | TOGGLEOUTXFER
       \ B      IB             IOAW read   TOGGLESR

       / D    ~ TC             data write  ACKSR | PWRITESTB
       \ D      TC             data write  ACKSR | TOGGLESR | PWRITESTB | EOT
       / D      DEVEND * ~ TC  IOPP read   ACKSR | EOT | TOGGLESR
       \ D      DEVEND *   TC  IOPP read   --

       / C    ~ DEVEND         IOPP read   DEVNODB
       \ A      DEVEND         IOCW read   --


     Read (sioREAD)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
       / B    ~ IB             IOAW read   TOGGLESR | TOGGLEINXFER | READNEXTWD
       \ B      IB             IOAW read   TOGGLESR | READNEXTWD

       / D    ~ TC             data write  ACKSR | PREADSTB | READNEXTWD
       \ D      TC             data write  ACKSR | PREADSTB | EOT | TOGGLEINXFER
       / D      DEVEND * ~ TC  IOPP read   ACKSR | TOGGLESR | EOT | TOGGLEINXFER
       \ D      DEVEND *   TC  IOPP read   ACKSR | TOGGLESR

       / C    ~ DEVEND         IOPP read   ACKSR | TOGGLESR | DEVNODB
       / A    ~ DEVEND         IOCW read   ACKSR
       \ A      DEVEND         IOCW read   ACKSR


     Read Chained (sioREADC)

       State  Condition        Action      Signals
       -----  ---------------  ----------  ------------------------------------------
         C                     IOPP read   DEVNODB
         A                     IOCW read   --
       / B    ~ IB             IOAW read   TOGGLESR | TOGGLEINXFER | READNEXTWD
       \ B      IB             IOAW read   TOGGLESR | READNEXTWD

       / D    ~ TC             data write  ACKSR | PREADSTB | READNEXTWD
       \ D      TC             data write  ACKSR | TOGGLESR | PREADSTB | EOT
       / D      DEVEND * ~ TC  IOPP read   ACKSR | TOGGLESR | EOT
       \ D      DEVEND *   TC  IOPP read   --

       / C    ~ DEVEND         IOPP read   DEVNODB
       \ A      DEVEND         IOCW read   --


   Summarizing the State D signals sent to the interface:

     Normal transfer
     ---------------
       - not the last word:                  ACKSR | PrwSTB { | READNEXTWD }
       - the last word and not chained:      ACKSR | PrwSTB | EOT | TOGGLEioXFER
       - the last word and chained:          ACKSR | PrwSTB | EOT | TOGGLESR

     DEVEND asserted after a normal transfer
     ---------------------------------------
       - not the last word and not chained:  ACKSR | TOGGLESR | EOT | TOGGLEioXFER
       - not the last word and chained:      ACKSR | TOGGLESR | EOT
       - the last word and not chained:      ACKSR | TOGGLESR
       - the last word and chained:          (none)

   In all cases where signals are generated, CHANSO is also included.


   Implementation notes:

    1. In hardware, IOCW bits 1-3 specify the I/O order, except that the Jump,
       End, Return Residue, and Set Bank orders require an additional bit (IOCW
       bit 4) to define their orders fully.  In simulation, the IOCW_ORDER macro
       uses IOCW bits 0-4 as an index into a 32-element lookup table to produce
       the final I/O order (because some of the orders define IOCW bit 4 as
       "don't care", there are only thirteen distinct orders).

    2. In hardware, the Interrupt order loads the address register with the
       (unused) IOAW value.  The simulator maintains this behavior.

    3. The word count rollover flip-flop is preset asynchronously by the carry
       out signal from the word counter and is cleared synchronously by the
       trailing edge of the write-to-RAMs signal at the end of each state.  It
       is used by the next-state logic to decide whether to remain in State D or
       exit to State C.

    4. In hardware, the Device End flip-flop is clocked at the beginning and end
       of every I/O cycle and samples the DEVEND signal from the interface.  The
       output controls the state sequencer.  In simulation, the flip-flop is
       cleared at the end of every cycle, which ensures that it's clear for the
       next cycle entry.

    5. The default label in the State B switch statement is necessary to quiet a
       warning that "inbound_signals" may be used uninitialized, even though all
       cases are covered.  The initialization of "outbound" is also necessary,
       even though all paths through the while statement set its value.
*/

void mpx_service (uint32 ticks_elapsed)
{
DIB          *dibptr;
int32        cycles;
uint32       srn, mask, priority_mask;
HP_WORD      inbound_data, outbound_data, iocw, ioaw;
t_bool       store_ioaw;
SIO_ORDER    sio_order;
INBOUND_SET  inbound_signals;
SIGNALS_DATA outbound = IORETURN (NO_SIGNALS, 0);       /* needed to quiet warning */

cycles = CYCLES_PER_EVENT - excess_cycles;              /* decrease the cycles available by any left over */

priority_mask = 0;                                      /* request a recalculation of the SR priority */

while (cycles > 0) {                                    /* execute as long as cycles remain */
    if (priority_mask == 0) {                           /* if priority must be recalculated */
        priority_mask = IOPRIORITY (mpx_request_set);   /*   then isolate the highest-priority bit from the set */

        if (priority_mask == 0)                         /* if no request is pending */
            break;                                      /*   then we're done for now */

        for (srn = 0, mask = priority_mask; !(mask & 1); srn++) /* determine the service request number */
            mask = mask >> 1;                                   /*   associated with the request bit */

        dibptr = srs [srn];                             /* get the DIB pointer for the request */

        state_reg = state_ram [srn];                    /* load the pipeline registers */
        aux_reg   = aux_ram   [srn];                    /*   from the selected RAM words */
        order_reg = order_ram [srn];
        cntr_reg  = cntr_ram  [srn];
        addr_reg  = addr_ram  [srn];

        sio_order = (SIO_ORDER) (order_reg & ORDER_MASK);   /* map the order */
        }

    dprintf (mpx_dev, DEB_STATE, "Channel SR %u entered %s with %d clock cycles remaining\n",
             srn, state_name [state_reg], cycles);

    switch (state_reg) {                                /* dispatch based on the multiplexer state */

        case State_A:
            if (sio_order == sioREAD                    /* if the previous order */
              || sio_order == sioWRITE)                 /*   was an unchained Read or Write */
                inbound_signals = ACKSR | CHANSO;       /*     then acknowledge the final service request */
            else                                        /* otherwise */
                inbound_signals = NO_SIGNALS;           /*   no acknowledgement is needed */

            iop_read_memory (absolute, addr_reg, &iocw);    /* fetch the IOCW from memory */
            cycles = cycles - CYCLES_PER_READ;              /*   and count the memory access */

            order_reg = IOCW_ORDER (iocw);              /* get the translated order from the IOCW */

            if (iocw & IOCW_DC)                         /* if the data chain bit is set */
                order_reg |= ORDER_DC;                  /*   then set the data chain flag */

            sio_order = (SIO_ORDER) (order_reg & ORDER_MASK);   /* isolate the I/O order */

            if (sio_order != sioRTRES)                  /* if this is not a Return Residue order */
                cntr_reg = IOCW_WCNT (iocw);            /*   then load the word count */

            dprintf (mpx_dev, DEB_PIO, "Channel SR %u loaded IOCW %06o (%s) from address %06o\n",
                     srn, iocw, sio_order_name [sio_order], addr_reg);

            if (sio_order == sioCNTL)                           /* if this a Control order */
                inbound_signals |= PCMD1 | TOGGLESR | CHANSO;   /*   then assert the first command strobe */

            if (inbound_signals)                        /* call the interface if there are signals to assert */
                outbound = dibptr->io_interface (dibptr, inbound_signals, iocw);
            else                                        /* otherwise the interface isn't involved */
                outbound = IORETURN (SRn, 0);           /*   but assert a service request to continue the program */

            addr_reg = addr_reg + 1 & R_MASK;           /* point at the IOAW program word */

            break;


        case State_B:
            store_ioaw = FALSE;                         /* assume that a fetch and not a store will be needed */

            switch (sio_order) {                        /* dispatch based on the I/O order */

                case sioJUMPC:
                    inbound_signals = SETJMP | CHANSO;
                    break;

                case sioRTRES:
                    inbound_signals = NO_SIGNALS;       /* no interface call is needed */

                    if (aux_reg & AUX_TC)               /* if the count has terminated */
                        outbound = IORETURN (SRn, 0);   /*   then return a zero count and a service request */
                    else                                /* otherwise return the two's-complement remainder */
                        outbound = IORETURN (SRn, IOCW_COUNT (cntr_reg));

                    store_ioaw = TRUE;                  /* set to store the count */
                    break;

                case sioINTRP:
                    inbound_signals = SETINT | CHANSO;
                    break;

                case sioEND:
                    inbound_signals = TOGGLESIOOK | TOGGLESR | PSTATSTB | CHANSO;
                    store_ioaw = TRUE;                  /* set to store the returned status */
                    break;

                case sioENDIN:
                    inbound_signals = TOGGLESIOOK | TOGGLESR | PSTATSTB | SETINT | CHANSO;
                    store_ioaw = TRUE;                  /* set to store the returned status */
                    break;

                case sioCNTL:
                    inbound_signals = ACKSR | PCONTSTB | CHANSO;
                    break;

                case sioSENSE:
                    inbound_signals = PSTATSTB | CHANSO;
                    store_ioaw = TRUE;                  /* set to store the returned status */
                    break;

                case sioWRITE:
                case sioWRITEC:
                    inbound_signals = TOGGLESR | CHANSO;

                    if ((aux_reg & AUX_IB) == 0)            /* if we are not within a block transfer */
                        inbound_signals |= TOGGLEOUTXFER;   /*   then add the signal to start the transfer */
                    break;

                case sioREAD:
                case sioREADC:
                    inbound_signals = READNEXTWD | TOGGLESR | CHANSO;

                    if ((aux_reg & AUX_IB) == 0)            /* if we are not within a block transfer */
                        inbound_signals |= TOGGLEINXFER;    /*   then add the signal to start the transfer */
                    break;

                default:                                    /* needed to quiet warning about inbound_signals */
                case sioJUMP:                               /* these orders do not need */
                case sioSBANK:                              /*   to call the interface */
                    inbound_signals = NO_SIGNALS;           /*     so assert a service request */
                    outbound = IORETURN (SRn, 0);           /*       to continue the program */
                    break;
                }

            if (store_ioaw == FALSE) {                          /* if a fetch is needed */
                iop_read_memory (absolute, addr_reg, &ioaw);    /*   then load the IOAW from memory */
                cycles = cycles - CYCLES_PER_READ;              /*     and count the memory access */

                dprintf (mpx_dev, DEB_PIO, "Channel SR %u loaded IOAW %06o from address %06o\n",
                         srn, ioaw, addr_reg);
                }

            else                                                /* otherwise provide a dummy value */
                ioaw = 0;                                       /*   that will be overwritten */

            if (inbound_signals)                                /* if there are signals to assert */
                outbound = dibptr->io_interface (dibptr,        /*   then pass them to the interface */
                                                 inbound_signals, ioaw);

            if (store_ioaw == TRUE) {                           /* if a store is needed */
                ioaw = IODATA (outbound);                       /*   then set the IOAW from the returned value */
                iop_write_memory (absolute, addr_reg, ioaw);    /*     and store it in memory */
                cycles = cycles - CYCLES_PER_WRITE;             /* count the memory access */

                dprintf (mpx_dev, DEB_PIO, "Channel SR %u stored IOAW %06o to address %06o\n",
                         srn, ioaw, addr_reg);
                }

            switch (sio_order) {                        /* dispatch based on the I/O order */
                case sioREAD:
                case sioREADC:
                case sioWRITE:
                case sioWRITEC:
                    aux_reg = aux_reg & ~AUX_TC | AUX_IB;   /* clear the terminal count and set the in-block bit */
                    addr_reg = ioaw;                        /* load the address register with the address word */
                    break;

                case sioJUMP:
                case sioJUMPC:
                case sioINTRP:
                    addr_reg = ioaw;                    /* load the address register with the address word */
                    break;

                case sioEND:
                case sioENDIN:
                    end_channel (dibptr);               /* end the channel program */

                    dprintf (mpx_dev, DEB_STATE, "Channel SR %u entered the %s\n",
                             srn, state_name [State_Idle]);
                    break;

                case sioCNTL:                           /* no additional */
                case sioSBANK:                          /*   processing needed */
                case sioRTRES:                          /*     for these orders */
                case sioSENSE:
                    break;
                }

            break;


        case State_C:
            inbound_signals = DEVNODB | CHANSO;         /* assert DEVNODB to get the device number */

            if (sio_order == sioREAD                    /* if we're completing */
              || sio_order == sioWRITE                  /*   a Read, Write, */
              || sio_order == sioCNTL)                  /*     or Control order */
                inbound_signals |= ACKSR | TOGGLESR;    /*       then clear the device and channel SR flip-flops */

            outbound = dibptr->io_interface (dibptr, inbound_signals, 0);

            if (sio_order != sioJUMP                                        /* if we're not completing */
              && (sio_order != sioJUMPC || (outbound & JMPMET) == 0)) {     /*   a successful jump order */
                iop_read_memory (absolute, IODATA (outbound), &addr_reg);   /*     then get the I/O program pointer */
                cycles = cycles - CYCLES_PER_READ;                          /*       and count the memory access */
                }

            iop_write_memory (absolute, IODATA (outbound),  /* write the updated program pointer */
                              addr_reg + 2 & R_MASK);       /*   back to the DRT */
            cycles = cycles - CYCLES_PER_WRITE;             /*     and count the access */

            break;


        case State_D:
            inbound_data = 0;                                   /* assume there is no inbound data */

            if (sio_order == sioSBANK) {                        /* if this is a Set Bank order */
                iop_read_memory (absolute, addr_reg, &ioaw);    /*   then read the IOAW */
                cycles = cycles - CYCLES_PER_READ;              /*     and count the memory access */

                dprintf (mpx_dev, DEB_PIO, "Channel SR %u loaded IOAW %06o from address %06o\n",
                         srn, ioaw, addr_reg);

                addr_reg = ioaw;                        /* store the IOAW into the address register */

                aux_reg = aux_reg & ~AUX_BANK_MASK      /* merge the new bank number */
                                | AUX_BANK (ioaw);      /*   into the auxiliary register */

                outbound = IORETURN (SRn, 0);           /* assert a service request to continue the program */
                break;                                  /* no call to the interface is needed */
                }

            else if (sio_order == sioREAD                       /* otherwise if this is a Read order */
              || sio_order == sioREADC) {                       /*   or a Read Chained order */
                inbound_signals = ACKSR | PREADSTB | CHANSO;    /*     then assert the read strobe */

                if (cntr_reg == CNTR_MAX) {                     /* if the word count is now exhausted */
                    if (sio_order == sioREADC)                  /*   then if the order is chained */
                        inbound_signals |= EOT | TOGGLESR;      /*     then assert EOT and toggle the channel SR flip-flop */
                    else                                        /*   otherwise */
                        inbound_signals |= EOT | TOGGLEINXFER;  /*     assert EOT and end the transfer */
                    }

                else                                            /* otherwise the transfer continues */
                    inbound_signals |= READNEXTWD;              /*   so request the next word */
                }

            else {                                              /* otherwise this is a Write or Write Chained order */
                inbound_signals = ACKSR | PWRITESTB | CHANSO;   /*   so assert the write strobe */

                if (cntr_reg == CNTR_MAX)                       /* if the word count is now exhausted */
                    if (sio_order == sioWRITEC)                 /*   then if the order is chained */
                        inbound_signals |= EOT | TOGGLESR;      /*     then assert EOT and toggle the channel SR flip-flop */
                    else                                        /*   otherwise */
                        inbound_signals |= EOT | TOGGLEOUTXFER; /*     assert EOT and end the transfer */

                if (iop_read_memory (dma,                                   /* read the word from memory */
                                     TO_PA (AUX_BANK (aux_reg), addr_reg),  /*   at the indicated bank and offset */
                                     &inbound_data))                        /* if the read succeeds */
                    cycles = cycles - CYCLES_PER_READ;                      /*   then count the memory access */

                else {                                                          /* otherwise the read failed */
                    outbound = abort_channel (dibptr, "a memory read error");   /*   so abort the transfer */
                    break;                                                      /*     and skip the interface call */
                    }
                }

            outbound = dibptr->io_interface (dibptr, inbound_signals, inbound_data);    /* call the interface */

            device_end = D_FF (outbound & DEVEND);              /* set the flip-flop if the interface asserted DEVEND */

            if (device_end == SET) {                            /* if the transfer was aborted by the interface */
                outbound_data = IODATA (outbound);              /*   then it returned the DRT program pointer address */

                iop_read_memory (absolute, outbound_data, &addr_reg);   /* do the I/O program pointer fetch here */
                iop_write_memory (absolute, outbound_data,              /*   so we don't have to do State C */
                                  addr_reg + 2 & R_MASK);
                cycles = cycles - CYCLES_PER_READ - CYCLES_PER_WRITE;   /* count the two memory accesses */

                if (cntr_reg == CNTR_MAX)                               /* if the word count is now exhausted */
                    if (order_reg & ORDER_DC)                           /*   then if the order is chained */
                        inbound_signals = NO_SIGNALS;                   /*     then all required signals have been sent */
                    else                                                /*   otherwise */
                        inbound_signals = ACKSR | TOGGLESR | CHANSO;    /*     toggle the channel SR flip-flop */

                else {                                                  /* otherwise the transfer is incomplete */
                    inbound_signals = ACKSR | EOT | TOGGLESR | CHANSO;  /*   so assert EOT and toggle the channel SR FF */

                    if (! (order_reg & ORDER_DC)) {             /* if the order is not chained */
                        aux_reg &= ~AUX_IB;                     /*   then clear the in-block bit in RAM */

                        if (sio_order == sioREAD)               /* if it's a Read order */
                            inbound_signals |= TOGGLEINXFER;    /*   then terminate the inbound transfer */
                        else                                    /* otherwise it's a Write order */
                            inbound_signals |= TOGGLEOUTXFER;   /*   so terminate the outbound transfer */
                        }
                    }

                if (inbound_signals)                                            /* if there are signals to assert */
                    outbound = dibptr->io_interface (dibptr,                    /*   then pass them to the interface */
                                                     inbound_signals, 0);
                }

            else {                                                              /* otherwise the transfer succeeded */
                if (sio_order == sioREAD || sio_order == sioREADC)              /* if this is a Read or Read Chained order */
                    if (iop_write_memory (dma,                                  /*   then write the word to memory */
                                          TO_PA (AUX_BANK (aux_reg), addr_reg), /*     at the indicated bank and offset */
                                          IODATA (outbound)))                   /* if the write succeeds */
                        cycles = cycles - CYCLES_PER_WRITE;                     /*   then count the memory access */

                    else {                                                          /* otherwise the write failed */
                        outbound = abort_channel (dibptr, "a memory write error");  /*   so abort the transfer */
                        break;                                                      /*     and bail out now */
                        }

                addr_reg = addr_reg + 1 & R_MASK;       /* point at the next word to transfer */
                cntr_reg = cntr_reg + 1 & CNTR_MASK;    /*   and count the word */

                if (cntr_reg == 0) {                    /* if the count is exhausted */
                    rollover = SET;                     /*   then set the rollover flip-flop */
                    aux_reg |= AUX_TC;                  /*     and the terminal count flag */

                    if (! (order_reg & ORDER_DC))       /* if the order is not chained */
                        aux_reg &= ~AUX_IB;             /*   then clear the in-block flag */
                    }
                }

            break;


        default:                                                            /* if the channel state is invalid */
            status_word = ST_STATE_PARITY | ST_RAM_ADDR (srn);              /*   then save the RAM address */
            outbound = abort_channel (dibptr, "an invalid state entry");    /*     and abort the transfer */
            break;
        }

    cycles = cycles - CYCLES_PER_STATE;                         /* count the state execution */

    state_reg = next_state (state_reg, sio_order, device_end);  /* get the next state */

    rollover   = CLEAR;                                 /* the end of each state clears */
    device_end = CLEAR;                                 /*   the word count rollover and device end flip-flops */

    if ((outbound & SRn) == NO_SIGNALS) {               /* if the device is no longer requesting service */
        mpx_request_set &= ~priority_mask;              /*   then clear its request from the set */
        dibptr->service_request = FALSE;                /*     and clear its internal request flag */

        priority_mask = 0;                              /* request SR priority recalculation */

        dprintf (mpx_dev, DEB_SR, "Device number %u denied SR%u\n",
                 dibptr->device_number, dibptr->service_request_number);
        }

    if (outbound & INTREQ)                              /* if the interface asserted an interrupt request */
        iop_assert_INTREQ (dibptr);                     /*   then set it up */

    if (cycles <= 0 || priority_mask == 0) {            /* if service for this device is ending */
        state_ram [srn] = state_reg;                    /*   then write */
        aux_ram   [srn] = aux_reg;                      /*     the pipeline */
        order_ram [srn] = order_reg;                    /*       registers back */
        cntr_ram  [srn] = cntr_reg;                     /*         to their */
        addr_ram  [srn] = addr_reg;                     /*           associated RAMS */
        }
    }                                                   /* end while */


if (cycles > 0)                                         /* if we exited because there are no service requests */
    excess_cycles = 0;                                  /*   then do a full set of cycles next time */
else                                                    /* otherwise we ran over our allotment */
    excess_cycles = - cycles;                           /*   so reduce the next poll by the overage */

return;
}



/* Channel local SCP support routines */



/* Multiplexer channel diagnostic interface.

   The channel diagnostic interface is installed on the IOP bus and receives
   direct I/O commands from the IOP.  It does not respond to programmed I/O
   (SIO) commands, nor does it interrupt.

   In simulation, the asserted signals on the bus are represented as bits in the
   inbound_signals set.  Each signal is processed sequentially in numerical
   order, and a set of similar outbound_signals is assembled and returned to the
   caller, simulating assertion of the corresponding bus signals.

   The interface allows a program to write to and read from any desired address
   in the address, order, state, or auxiliary RAMs.  A CIO instruction specifies
   the RAM address and register to write or read with a subsequent WIO or RIO
   instruction.  In addition, the address and word count registers may be
   incremented and the resulting values tested for correctness.  After the RAMs
   are written, the next state is computed and written to the state RAM.
   Reading this value allows the next-state logic to be checked.


   Implementation notes:

    1. In hardware, IOCW bits 1-3 specify the I/O order, except that the Jump,
       End, Return Residue, and Set Bank orders require an additional bit (IOCW
       bit 4) to define their orders fully.  In simulation, the IOCW_ORDER macro
       uses IOCW bits 0-4 as an index into a 32-element lookup table to produce
       the final I/O order (because some of the orders define IOCW bit 4 as
       "don't care", there are only thirteen distinct orders).

    2. In hardware, the "select the Address RAM and Register" bit (bit 6) of the
       control word is used only to enable reading and incrementing.  The
       address RAM is written by a WIO instruction if the "select the Order RAM
       and Register" bit (bit 7) is not set.  If bit 7 is set, then the Order
       RAM is written.

    3. A WIO instruction writes all of the RAMs simultaneously.  The control
       word select bits simply determine whether RAM data comes from the output
       word or the corresponding register.

    4. A RIO instruction with the "load the registers from the RAMs during the
       next read" bit (bit 9) of the control word set loads all registers
       simultaneously.  If the load bit and the "increment the Address or Word
       Count Registers after the next read" bit (bit 10) are both set, the load
       overrides the increment.  An enabled increment occurs after the current
       value is returned.

    5. If multiple registers are enabled in the control word, an RIO instruction
       will return the logical OR of the several values (in hardware, the
       selected registers are enabled to the active-low IOD bus).
*/

static SIGNALS_DATA mpx_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
uint32         address;
SIO_ORDER      sio_order;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (mpx_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

while (working_set) {
    signal = IONEXTSIG (working_set);                   /* isolate the next signal */

    switch (signal) {                                   /* dispatch an I/O signal */

        case DWRITESTB:
            address = CN_RAM_ADDR (control_word);       /* get the RAM location to address */

            if (control_word & CN_ORDER_RAM) {          /* if the order RAM is enabled */
                addr_ram [address] = addr_reg;          /*   then reload the address RAM from its register */

                order_ram [address] = WR_ORDER (inbound_value);     /* set the order RAM from the order field */

                sio_order = IOCW_ORDER (inbound_value);             /* get the translated order */

                if (sio_order != sioRTRES)                          /* if it's not a Return Residue order */
                    cntr_ram [address] = WR_COUNT (inbound_value);  /*   then set the counter RAM from the counter field */
                }

            else {                                      /* otherwise the order RAM is disabled */
                addr_ram [address] = inbound_value;     /*   so set the address RAM from the value */

                sio_order = RD_SIO_ORDER (order_reg, cntr_reg); /* get the current SIO order */

                order_ram [address] = order_reg;        /* reload the order and counter RAMs */
                cntr_ram [address]  = cntr_reg;         /*   from their respective registers */
                }

            state_ram [address] = next_state (state_reg, sio_order, FALSE); /* store the next state into the state RAM */

            if (control_word & CN_STATE_RAM) {                          /* if the state RAM is enabled */
                state_ram [address] |= WR_STATE (inbound_value);        /*   then merge the new state values */

                aux_ram [address] = aux_reg & (AUX_IB | AUX_TC)         /* set the new bank value */
                                          | WR_BANK (inbound_value);    /*   while preserving the flag bits */
                }

            else                                        /* otherwise the state RAM is disabled */
                aux_ram [address] = aux_reg;            /*   so reload the auxiliary RAM from its register */

            if (state_reg == State_B)                   /* if the current state is State B */
                rollover = CLEAR;                       /*   then clear the word count rollover flip-flop */

            dprintf (mpx_dev, DEB_CSRW, "RAM [%u] stored address %06o | %s | "
                                        "counter %04o | %s | %sbank %02o\n",
                     address, addr_ram [address], sio_order_name [sio_order],
                     cntr_ram [address], state_name [state_ram [address]],
                     fmt_bitset (aux_ram [address], aux_format),
                     AUX_BANK (aux_ram [address]));
            break;


        case DREADSTB:
            address = CN_RAM_ADDR (control_word);       /* get the RAM location to address */

            if (control_word & CN_LOAD_REGS) {          /* if the load enable bit is set */
                addr_reg  = addr_ram  [address];        /*   then load all */
                order_reg = order_ram [address];        /*     of the registers */
                cntr_reg  = cntr_ram  [address];        /*       from their */
                state_reg = state_ram [address];        /*         associated RAMs */
                aux_reg   = aux_ram   [address];        /*           regardless of any RAM enables */

                sio_order = RD_SIO_ORDER (order_reg, cntr_reg); /* get the current SIO order */

                dprintf (mpx_dev, DEB_CSRW, "RAM [%u] loaded address %06o | %s | "
                                            "counter %04o | %s | %sbank %02o\n",
                         address, addr_reg, sio_order_name [sio_order],
                         cntr_reg, state_name [state_reg],
                         fmt_bitset (aux_reg, aux_format),
                         AUX_BANK (aux_reg));
                }

            outbound_value = 0;                             /* start with an inactive IOD bus */

            if (control_word & CN_STATE_RAM) {              /* if the state register is selected */
                outbound_value = RD_STATE (state_reg)       /*   then merge the state register */
                                   | RD_BANK (aux_reg);     /*     and bank number to the bus */

                if (aux_reg & AUX_TC)                       /* if the transfer-complete flag is set */
                    outbound_value |= RD_XFER_COMPLETE;     /*   then reflect it in the status */

                if (rollover == SET)                        /* if the word count rollover flip-flop is set */
                    outbound_value |= RD_XFER_END;          /*   then indicate the end of the transfer */

                if (odd_parity [UPPER_BYTE (addr_reg)       /* if the address register value */
                                  ^ LOWER_BYTE (addr_reg)]) /*   has odd parity */
                    outbound_value |= RD_ADDR_PARITY;       /*     then set the parity status bit */

                if (state_parity [state_reg])               /* if the state register does not have exactly one bit set */
                    outbound_value |= RD_STATE_PARITY;      /*   then set the state parity status bit */

                dprintf (mpx_dev, DEB_CSRW, "State register value %sbank %02o returned\n",
                         fmt_bitset (outbound_value, read_format),
                         AUX_BANK (aux_reg));
                }

            if (control_word & CN_ORDER_RAM) {              /* if the order register is selected */
                outbound_value = RD_ORDER (order_reg)       /*   then merge the order */
                                   | RD_COUNT (cntr_reg);   /*     and counter registers to the bus */

                dprintf (mpx_dev, DEB_CSRW, "Order register value %02o (%s) "
                                            "and counter register value %d returned\n",
                         order_reg & ORDER_MASK, sio_order_name [IOCW_ORDER (outbound_value)],
                         SEXT16 (IOCW_COUNT (outbound_value)));
                }

            if (control_word & CN_ADDR_RAM) {               /* if the address register is selected */
                outbound_value |= addr_reg;                 /*   then enable it to drive the bus */

                dprintf (mpx_dev, DEB_CSRW, "Address register value %06o returned\n",
                         addr_reg);
                }

            if (control_word & CN_INCR_REGS) {              /* if incrementing is enabled */
                if (control_word & CN_ADDR_RAM){            /*   then if the address register is selected */
                    addr_reg = addr_reg + 1 & RD_ADDR_MASK; /*     then increment it */

                    dprintf (mpx_dev, DEB_CSRW, "Address register incremented to %06o\n",
                             addr_reg);
                    }

                if (control_word & CN_ORDER_RAM) {              /* if the order register is selected */
                    cntr_reg = cntr_reg + 1 & RD_COUNT_MASK;    /*   then increment the counter part of it */

                    dprintf (mpx_dev, DEB_CSRW, "Counter register incremented to %04o\n",
                             cntr_reg);

                    if (cntr_reg == 0) {                /* if the counter rolled over */
                        rollover = SET;                 /*   then set the rollover flip-flop */
                        aux_reg |= AUX_TC;              /*     and the terminal count flag */
                        }
                    }
                }
            break;


        case DSTATSTB:
            outbound_value = ST_DIO_OK | status_word;   /* get the last state parity error, if any */

            dprintf (mpx_dev, DEB_CSRW, (status_word & ST_STATE_PARITY)
                                           ? "Status is %sRAM address %u\n"
                                           : "Status is DIO OK\n",
                     fmt_bitset (outbound_value, status_format),
                     ST_TO_RAM_ADDR (outbound_value));
            break;


        case DCONTSTB:
            control_word = inbound_value;               /* save the new control word */

            if (control_word & CN_MR)                   /* if a master reset is indicated */
                mpx_reset (&mpx_dev);                   /*   then perform an IORESET */

            dprintf (mpx_dev, DEB_CSRW, "Control is %sRAM address %u\n",
                     fmt_bitset (inbound_value, control_format),
                     CN_RAM_ADDR (control_word));
            break;


        case DSETINT:                                   /* not used by this interface */
        case DRESETINT:                                 /* not used by this interface */
        case DSTARTIO:                                  /* not used by this interface */
        case DSETMASK:                                  /* not used by this interface */
        case INTPOLLIN:                                 /* not used by this interface */
        case XFERERROR:                                 /* not used by this interface */
        case ACKSR:                                     /* not used by this interface */
        case TOGGLESR:                                  /* not used by this interface */
        case TOGGLESIOOK:                               /* not used by this interface */
        case TOGGLEINXFER:                              /* not used by this interface */
        case TOGGLEOUTXFER:                             /* not used by this interface */
        case READNEXTWD:                                /* not used by this interface */
        case PREADSTB:                                  /* not used by this interface */
        case PWRITESTB:                                 /* not used by this interface */
        case PCMD1:                                     /* not used by this interface */
        case PCONTSTB:                                  /* not used by this interface */
        case PSTATSTB:                                  /* not used by this interface */
        case DEVNODB:                                   /* not used by this interface */
        case SETINT:                                    /* not used by this interface */
        case EOT:                                       /* not used by this interface */
        case SETJMP:                                    /* not used by this interface */
        case CHANSO:                                    /* not used by this interface */
        case PFWARN:                                    /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

dprintf (mpx_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Device reset.

   This routine is called for a RESET or RESET MPX command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to a Programmed Master Reset.

   A reset does not clear the order, counter, or address registers, nor any of
   the RAMs.
*/

static t_stat mpx_reset (DEVICE *dptr)
{
state_reg = 0;                                          /* clear the state */
aux_reg   = 0;                                          /*   and auxiliary registers */

control_word = 0;                                       /* clear the control register */
status_word  = 0;                                       /*   and state parity status register */

rollover   = CLEAR;                                     /* clear the word count rollover */
device_end = CLEAR;                                     /*   and device end flip-flops */

active_count = 0;                                       /* idle the channel */
mpx_is_idle  = TRUE;

return SCPE_OK;
}



/* Channel local utility routines */



/* Determine the next state.

   All I/O orders except Set Bank, Read, and Write execute states C, A, and B,
   in that order.  The Set Bank order executes state C, A, and D.  The Read and
   Write orders execute states C, A, B, and then one D state for each word
   transferred.

   An abort in state D uses that cycle to perform the action of the next initial
   state C, which is skipped.  Following the abort, the next state is state A.
*/

static uint8 next_state (uint8 current_state, SIO_ORDER order, t_bool abort)
{
switch (current_state) {

    case State_A:                                       /* from state A */
        if (order == sioSBANK)                          /*   the Set Bank order */
            return State_D;                             /*     proceeds to state D */
        else                                            /*   while all other orders */
            return State_B;                             /*     proceed to state B */


    case State_B:                                       /* from state B */
        if (order == sioEND || order == sioENDIN)       /*   the End and End with Interrupt orders */
            return State_Idle;                          /*     idle the channel */

        else if (order >= sioWRITE)                     /*   while the Write and Read orders */
            return State_D;                             /*     proceed to state D */

        else                                            /*   and all other orders */
            return State_C;                             /*     proceed to state C */


    case State_C:                                       /* from state C */
        return State_A;                                 /*   all orders proceed to state A */


    case State_D:                                       /* from state D */
        if (order == sioSBANK || rollover == SET)       /*   the Set Bank order and the terminal count condition */
            return State_C;                             /*     proceed to state C */

        else if (abort)                                 /*   while the transfer abort condition */
            return State_A;                             /*     proceeds to state A */

        else                                            /*   and transfer continuation */
            return State_D;                             /*     remains in state D */


    default:                                            /* all invalid states */
        return State_Idle;                              /*   return to the idle condition */
    }
}


/* End the channel I/O program.

   The channel program ends, either normally via an sioEND or sioENDIN order, or
   abnormally via an XFERERROR abort.  The reference count is decreased, and the
   idle flag set if no more transfers are active.
*/

static void end_channel (DIB *dibptr)
{
active_count = active_count - 1;                        /* decrease the reference count */
mpx_is_idle = (active_count == 0);                      /*   and idle the channel if no more work */

dprintf (mpx_dev, DEB_CSRW, "Channel SR %u program ended\n",
         dibptr->service_request_number);

return;
}


/* Abort the transfer in progress.

   If an internal channel error occurs (e.g., a memory read or write failure,
   due to an invalid address), the channel asserts the XFERERROR signal to the
   device and then terminates the channel program.  The device will clear its
   internal logic in response.
*/

static SIGNALS_DATA abort_channel (DIB *dibptr, const char *reason)
{
SIGNALS_DATA outbound;

dprintf (mpx_dev, DEB_CSRW, "Channel SR %u asserted XFERERROR for %s\n",
         dibptr->service_request_number, reason);

outbound = dibptr->io_interface (dibptr, XFERERROR | CHANSO, 0);    /* tell the device that the channel has aborted */

end_channel (dibptr);                                               /* end the channel program */

return outbound;
}
