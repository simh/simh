/* hp3000_sel.c: HP 3000 30030C Selector Channel simulator

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

   SEL          HP 3000 Series III Selector Channel

   11-Jul-16    JDB     Change "sel_unit" from a UNIT to an array of one UNIT
   30-Jun-16    JDB     Reestablish active_dib pointer during sel_initialize
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     abort_channel parameter is now a pointer-to-constant
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   23-Sep-15    JDB     First release version
   27-Jan-15    JDB     Passes the selector channel diagnostic (D429A)
   10-Feb-13    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - Stand-Alone HP 30030B/C Selector Channel Diagnostic
         (30030-90011, July 1978)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, Apr-1980)


   The HP 30030C Selector Channel provides high-speed data transfer between a
   device and main memory.  While several interfaces may be connected to the
   selector channel bus, only one transfer is active at a time, and the channel
   remains dedicated to that interface until the transfer is complete.  The
   channel contains its own memory port controller, so transfers to and from
   memory bypass the I/O Processor.

   Interfaces must have additional hardware to be channel-capable, as the
   channel uses separate control and data signals from those used for direct
   I/O.  In addition, the multiplexer and selector channels differ somewhat in
   their use of the signals, so interfaces are generally designed for use with
   one or the other (the Selector Channel Maintenance Board is a notable
   exception that uses jumpers to indicate which channel to use).

   The transfer rate of the Series III selector channel is poorly documented.
   Various rates are quoted in different publications: a 1.9 MB/second rate in
   one, a 2.86 MB/second rate in another.  Main memory access time is given as
   300 nanoseconds, and the cycle time is 700 nanoseconds.

   Once started by an SIO instruction, the channel executes I/O programs
   independently of the CPU.  Program words are read, and device status is
   written back, directly via the port controller.

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

   Channel-capable interfaces connect via the selector channel bus and request
   channel service by asserting the CHANSR signal.  An interface may initiate an
   I/O program on the channel only if the channel is not busy with a transfer
   involving another interface.  The SIO instruction microcode tests status bit
   0 ("SIO OK") from the interface before starting the I/O program.  This bit
   reflects the SIOENABLE signal from the channel, which is true only when the
   channel is idle.  If the channel is busy, the SIO instruction sets CCG and
   returns without disturbing the channel.

   The channel uses double-buffering for both data and I/O program words,
   allowing concurrent channel/interface and channel/memory transfers.
   Prefetching of the next I/O program word increases channel speed by reducing
   the wait on memory transfers.

   In simulation, an interface is implicitly connected to the selector channel
   bus by calling the "sel_assert_REQ" routine.  This initiates the transfer
   between the device number of the interface and the channel.  The
   "service_request" field in the DIB is not used (it must be set to the
   SRNO_UNUSED value).

   The channel simulator provides these global objects:

     t_bool sel_is_idle

       TRUE if the selector channel is idle; FALSE otherwise.  Corresponds to
       the hardware SIOENABLE signal and reflects the value of the Selector
       Active flip-flop.  Used by device interfaces to qualify their SIO OK
       status bits.

     sel_assert_REQ (DIB *)

       Called by the device interface while processing a DSTARTIO signal to
       request that the selector channel begin an SIO operation, or called at
       any time while the channel is active to abort the operation.  Corresponds
       to asserting the REQ signal.  If the channel is idle, it initializes the
       channel and starts the sequencer.  If the channel is active, it aborts
       the I/O program execution and idles the channel.

     sel_assert_CHANSR (DIB *)

       Called by the device controller to request service from the selector
       channel asynchronously.  Corresponds to asserting the CHANSR signal.
       Typically called from a device service routine; device controller
       interface routines return the CHANSR signal to request service
       synchronously.  Sets the service_request flag in the DIB and sets
       sel_request.

     t_bool sel_request

       TRUE if an interface is requesting service from the selector channel or
       the channel is servicing an internal request; FALSE otherwise.
       Corresponds to the CHANSR signal received by the channel.  Used by the
       CPU to determine if a request is pending and the selector channel service
       routine should be called.

     sel_initialize (void)

       Called in the instruction execution prelude to allow devices to be
       reassigned or reset.  If a device is under channel control, the routine
       reestablishes the device number for the channel.  It also sets
       sel_request TRUE if the device has a service request pending or FALSE
       otherwise.

     sel_service (uint32)

       Called to service a request from the device interface or an internal
       request from the selector channel.  Executes one or more channel cycles
       for the associated interface.  Used by the CPU to run the selector
       channel.


   Implementation notes:

    1. In hardware, the installed memory size must be set with jumper S3 on the
       Selector Channel Register PCA.  In simulation, the size is determined by
       using the MEMSIZE macro that tests the "capac" field of the CPU unit.

    2. Multiple channels and interleaved memory operation and its associated
       configuration jumpers are not supported.

    3. The selectable trigger/freeze and error logging register features of the
       Selector Channel Control PCA are not supported.  Debug tracing may be
       enabled to determine why an I/O program aborted.

    3. The selector channel must execute more than one I/O order per CPU
       instruction in order to meet the timing requirements of the diagnostic.
       The timing is modeled by establishing a count of channel clock pulses at
       poll entry and then executing orders until the count is exhausted.  If
       the clock count was exceeded, the excess count is saved and then
       subtracted from the next entry's count, so that the typical execution
       time is preserved over a number of entries.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_io.h"



/* Program constants.

   The selector channel clock period is 175 nanoseconds.  The channel runs
   concurrently with the CPU, which executes instructions in an average of 2.57
   microseconds, so multiple cycles are executed per CPU instruction.

   In simulation, the channel is called from the instruction execution loop
   after every instruction, and sometimes additionally within instructions that
   have long execution times (e.g., MOVE).  The number of event ticks that have
   elapsed since the last call are passed to the channel; this determines the
   number of channel cycles to execute.


   Implementation notes:

    1. The number of cycles consumed by the channel for various operations are
       educated guesses.  There is no documentation available that details the
       cycle timing.

    2. In simulation, the Wait Sequence exists separately from the Transfer
       Sequence only to avoid cancelling the SR wait timer for each word
       transferred.  It is reported as a Transfer Sequence cycle.
*/

#define SR_WAIT_TIMER       mS (1000)           /* 1000 millisecond SR wait timer */

#define NS_PER_CYCLE        175                 /* each clock cycle is 175 nanoseconds */

#define CYCLES_PER_FETCH    6
#define CYCLES_PER_PREFETCH 1
#define CYCLES_PER_EXECUTE  1
#define CYCLES_PER_RELOAD   3
#define CYCLES_PER_READ     4
#define CYCLES_PER_WRITE    4

#define CYCLES_PER_EVENT    (uint32) (USEC_PER_EVENT * 1000 / NS_PER_CYCLE)

#define CNTR_MASK           0007777u            /* word counter count mask */
#define CNTR_MAX            0007777u            /* word counter maximum value */


typedef enum {                                  /* selector channel sequencer state */
    Idle_Sequence,
    Fetch_Sequence,
    Execute_Sequence,
    Wait_Sequence,
    Transfer_Sequence,
    Reload_Sequence
    } SEQ_STATE;

static const char *const seq_name [] = {        /* indexed by SEQ_STATE */
    "Idle",
    "Fetch",
    "Execute",
    "Transfer",                                 /* the wait sequence is reported as a transfer sequence */
    "Transfer",
    "Reload"
    };

static const char *const action_name [] = {     /* indexed by SEQ_STATE */
    NULL,                                       /*   no loading occurs in the Idle_Sequence */
    "loaded",                                   /*   loads occur in the Fetch_Sequence */
    "prefetched",                               /*   prefetches occur in the Execute_Sequence */
    NULL,                                       /*   no loading occurs in the Wait_Sequence */
    "prefetched",                               /*   prefetches occur in the Transfer_Sequence */
    "loaded"                                    /*   loads occur in the Reload_Sequence */
    };


/* Debug flags.


   Implementation notes:

    1. Bit 0 is reserved for the memory data trace flag.
*/

#define DEB_CSRW            (1u << 1)           /* trace channel command initiations and completions */
#define DEB_PIO             (1u << 2)           /* trace programmed I/O commands */
#define DEB_STATE           (1u << 3)           /* trace state changes */
#define DEB_SR              (1u << 4)           /* trace service requests */


/* Channel global state */

t_bool sel_is_idle = TRUE;                      /* TRUE if the channel is idle */
t_bool sel_request = FALSE;                     /* TRUE if the channel sequencer is to be invoked */


/* Channel local state */

static SEQ_STATE sequencer = Idle_Sequence;     /* the current sequencer execution state */
static SIO_ORDER order;                         /* the current SIO order */
static DIB      *active_dib;                    /* a pointer to the participating interface's DIB */
static uint32    device_index;                  /* the index into the device table */
static t_bool    prefetch_control;              /* TRUE if the IOCW should be prefetched */
static t_bool    prefetch_address;              /* TRUE if the IOAW should be prefetched */

static uint32    device_number;                 /* the participating interface's device number */
static uint32    bank;                          /* the transfer bank register */
static uint32    word_count;                    /* the transfer word count register */

static HP_WORD   program_counter;               /* the I/O program counter */
static HP_WORD   control_word;                  /* the current IOCW */
static HP_WORD   control_buffer;                /* the prefetched IOCW */
static HP_WORD   address_word;                  /* the current IOAW */
static HP_WORD   address_buffer;                /* the prefetched IOAW */
static HP_WORD   input_buffer;                  /* the input data word buffer */
static HP_WORD   output_buffer;                 /* the output data word buffer */

static FLIP_FLOP rollover;                      /* SET if the transfer word count rolls over */
static int32     excess_cycles;                 /* the count of cycles in excess of allocation */


/* Channel local SCP support routines */

static t_stat sel_timer (UNIT   *uptr);
static t_stat sel_reset (DEVICE *dptr);

/* Channel local utility routines */

static void         end_channel       (DIB        *dibptr);
static SIGNALS_DATA abort_channel     (const char *reason);
static void         load_control      (HP_WORD    *value);
static void         load_address      (HP_WORD    *value);


/* Channel SCP data structures */


/* Unit list */

static UNIT sel_unit [] = {
    { UDATA (&sel_timer, 0, 0), SR_WAIT_TIMER }
    };

/* Register list */

static REG sel_reg [] = {
/*    Macro   Name    Location         Width  Offset  Flags             */
/*    ------  ------  ---------------  -----  ------  ----------------- */
    { FLDATA (IDLE,   sel_is_idle,              0)                      },
    { FLDATA (SREQ,   sel_request,              0)                      },
    { DRDATA (DEVNO,  device_number,     8),          PV_LEFT           },
    { DRDATA (EXCESS, excess_cycles,    32),          PV_LEFT           },
    { DRDATA (INDEX,  device_index,     32),          PV_LEFT | REG_HRO },

    { DRDATA (SEQ,    sequencer,         3)                             },
    { ORDATA (ORDER,  order,             4)                             },
    { FLDATA (ROLOVR, rollover,                 0)                      },
    { FLDATA (PFCNTL, prefetch_control,         0)                      },
    { FLDATA (PFADDR, prefetch_address,         0)                      },

    { ORDATA (BANK,   bank,              4),          PV_LEFT           },
    { DRDATA (WCOUNT, word_count,       12)                             },

    { ORDATA (PCNTR,  program_counter,  16),                  REG_FIT   },
    { ORDATA (CNTL,   control_word,     16),                  REG_FIT   },
    { ORDATA (CNBUF,  control_buffer,   16),                  REG_FIT   },
    { ORDATA (ADDR,   address_word,     16),                  REG_FIT   },
    { ORDATA (ADBUF,  address_buffer,   16),                  REG_FIT   },
    { ORDATA (INBUF,  input_buffer,     16),          REG_A | REG_FIT   },
    { ORDATA (OUTBUF, output_buffer,    16),          REG_A | REG_FIT   },

    { NULL }
    };

/* Debugging trace list */

static DEBTAB sel_deb [] = {
    { "CSRW",  DEB_CSRW  },                     /* channel command initiations and completions */
    { "PIO",   DEB_PIO   },                     /* programmed I/O commands executed */
    { "STATE", DEB_STATE },                     /* channel state changes executed */
    { "SR",    DEB_SR    },                     /* service requests received */
    { "DATA",  DEB_MDATA },                     /* I/O data accesses to memory */
    { NULL,    0         }
    };

/* Device descriptor */

DEVICE sel_dev = {
    "SEL",                                      /* device name */
    sel_unit,                                   /* unit array */
    sel_reg,                                    /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &sel_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    NULL,                                       /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    sel_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Channel global routines */



/* Initialize the channel.

   This routine is called in the CPU instruction execution prelude to allow the
   device number of the participating interface to be reassigned.  It also sets
   up the service request value from the device DIB.  This allows the device
   state to be changed during a simulation stop.

   Implementation notes:

    1. The active DIB pointer is restored from the device context to support
       resuming after a SAVE and RESTORE is performed.

    2. In simulation, we allow the device number to be changed during a
       simulation stop, so this routine must recover it from the device.
       Normally, the device number register would be reset from the device
       number field in the DIB.  However, the SCMB may be spoofing the device
       number, and it is this spoofed number that must be restored.  To do this,
       we first assert the DEVNODB signal to the interface.  The SCMB will
       respond to the DEVNODB signal, as it supports connection to the
       multiplexer channel.  Devices that connect only to the selector channel
       will not respond to DEVNODB, returning an outbound value of zero.  In
       this case, we use the DIB field to obtain the device number.
*/

void sel_initialize (void)
{
SIGNALS_DATA outbound;

if (sel_is_idle == FALSE) {                                         /* if the channel is controlling a device */
    active_dib = (DIB *) sim_devices [device_index]->ctxt;          /*   then restore the active DIB pointer */

    outbound = active_dib->io_interface (active_dib, DEVNODB, 0);   /* see if the device responds to DEVNODB */

    if (IODATA (outbound) > 0)                          /* if it does (e.g., the SCMB) */
        device_number = IODATA (outbound) / 4;          /*   then use the returned device number */
    else                                                /* otherwise */
        device_number = active_dib->device_number;      /*   use the device number from the DIB */

    sel_request = active_dib->service_request;          /* restore the service request state */
    }

return;
}


/* Start an I/O program.

   This routine is called by a device interface in response to a Start I/O (SIO)
   instruction to request that the selector channel begin an I/O program.  It
   corresponds in hardware to asserting the REQ signal.

   If REQ is asserted while the channel is servicing the interface, the channel
   aborts the transfer.  This occurs when an interface decides to terminate a
   transfer, for example when an error retry count has expired or a device has
   become not ready.


   Implementation notes:

    1. Both the HP 3000 Series II/III System Reference Manual and the HP 3000
       Series III Reference/Training manuals say that the selector channel
       asserts a DEVNODB signal as part of the REQ processing.  This is
       incorrect; the DEVNODB line is tied inactive by the channel, per the
       Selector Channel Control PCA schematic.  Instead, the channel expects the
       device number, multiplied by four, to be present on the SRn bus during
       REQ signal assertion, when it is loaded into the device number register.
       Selector channel devices gate their device numbers onto SR6-13 when an
       SIO instruction is decoded.
*/

void sel_assert_REQ (DIB *dibptr)
{
if (sel_is_idle) {                                      /* if the channel is idle then set it up */
    dprintf (sel_dev, DEB_CSRW, "Device number %u asserted REQ for channel initialization\n",
             dibptr->device_number);

    sel_is_idle = FALSE;                                /* the channel is now busy */
    sel_request = TRUE;                                 /* set the request flag */

    sequencer = Fetch_Sequence;                         /* initialize the sequencer */
    bank = 0;                                           /* set the bank to bank 0 */

    word_count = 0;                                     /* clear the word counter */
    rollover = CLEAR;                                   /*   and the word count rollover flip-flop */
    excess_cycles = 0;                                  /* clear the excess cycle count */

    device_index = 0;                                           /* find the device index */
                                                                /*   corresponding to */
    while ((DIB *) sim_devices [device_index]->ctxt != dibptr)  /*     the active DIB pointer */
        device_index = device_index + 1;                        /*       to aid later restoration */

    active_dib = dibptr;                                /* save the interface's DIB pointer */
    device_number = dibptr->device_number;              /*   and set the device number register */

    cpu_read_memory (absolute_sel, device_number * 4,   /* read the initial program counter from the DRT */
                     &program_counter);
    }

else {                                                  /* otherwise abort the transfer in progress */
    dprintf (sel_dev, DEB_CSRW, "Device number %u asserted REQ for channel abort\n",
             device_number);

    end_channel (dibptr);                               /* idle the channel */
    sim_cancel (&sel_unit [0]);                         /*   and cancel the CHANSR timer */
    }

return;
}


/* Request channel service.

   This routine is called by a device interface to request service from the
   channel.  It is called either directly by the interface or indirectly by the
   channel in response to a CHANSR signal returned by the interface.  A direct
   call is needed for asynchronous assertion, e.g., in response to an event
   service call.  Synchronous assertion, i.e., in response to an interface call,
   is made by returning the CHANSR to the channel.  The routine corresponds in
   hardware to asserting the CHANSR signal on the selector channel bus.

   Sets the service_request flag in the DIB and sets the sel_request flag to
   cause the channel sequencer to be invoked.
*/

void sel_assert_CHANSR (DIB *dibptr)
{
dprintf (sel_dev, DEB_SR, "Device number %u asserted CHANSR\n",
         device_number);

dibptr->service_request = TRUE;                         /* set the service request flag in the interface */
sel_request = TRUE;                                     /*   and the selector request flag */

return;
}


/* Invoke the channel sequencer in response to a service request.

   This routine is called in the CPU instruction execution loop to service a
   channel request, asserted either by the participating interface, or
   generated internally by the channel.  It executes one or more channel cycles
   for the associated device interface and resets the service request flag in
   the DIB before exiting.  The routine is called after every instruction, and
   sometimes additionally within instructions that have long execution times
   (e.g., MOVE).  The number of event ticks that have elapsed since the last
   call are passed in; this determines the number of channel cycles available to
   execute.

   The selector channel clock period is 175 nanoseconds.  The channel runs
   concurrently with the CPU, which executes instructions in an average of 2.57
   microseconds, so multiple cycles are executed per CPU instruction.

   On entry, the routine executes the next state in the transfer for the
   currently participating interface.  The number of channel clock counts
   consumed for the specified state execution is subtracted from the number of
   clock counts available.  If more time remains, and the service request is
   still active, another channel cycle is run for the interface.

   The actions for the orders are:

                          Transfer  Last
     SIO Order  State       Type    Word  Signals Asserted
     ---------  --------  --------  ----  -------------------------------
     sioJUMP    Execute      --      --   (none)

     sioJUMPC   Execute      --      --   SETJMP

     sioRTRES   Execute      --      --   (none)

     sioSBANK   Execute      --      --   (none)

     sioINTRP   Execute      --      --   SETINT

     sioEND     Execute      --      --   TOGGLESIOOK | PSTATSTB

     sioENDIN   Execute      --      --   TOGGLESIOOK | PSTATSTB | SETINT

     sioCNTL    Execute      --      --   PCMD1 | CHANSO
        "       Transfer     --      --   PCONTSTB

     sioSENSE   Execute      --      --   PSTATSTB

     sioWRITE   Execute      --      --   TOGGLEOUTXFER
                Transfer   Normal    No   PWRITESTB
                Transfer   Normal   Yes   PWRITESTB | EOT
                Transfer   DEVEND    No   EOT | TOGGLEOUTXFER
                Transfer   DEVEND   Yes   (none)

     sioWRITEC  Execute      --      --   TOGGLEOUTXFER
                Transfer   Normal    No   PWRITESTB
                Transfer   Normal   Yes   PWRITESTB | EOT | TOGGLEOUTXFER
                Transfer   DEVEND    No   EOT
                Transfer   DEVEND   Yes   (none)

     sioREAD    Execute      --      --   TOGGLEINXFER | READNEXTWD
                Transfer   Normal    No   PREADSTB | READNEXTWD
                Transfer   Normal   Yes   PREADSTB | EOT | READNEXTWD
                Transfer   Devend    No   EOT | TOGGLEINXFER
                Transfer   DEVEND   Yes   (none)

     sioREADC   Execute      --      --   TOGGLEINXFER | READNEXTWD
                Transfer   Normal    No   PREADSTB | READNEXTWD
                Transfer   Normal    Yes  PREADSTB | EOT | TOGGLEINXFER
                Transfer   Devend    No   EOT
                Transfer   DEVEND    Yes  (none)

   A fundamental difference between the multiplexer and selector channels is
   that the latter needs an external service request (i.e., CHANSR assertion)
   only for operations on the interface.  All other channel operations apply an
   internal service request and so occur automatically without CHANSR being
   asserted.  In simulation, sel_request is set TRUE during channel
   initialization and is only cleared when the channel is waiting for a response
   from the interface during a control, read, or write operation.  The following
   signal assertions to the interface must return CHANSR to permit the channel
   sequencer to advance:

     - PCMD1
     - PCONTSTB
     - TOGGLEINXFER | READNEXTWD
     - PREADSTB
     - TOGGLEOUTXFER
     - PWRITESTB

   Because the channel is dedicated to an interface for the duration of a
   transfer, a non-responding interface would tie up the channel forever.  To
   prevent this, the channel starts a one-millisecond timer whenever it is
   waiting for the interface to assert CHANSR.  If the timer expires, the
   transfer is aborted, and the channel is freed.  The channel also checks for
   CHANACK in response to CHANSO assertion to the interface and will terminate
   (but not abort) the transfer if the interface fails to return it.

   To maintain the maximum transfer rate across chained read or write transfers,
   the channel will attempt to prefetch the next set of I/O Control and Address
   words during the current data transfer.  The two memory reads are interleaved
   between successive channel data transfers, but only if the input or output
   data buffers are both empty (read) or full (write), respectively.  This will
   normally occur unless the device is using all available channel bandwidth;
   the SCMB in high speed mode is an example of a device that asserts SR
   continuously.

   The selector channel diagnostic checks for the expected prefetching.  With
   the SCMB in slow mode, both the control and address prefetches will occur
   within three (read) or five (write) I/O cycles.  In fast mode, prefetches are
   locked out during a write operation, and only the control word prefetch
   occurs during a read operation.


   Implementation notes:

    1. Conceptually, the selector channel has three internal states: initiate,
       fetch, and execute.  The initiator sequence begins with a REQ
       assertion from the interface and sets up the I/O program for execution.
       The fetch sequence obtains the IOCW and IOAW.  The execute sequence
       generates the signals needed for each I/O order.

       In hardware, the Selector Channel Sequencer PCA contains several state
       machines (initiator, fetch, prefetch, buffer transfer, etc.) that provide
       substates.  In simulation, the prefetch, transfer, and reload actions are
       given separate states, rather than making them substates of the execute
       state.

    2. In hardware, a ten-microsecond CHANACK timer is started when CHANSO is
       asserted to the interface.  In simulation, a timer is not needed;
       instead, the interface must return CHANACK to avoid a CHANACK timeout
       error.

    3. In hardware, the CHANACK and CHANSR timers run on every channel cycle.
       In simulation, CHANACK is tested only during a read or write transfer,
       and CHANSR is tested only at the beginning of a read or write transfer,
       and at the beginning and end of a control transfer.  This limits overhead
       while passing the diagnostic and providing some measure of protection.

    4. In hardware, there are two input and two output buffers.  This enables
       memory transfers to overlap with interface data transfers.  In
       simulation, only one of each buffer is implemented, and memory transfers
       occur synchronously with interface transfers.

    5. In hardware, prefetches of the IOCW and IOAW are interleaved with channel
       memory accesses and only occur when the port controller is idle.  In
       simulation, prefetches are not counted against the sequencer execution
       time.  Normal fetches, by contrast, wait for memory access and so the
       time taken is counted.

    6. The default label in the Execute Sequence switch statement is necessary
       to quiet a warning that inbound_signals may be used uninitialized, even
       though all cases are covered.
*/

void sel_service (uint32 ticks_elapsed)
{
HP_WORD      inbound_data, outbound_data;
INBOUND_SET  inbound_signals;
SIGNALS_DATA outbound;
int32        cycles;
uint32       return_address;

cycles = CYCLES_PER_EVENT - excess_cycles;              /* decrease the cycles available by any left over */

while (sel_request && cycles > 0) {                     /* execute as long as a request and cycles remain */
    outbound = IORETURN (NO_SIGNALS, 0);                /* initialize in case we don't call the interface */

    dprintf (sel_dev, DEB_STATE, "Channel entered the %s sequence with %d clock cycles remaining\n",
             seq_name [sequencer], cycles);

    switch (sequencer) {                                /* dispatch based on the selector state */

        case Idle_Sequence:                             /* if the selector is idle */
            sel_request = FALSE;                        /*   then the request is invalid */
            break;


        case Fetch_Sequence:
            sim_cancel (&sel_unit [0]);                 /* cancel the CHANSR timer */

            load_control (&control_word);               /* load the IOCW */
            load_address (&address_word);               /*   and the IOAW */
            cycles = cycles - 2 * CYCLES_PER_READ       /*     and count the accesses */
                       - CYCLES_PER_FETCH;              /*       and the fetch sequence */

            order = IOCW_ORDER (control_word);          /* save the current order */

            if (control_word & IOCW_DC                          /* if the data chain bit is set */
              && order != sioREADC && order != sioWRITEC)       /*   but the order isn't a chained order */
                outbound = abort_channel ("an illegal order");  /*     then abort the channel program */

            else                                                /* otherwise the order is valid */
                sequencer = Execute_Sequence;                   /*   and execution is next */
            break;


        case Execute_Sequence:
            switch (order) {                            /* dispatch based on the I/O order */

                case sioJUMPC:
                    inbound_signals = SETJMP | CHANSO;
                    break;

                case sioRTRES:
                    inbound_signals = NO_SIGNALS;               /* no interface call is needed */

                    if (rollover == SET)                        /* if the count terminated */
                        outbound = IORETURN (NO_SIGNALS, 0);    /*   then return a zero count */
                    else                                        /* otherwise return the two's-complement remainder */
                        outbound = IORETURN (NO_SIGNALS,
                                             IOCW_COUNT (word_count));
                    break;

                case sioINTRP:
                    inbound_signals = SETINT | CHANSO;
                    break;

                case sioEND:
                    inbound_signals = TOGGLESIOOK | PSTATSTB | CHANSO;
                    break;

                case sioENDIN:
                    inbound_signals = TOGGLESIOOK | PSTATSTB | SETINT | CHANSO;
                    break;

                case sioCNTL:
                    inbound_signals = PCMD1 | CHANSO;

                    sel_request = FALSE;                /* wait until the interface requests the next word */
                    break;

                case sioSENSE:
                    inbound_signals = PSTATSTB | CHANSO;
                    break;

                case sioWRITE:
                case sioWRITEC:
                    inbound_signals = TOGGLEOUTXFER | CHANSO;

                    word_count = IOCW_WCNT (control_word);  /* load the word count */
                    sel_request = FALSE;                    /* wait until the interface requests the next word */
                    break;

                case sioREAD:
                case sioREADC:
                    inbound_signals = TOGGLEINXFER | READNEXTWD | CHANSO;

                    word_count = IOCW_WCNT (control_word);  /* load the word count */
                    sel_request = FALSE;                    /* wait until the interface requests the next word */
                    break;

                default:                                /* needed to quiet warning about inbound_signals */
                case sioJUMP:                           /* these orders do not need */
                case sioSBANK:                          /*   to call the interface */
                    inbound_signals = NO_SIGNALS;
                    break;
                }                                       /* end switch */

            if (inbound_signals) {                                  /* if there are signals to assert */
                outbound = active_dib->io_interface (active_dib,    /*   then pass them to the interface */
                                                     inbound_signals,
                                                     control_word);

                if ((outbound & CHANACK) == NO_SIGNALS) {   /* if CHANACK was not returned */
                    dprintf (sel_dev, DEB_SR, "Device number %u CHANACK timeout\n",
                             device_number);

                    end_channel (active_dib);               /* terminate the channel program */

                    dprintf (sel_dev, DEB_CSRW, "Channel program ended\n");
                    break;
                    }
                }

            switch (order) {                            /* dispatch based on the I/O order */

                case sioJUMP:
                    program_counter = address_word;     /* load the program counter with the new address */
                    sequencer = Fetch_Sequence;         /* next state is Fetch */
                    break;

                case sioJUMPC:
                    if (outbound & JMPMET)              /* if the jump condition is true */
                        program_counter = address_word; /*   then load the program counter with the new address */

                    sequencer = Fetch_Sequence;         /* the next state is Fetch */
                    break;

                case sioRTRES:
                case sioEND:
                case sioENDIN:
                case sioSENSE:
                    outbound_data = IODATA (outbound);              /* get the status or residue to return */
                    return_address = program_counter - 1 & LA_MASK; /* point at the second of the program words */

                    cpu_write_memory (absolute_sel, return_address, outbound_data); /* save the word */
                    cycles = cycles - CYCLES_PER_WRITE;                             /*   and count the access */

                    dprintf (sel_dev, DEB_PIO, "Channel stored IOAW %06o to address %06o\n",
                             outbound_data, return_address);

                    if (order == sioEND || order == sioENDIN) {     /* if it's an End or End with Interrupt order */
                        end_channel (active_dib);                   /*   then terminate the program */

                        dprintf (sel_dev, DEB_CSRW, "Channel program ended\n");
                        }

                    else                                /* otherwise the program continues */
                        sequencer = Fetch_Sequence;     /*   with the fetch state */
                    break;

                case sioSBANK:
                    bank = IOAW_BANK (address_word);    /* set the bank number register */
                    sequencer = Fetch_Sequence;         /* the next state is Fetch */
                    break;

                case sioINTRP:
                    sequencer = Fetch_Sequence;         /* the next state is Fetch */
                    break;

                case sioCNTL:
                    prefetch_control = FALSE;                   /* prefetching is not used */
                    prefetch_address = FALSE;                   /*   for the Control order */

                    sim_activate (&sel_unit [0], sel_unit [0].wait);    /* start the SR timer */
                    sequencer = Wait_Sequence;                          /*   and check for a timeout */
                    break;

                case sioWRITE:
                case sioWRITEC:
                    prefetch_control = (order == sioWRITEC);    /* enable prefetching */
                    prefetch_address = (order == sioWRITEC);    /*   if the order is chained */

                    sim_activate (&sel_unit [0], sel_unit [0].wait);    /* start the SR timer */
                    sequencer = Wait_Sequence;                          /*   and check for a timeout */
                    break;

                case sioREAD:
                case sioREADC:
                    prefetch_control = (order == sioREADC);     /* enable prefetching */
                    prefetch_address = (order == sioREADC);     /*   if the order is chained */

                    if (prefetch_control) {                     /* if control word prefetching is enabled */
                        load_control (&control_buffer);         /*   then prefetch the next IOCW into the buffer */
                        cycles = cycles - CYCLES_PER_PREFETCH;  /*     and count the sequencer time */
                        prefetch_control = FALSE;               /* mark the job done */
                        }

                    sim_activate (&sel_unit [0], sel_unit [0].wait);    /* start the SR timer */
                    sequencer = Wait_Sequence;                          /*   and check for a timeout */
                    break;
                }                                               /* end switch */

            cycles = cycles - CYCLES_PER_EXECUTE;               /* count the sequencer time */
            break;


        case Wait_Sequence:
            sim_cancel (&sel_unit [0]);                 /* cancel the SR timer */

            sequencer = Transfer_Sequence;              /* continue with the transfer sequence */

        /* fall into the Transfer_Sequence */


        case Transfer_Sequence:
            if (order == sioCNTL) {                     /* if this is a Control order */
                inbound_data = address_word;            /*   then supply the control word */
                inbound_signals = PCONTSTB | CHANSO;    /*     to the interface */
                sel_request = FALSE;                    /* wait until the interface confirms receipt */
                }

            else if (order == sioREAD || order == sioREADC) {   /* otherwise if this is a Read or Read Chained order */
                inbound_data = 0;                               /*   then no value is needed */
                inbound_signals = PREADSTB | CHANSO;            /*     by the interface */

                if (word_count == CNTR_MAX)                     /* if the word count is exhausted */
                    if (order == sioREADC)                      /*   then if the order is chained */
                        inbound_signals |= EOT | READNEXTWD;    /*     then continue the transfer block */
                    else                                        /*   otherwise */
                        inbound_signals |= EOT | TOGGLEINXFER;  /*     end the transfer block */

                else                                            /* otherwise the transfer continues */
                    inbound_signals |= READNEXTWD;              /*   with the next word */

                sel_request = FALSE;                            /* wait until the interface confirms receipt */
                }

            else {                                                  /* otherwise it's a Write or Write Chained order */
                if (cpu_read_memory (dma_sel,                       /* if the memory read */
                                     TO_PA (bank, address_word),    /*   from the specified bank and offset */
                                     &input_buffer)) {              /*     succeeds */
                    cycles = cycles - CYCLES_PER_READ;              /*       then count the access */

                    inbound_data = input_buffer;                    /* get the word to supply */
                    inbound_signals = PWRITESTB | CHANSO;           /*   to the interface */

                    if (word_count == CNTR_MAX)                     /* if the word count is exhausted */
                        if (order == sioWRITEC)                     /*   then if the order is chained */
                            inbound_signals |= EOT;                 /*     then continue the transfer block */
                        else                                        /*   otherwise */
                            inbound_signals |= EOT | TOGGLEOUTXFER; /*     end the transfer block */

                    sel_request = FALSE;                            /* wait until the interface confirms receipt */
                    }

                else {                                                  /* otherwise the memory read failed */
                    outbound = abort_channel ("a memory read error");   /*   so abort the transfer */
                    break;                                              /*     and skip the interface call */
                    }
                }

            cycles = cycles - CYCLES_PER_EXECUTE;               /* count the sequencer time */

            outbound = active_dib->io_interface (active_dib, inbound_signals, inbound_data);    /* call the interface */

            if (sel_is_idle)                            /* if the interface aborted the transfer */
                break;                                  /*   then terminate processing now */

            if ((outbound & CHANSR) == NO_SIGNALS)          /* if the interface did not assert a service request */
                if (prefetch_control) {                     /*   then if control word prefetching is enabled */
                    load_control (&control_buffer);         /*     then prefetch the next IOCW into the buffer */
                    cycles = cycles - CYCLES_PER_PREFETCH;  /*       and count the sequencer time */
                    prefetch_control = FALSE;               /* mark the job done */
                    }

                else if (prefetch_address) {                /*   otherwise if address word prefetching is enabled */
                    load_address (&address_buffer);         /*     then prefetch the next IOAW into the buffer */
                    cycles = cycles - CYCLES_PER_PREFETCH;  /*       and count the sequencer time */
                    prefetch_address = FALSE;               /* mark the job done */
                    }

            if (order == sioCNTL) {                                 /* if this is a Control order */
                sim_activate (&sel_unit [0], sel_unit [0].wait);    /*   then start the SR timer */
                sequencer = Fetch_Sequence;                         /*     and the next state is Fetch */
                }

            else {                                              /* otherwise it's a Write or Read (Chained) order */
                if (outbound & DEVEND) {                        /* if the device ended the transfer */
                    if (word_count < CNTR_MAX) {                /*   then if the transfer is incomplete */
                        inbound_signals = EOT | CHANSO;         /*     then assert EOT to end the transfer */

                        if (order == sioREAD)                   /* if the order is Read and not chained */
                            inbound_signals |= TOGGLEINXFER;    /*   then terminate the input block */

                        else if (order == sioWRITE)             /* otherwise if the order is Write and not chained */
                            inbound_signals |= TOGGLEOUTXFER;   /*   then terminate the output block */

                        active_dib->io_interface (active_dib, inbound_signals, 0);  /* tell the interface */
                        }

                    sequencer = Reload_Sequence;                /* the next state is Reload */
                    }

                else if (order == sioREAD               /* otherwise the transfer was successful */
                  || order == sioREADC) {               /*   and if this is a Read or Read Chained order */
                    output_buffer = IODATA (outbound);  /*     then pick up the returned data word */

                    if (cpu_write_memory (dma_sel,                      /* if the memory write */
                                          TO_PA (bank, address_word),   /*   to the specified bank and offset */
                                          output_buffer))               /*     succeeds */
                        cycles = cycles - CYCLES_PER_WRITE;             /*       then count the access */

                    else {                                                  /* otherwise the memory write failed */
                        outbound = abort_channel ("a memory write error");  /*   so abort the transfer */
                        break;                                              /*     and skip the address and count update */
                        }
                    }

                address_word = address_word + 1 & LA_MASK;  /* increment the transfer address */
                word_count = word_count + 1 & CNTR_MASK;    /*   and the word count */

                if (word_count == 0) {                      /* if the word count is exhausted */
                    rollover = SET;                         /*   then set the rollover flip-flop */
                    sequencer = Reload_Sequence;            /*     and load the next I/O program word */
                    }
                }

            break;


        case Reload_Sequence:
            if (order == sioWRITEC || order == sioREADC) {  /* if the current order is chained */
                if (prefetch_control) {                     /*   and the IOCW has not been prefetched yet */
                    load_control (&control_buffer);         /*     then load it now */
                    cycles = cycles - CYCLES_PER_READ;      /*       and count the memory access */
                    }

                if (prefetch_address) {                     /* if the IOAW has not be prefetched yet */
                    load_address (&address_buffer);         /*   then load it now */
                    cycles = cycles - CYCLES_PER_READ;      /*       and count the memory access */
                    }

                if (prefetch_control || prefetch_address)   /* if both words had not been prefetched */
                    cycles = cycles - CYCLES_PER_FETCH;     /*   then count as a fetch sequence */
                else                                        /* otherwise */
                    cycles = cycles - CYCLES_PER_RELOAD;    /*   count as a reload sequence */

                if ((control_word ^ control_buffer) & IOCW_SIO_MASK)        /* if the next order isn't the same type */
                    outbound = abort_channel ("an invalid chained order");  /*   then an invalid order abort occurs */

                else {                                      /* otherwise the next order is OK */
                    control_word = control_buffer;          /*   so copy the control and address values */
                    address_word = address_buffer;          /*     from the buffers */

                    order = IOCW_ORDER (control_word);      /* get the new order */
                    word_count = IOCW_WCNT (control_word);  /*   and word count */

                    rollover = CLEAR;                       /* clear the word count rollover flip-flop */

                    prefetch_control = (control_word & IOCW_DC) != 0;   /* enable prefetching */
                    prefetch_address = (control_word & IOCW_DC) != 0;   /*   if the new order is chained */

                    sequencer = Transfer_Sequence;          /* the next state is Transfer */
                    }
                }

            else                                        /* otherwise an unchained order ends the transfer */
                sequencer = Fetch_Sequence;             /*   so proceed directly to Fetch */

            break;
        }                                               /* end switch */


    if (outbound & INTREQ)                              /* if an interrupt request was asserted */
        iop_assert_INTREQ (active_dib);                 /*   then set it up */

    if (sel_is_idle == FALSE)                           /* if the channel is still running */
        if (outbound & CHANSR)                          /*   then if the interface requested service */
            sel_assert_CHANSR (active_dib);             /*     then set it up */
        else                                            /*   otherwise */
            active_dib->service_request = FALSE;        /*     clear the current service request */

    else                                                /* otherwise the channel has stopped */
        sim_cancel (&sel_unit [0]);                     /*   so cancel the CHANSR timer */

    }                                                   /* end while */


if (cycles > 0)                                         /* if we exited to wait for a service request */
    excess_cycles = 0;                                  /*   then do a full set of cycles next time */
else                                                    /* otherwise we ran over our allotment */
    excess_cycles = - cycles;                           /*   so reduce the next poll by the overage */

return;
}



/* Channel local SCP support routines */



/* Service the channel service request timer.

   The CHANSR timer is started whenever the channel is waiting for a service
   request from the participating interface.  Because the selector channel is
   dedicated to a single interface until the end of the I/O program, if that
   interface were to malfunction and not respond, the channel would be tied up
   forever.

   Normally, the timer is cancelled as soon as CHANSR is returned from the
   interface.  If this service routine is entered, it means that CHANSR is
   taking too long, so the I/O program is aborted, and the channel is idled, so
   that it is available for other devices.
*/

static t_stat sel_timer (UNIT *uptr)
{
SIGNALS_DATA outbound;

outbound = abort_channel ("a CHANSR timeout");          /* abort the transfer in progress */

if (outbound & INTREQ)                                  /* if an interrupt request was asserted */
    iop_assert_INTREQ (active_dib);                     /*   then set it up */

return SCPE_OK;
}


/* Device reset.

   This routine is called for a RESET or RESET SEL command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to the internal Clear Logic signal.

   A reset does not clear any of the registers.
*/

static t_stat sel_reset (DEVICE *dptr)
{
rollover = CLEAR;                                       /* clear the word count rollover flip-flop */

sel_is_idle = TRUE;                                     /* the channel is now inactive */
sel_request = FALSE;                                    /* clear the request flag */

sequencer = Idle_Sequence;                              /* stop the sequencer */

return SCPE_OK;
}



/* Channel local utility routines */



/* End the channel I/O program.

   The channel program ends, either normally via an sioEND or sioENDIN order, or
   abnormally via a REQ or timeout abort.  The program counter is written back
   to the DRT, and the channel is idled by performing a Clear Logic operation.


   Implementation notes:

    1. The memory write cycle time need not be counted, as the channel will be
       terminating unconditionally.
*/

static void end_channel (DIB *dibptr)
{
cpu_write_memory (absolute_sel, device_number * 4,      /* write the program counter back to the DRT */
                  program_counter);

dibptr->service_request = FALSE;                        /* clear any outstanding device service request */

sel_reset (&sel_dev);                                   /* perform a Clear Logic operation */

return;
}


/* Abort the transfer in progress.

   If an internal channel error occurs (e.g., a memory read or write failure,
   due to an invalid address), the channel asserts the XFERERROR signal to the
   interface.  The interface will clear its logic and assert REQ to the channel
   to complete the abort.
*/

static SIGNALS_DATA abort_channel (const char *reason)
{
dprintf (sel_dev, DEB_CSRW, "Channel asserted XFERERROR for %s\n",
         reason);

return active_dib->io_interface (active_dib, XFERERROR | CHANSO, 0);    /* tell the interface that the channel has aborted */
}


/* Load the I/O Control Word.

   The first of the two I/O program words is loaded into the channel register
   pointed to by "value".  The program counter points at the location to read
   and is incremented after retrieving the value.  This routine is called both
   for a normal fetch and for a prefetch.
*/

static void load_control (HP_WORD *value)
{
cpu_read_memory (absolute_sel, program_counter, value); /* read the IOCW from memory */

dprintf (sel_dev, DEB_PIO, "Channel %s IOCW %06o (%s) from address %06o\n",
         action_name [sequencer], *value,
         sio_order_name [IOCW_ORDER (*value)], program_counter);

program_counter = program_counter + 1 & LA_MASK;        /* increment the program counter */

return;
}


/* Load the I/O Address Word.

   The second of the two I/O program words is loaded into the channel register
   pointed to by "value".  The program counter points at the location to read
   and is incremented after retrieving the value.  This routine is called both
   for a normal fetch and for a prefetch.
*/

static void load_address (HP_WORD *value)
{
cpu_read_memory (absolute_sel, program_counter, value); /* read the IOAW from memory */

dprintf (sel_dev, DEB_PIO, "Channel %s IOAW %06o from address %06o\n",
         action_name [sequencer], *value, program_counter);

program_counter = program_counter + 1 & LA_MASK;        /* increment the program counter */

return;
}
