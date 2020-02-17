/* hp3000_io.h: HP 3000 device-to-IOP/MPX/SEL interface declarations

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

   30-Sep-18    JDB     Corrected typo in I/O macro comments
   12-Sep-16    JDB     Added the DIB_REGS macro
   02-Sep-16    JDB     Added the iop_assert_PFWARN routine
   11-Jun-16    JDB     Bit mask constants are now unsigned
   21-Mar-16    JDB     Changed type of inbound_value of CNTLR_INTRF to HP_WORD
   20-Jan-16    JDB     First release version
   11-Dec-12    JDB     Created


   This file contains declarations used by I/O devices to interface with the HP
   3000 I/O Processor, Multiplexer Channel, and Selector Channel.  It is
   required by any module that uses Device Information Blocks (DIBs).
*/



/* I/O bus signals.

   The INBOUND_SIGNAL and OUTBOUND_SIGNAL declarations mirror the hardware
   signals that are received and asserted, respectively, by the I/O interfaces
   on the IOP, selector/multiplexer channel, and power buses.  A set of one or
   more signals forms an INBOUND_SET or OUTBOUND_SET that is sent to or returned
   from a device interface.  Under simulation, the IOP and channels dispatch one
   INBOUND_SET to the target device interface per I/O cycle.  The interface
   returns a combined OUTBOUND_SET and data value to the caller.

   Hardware allows parallel action for concurrent signals.  Under simulation, a
   "concurrent" set of signals is processed sequentially by the interface in
   order of ascending numerical value.

   In addition, some signals must be asserted asynchronously, e.g., in response
   to an event service call.  The IOP and channels provide asynchronous
   assertion via function calls for the INTREQ, REQ, SRn, and CHANSR signals.


   Implementation notes:

    1. The enumerations describe signals.  A set of signals normally would be
       modeled as an unsigned integer, as a set may contain more than one
       signal.  However, we define a set as the enumeration, as the "gdb"
       debugger has special provisions for an enumeration of discrete bit values
       and will display the set in "ORed" form.

    2. The null set -- NO_SIGNALS -- cannot be defined as an enumeration
       constant because the C language has a single name space for all
       enumeration constants, so separate "no inbound signals" and "no outbound
       signals" identifiers would be required, and because including them would
       require handlers for them in "switch" statements, which is undesirable.
       Therefore, we define NO_SIGNALS as an explicit integer 0, so that it is
       compatible with both enumerations.

    3. Outbound signal values are restricted to the upper 16 bits to allow the
       combined signal/data value to fit in 32 bits.

    4. Inbound and outbound signal definitions are separated to allow for future
       inbound expansion, if necessary.

    5. In hardware, the IOP encodes direct I/O commands as a 3-bit IOCMD signal
       set on the IOP bus.  Each device interface decodes these signals into
       individual strobes to control the logic.  Under simulation, the IOCMD
       values are decoded by the IOP into individual signals for inclusion in
       the INBOUND_SIGNAL set that is passed to the interfaces.

    6. The ACKSR signal must come before the programmed I/O and TOGGLESR
       signals, as they may set an interface's Service Request flip-flop.

    7. The READNEXTWD signal must come after PREADSTB, as the former overwrites
       the input data word used by the latter.

    8. The TOGGLEnXFER signals must come after PREADSTB and PWRITESTB and before
       READNEXTWD, so that the strobes can test the interface's Device End
       flip-flop before the toggles can reset it.

    9. The EOT signal must come after PREADSTB and PWRITESTB and before the
       TOGGLEnXFER signals.  The former condition is required for the SCMB to
       return the correct EOT count, and the latter is required for the DS to
       set its End-of-Data flip-flop correctly.

   10. The SETINT signal must come before, and the TOGGLESIOOK signal must come
       after, the PSTATSTB signal so that the status of the interrupt request
       and SIO Busy flip-flops can be reported correctly.

   11. The CHANSO signal must come after all programmed I/O signals, as it is
       used by channel devices to assert CHANSR when needed.
*/

#define NO_SIGNALS          0                   /* a universal "no signals are asserted" value */

typedef enum {                                  /* --- source of signal --- */
    DSETINT       = 000000000001,               /*   SIN instruction */
    DCONTSTB      = 000000000002,               /*   CIO instruction */
    DSTARTIO      = 000000000004,               /*   SIO instruction */
    DWRITESTB     = 000000000010,               /*   WIO instruction */
    DRESETINT     = 000000000020,               /*   IXIT instruction */
    DSTATSTB      = 000000000040,               /*   TIO instruction */
    DSETMASK      = 000000000100,               /*   SMSK instruction */
    DREADSTB      = 000000000200,               /*   RIO instruction */
    ACKSR         = 000000000400,               /*   Multiplexer SR response */
    TOGGLESR      = 000000001000,               /*   Read/Write/Control/End order */
    SETINT        = 000000002000,               /*   Interrupt/End channel order */
    PCMD1         = 000000004000,               /*   Control channel order */
    PCONTSTB      = 000000010000,               /*   Control channel order */
    SETJMP        = 000000020000,               /*   Jump channel order */
    PSTATSTB      = 000000040000,               /*   Sense channel order */
    PWRITESTB     = 000000100000,               /*   Write channel order */
    PREADSTB      = 000000200000,               /*   Read channel order */
    EOT           = 000000400000,               /*   Read/Write channel order */
    TOGGLEINXFER  = 000001000000,               /*   Read channel order */
    TOGGLEOUTXFER = 000002000000,               /*   Write channel order */
    READNEXTWD    = 000004000000,               /*   Read channel order */
    TOGGLESIOOK   = 000010000000,               /*   End channel order */
    DEVNODB       = 000020000000,               /*   Multiplexer DRT Fetch */
    INTPOLLIN     = 000040000000,               /*   IOP interrupt poll */
    XFERERROR     = 000100000000,               /*   Multiplexer channel abort */
    CHANSO        = 000200000000,               /*   Channel service call to interface */
    PFWARN        = 000400000000                /*   POWER FAIL command */
/*                = 001000000000                     (available) */
/*                = 002000000000                     (available) */
/*                = 004000000000                     (available) */
/*                = 010000000000                     (available) */
/*                = 020000000000                     (available) */
    } INBOUND_SIGNAL;

typedef INBOUND_SIGNAL      INBOUND_SET;        /* a set of INBOUND_SIGNALs */


typedef enum {                                  /* --- destination of signal --- */
    INTREQ       = 000000200000,                /*   IOP, to request an external interrupt */
    INTACK       = 000000400000,                /*   IOP, to acknowledge an external interrupt request */
    INTPOLLOUT   = 000001000000,                /*   IOP, to clear an external interrupt request */
    DEVEND       = 000002000000,                /*   Channel, to terminate a read/write order */
    JMPMET       = 000004000000,                /*   Channel, to enable a Conditional Jump order */
    CHANACK      = 000010000000,                /*   Channel, to acknowledge interface call */
    CHANSR       = 000020000000,                /*   Selector channel, to request service */
    SRn          = 000040000000                 /*   Multiplexer channel, to request service */
/*               = 000100000000                      (available) */
/*               = 000200000000                      (available) */
/*               = 000400000000                      (available) */
/*               = 001000000000                      (available) */
/*               = 002000000000                      (available) */
/*               = 004000000000                      (available) */
/*               = 010000000000                      (available) */
/*               = 020000000000                      (available) */
    } OUTBOUND_SIGNAL;

typedef OUTBOUND_SIGNAL     OUTBOUND_SET;       /* a set of OUTBOUND_SIGNALs */


typedef uint32              SIGNALS_DATA;       /* a combined outbound signal set and data value */


/* I/O macros.

   The following macros are useful in device interface signal handlers and unit
   service routines.  The parameter definition symbols employed are:

     P = a priority set value
     S = an INBOUND_SET or OUTBOUND_SET value
     L = an INBOUND_SIGNAL value
     D = an outbound 16-bit data value
     C = a SIGNALS_DATA value
     B = a DIB value

   A priority set is an unsigned value, where each bit represents an assertion
   of some nature (e.g., I/O signals, interrupt requests, etc.), and the
   position of the bit represents its priority, which decreases from LSB to MSB.
   The IOPRIORITY macro isolates the highest-priority bit from the set.  It does
   this by ANDing the value with its two's complement; only the lowest-order bit
   will differ.  For example (bits are numbered here from the LSB):

     priority set :  ...0 0 1 1 0 1 0 0 0 0 0 0  (bits 6, 8, and 9 are asserted)
     one's compl  :  ...1 1 0 0 1 0 1 1 1 1 1 1
     two's compl  :  ...1 1 0 0 1 1 0 0 0 0 0 0
     ANDed value  :  ...0 0 0 0 0 1 0 0 0 0 0 0  (bit 6 is highest priority)

   If the request set indicates requests by 0 bits, rather than 1 bits, the
   IOPRIORITY macro must be called with the one's complement of the bits.

   The IONEXTSIG macro isolates the next inbound signal in sequence to process
   from the inbound signal set S.

   The IOCLEARSIG macro removes the processed signal L from the inbound signal
   set S.

   The IORETURN macro forms the 32-bit combined outbound signal set and data
   value to be returned by an interface from the signal set S and the 16-bit
   data value D.

   The IOSIGNALS macro isolates the outbound signal set from a 32-bit combined
   status and data value value C.

   The IODATA macro isolates the 16-bit data value from a 32-bit combined signal
   set and data value value C.


   Implementation notes:

    1. The IOPRIORITY macro implements two's complement explicitly, rather than
       using a signed negation, to be compatible with machines using a
       sign-magnitude integer format.  "gcc" and "clang" optimize the complement
       and add to a single NEG instruction on x86 machines.
*/

#define IOPRIORITY(P)       ((P) & ~(P) + 1)

#define IONEXTSIG(S)        ((INBOUND_SIGNAL) IOPRIORITY (S))
#define IOCLEARSIG(S,L)     S = (INBOUND_SIGNAL) ((S) ^ (L))

#define IORETURN(S,D)       ((SIGNALS_DATA) ((S) & ~D16_MASK | (D) & D16_MASK))
#define IOSIGNALS(C)        ((OUTBOUND_SET) ((C) & ~D16_MASK))
#define IODATA(C)           ((HP_WORD) ((C) & D16_MASK))


/* I/O structures.

   The Device Information Block (DIB) allows devices to be relocated in the
   machine's I/O space.  Each DIB contains a pointer to the device controller
   interface routine, values corresponding to hardware jumpers on the controller
   (e.g., device number), and flip-flops that indicate the interrupt and channel
   service states.

   For fast access during I/O, interrupt, and channel service requests, devices
   are accessed via indexed tables.  The index employed depends on the
   application.  For example, I/O commands are routed via a table that is
   indexed by device number.  The tables are built during the instruction
   execution prelude by scanning the DIBs of all devices and placing pointers to
   the DIBs into the tables at the entries associated with the index values.
   Between execution runs, the user may reassign device properties, so the
   tables must be rebuilt each time.


   Implementation notes:

    1. The device number (DEVNO) bus is eight bits in width, and the CPU
       microcode, the IOP, and the device controllers all support device numbers
       up to 255.  However, MPE limits the size of the device reference table to
       correspond with a device number of 127, while the CPU reserves memory
       that would correspond to device numbers 0-2.  As a result, most device
       controllers provide only seven-bit configurable device numbers.  One
       exception is the Selector Channel Maintenance Board.  The Selector
       Channel diagnostic tests programmable device numbers > 127, which the
       SCMB provides via bits 8-15 of the counter/buffer register, although only
       seven preset jumpers are provided to set the standard device number for
       the board.

    2. The device_number, service_request_number, and interrupt_priority fields
       could be smaller than the defined 32-bit sizes, but IA-32 processors
       execute instructions with 32-bit operands much faster than those with
       16- or 8-bit operands.

    3. The DIB_REGS macro provides hidden register entries needed to save and
       restore the state of a DIB.  Only the potentially variable fields are
       referenced.  In particular, the "io_interface" field must not be saved,
       as the address of the device's interface routine may change from version
       to version of the simulator.
*/

#define DEVNO_MAX           127                 /* the maximum device number */
#define DEVNO_MASK          0177u               /* the mask for the device number */
#define DEVNO_BASE          10                  /* the radix for the device number */
#define DEVNO_UNUSED        D32_UMAX            /* the unused device number indicator */

#define INTMASK_MAX         15                  /* the maximum interrupt mask number */
#define INTMASK_MASK        017u                /* the mask for the interrupt mask number */
#define INTMASK_BASE        10                  /* the radix for the interrupt mask number */
#define INTMASK_D           0000000u            /* the interrupt mask disabled always value */
#define INTMASK_E           0177777u            /* the interrupt mask enabled always value */
#define INTMASK_UNUSED      D32_UMAX            /* the unused interrupt mask indicator */

#define INTPRI_MAX          31                  /* the maximum interrupt priority */
#define INTPRI_MASK         037u                /* the mask for the interrupt priority */
#define INTPRI_BASE         10                  /* the radix for the interrupt priority */
#define INTPRI_UNUSED       D32_UMAX            /* the unused interrupt priority indicator */

#define SRNO_MAX            15                  /* the maximum service request number */
#define SRNO_MASK           017u                /* the mask for the service request number */
#define SRNO_BASE           10                  /* the radix for the service request number */
#define SRNO_UNUSED         D32_UMAX            /* the unused service request number indicator */

typedef struct dib          DIB;                /* an incomplete definition */

typedef SIGNALS_DATA CNTLR_INTRF                /* the I/O device controller interface function prototype */
    (DIB         *dibptr,                       /*   a pointer to the device information block */
     INBOUND_SET inbound_signals,               /*   a set of inbound signals */
     HP_WORD     inbound_value);                /*   a 16-bit inbound value */

struct dib {                                    /* the Device Information Block */
    CNTLR_INTRF *io_interface;                  /*   the controller I/O interface function pointer */
    uint32      device_number;                  /*   the device number 0-255 */
    uint32      service_request_number;         /*   the service request number 0-15 */
    uint32      interrupt_priority;             /*   the interrupt priority 0-31 */
    uint32      interrupt_mask;                 /*   the interrupt mask (16 bits) */
    uint32      card_index;                     /*   the card index if multiple interfaces are supported */
    FLIP_FLOP   interrupt_request;              /*   an interrupt has been requested */
    FLIP_FLOP   interrupt_active;               /*   an interrupt is active */
    t_bool      service_request;                /*   channel service has been requested */
    };

#define DIB_REGS(dib) \
/*    Macro   Name     Location                    Width  Flags   */ \
/*    ------  -------  --------------------------  -----  ------- */ \
    { DRDATA (DIBDN,   dib.device_number,           32),  REG_HRO }, \
    { DRDATA (DIBSRN,  dib.service_request_number,  32),  REG_HRO }, \
    { DRDATA (DIBPRI,  dib.interrupt_priority,      32),  REG_HRO }, \
    { ORDATA (DIBMASK, dib.interrupt_mask,          32),  REG_HRO }, \
    { ORDATA (DIBIRQ,  dib.interrupt_request,       32),  REG_HRO }, \
    { ORDATA (DIBACT,  dib.interrupt_active,        32),  REG_HRO }, \
    { ORDATA (DIBSR,   dib.service_request,         32),  REG_HRO }


/* Calibrated timer numbers */

#define TMR_PCLK            0                   /* the CPU process clock timer */
#define TMR_CLK             1                   /* the CLK system clock timer */
#define TMR_ATC             2                   /* the ATC input polling timer */


/* CPU front panel command identifiers */

typedef enum {
    Run,                                        /* a run request */
    Cold_Load,                                  /* a cold load request */
    Cold_Dump                                   /* a cold dump request */
    } PANEL_TYPE;


/* Global CPU state and functions */

extern UNIT  *cpu_pclk_uptr;                            /* pointer to the process clock unit */
extern t_bool cpu_is_calibrated;                        /* TRUE if the process clock is calibrated */

extern void cpu_front_panel (HP_WORD    switch_reg,     /* set the CPU front panel switches as directed */
                             PANEL_TYPE request);


/* Global asynchronous signal assertion functions */

extern void iop_assert_INTREQ (DIB *dib_pointer);       /* assert the interrupt request signal */
extern void iop_assert_PFWARN (void);                   /* assert the power failure warning signal */

extern void mpx_assert_REQ    (DIB *dib_pointer);       /* assert the multiplexer channel request signal */
extern void mpx_assert_SRn    (DIB *dib_pointer);       /* assert the multiplexer channel service request signal */

extern void sel_assert_REQ    (DIB *dib_pointer);       /* assert the selector channel request signal */
extern void sel_assert_CHANSR (DIB *dib_pointer);       /* assert the selector channel service request signal */


/* Global channel state */

extern t_bool mpx_is_idle;                              /* TRUE if the multiplexer channel is idle */
extern t_bool sel_is_idle;                              /* TRUE if the selector channel is idle */


/* Global ATC state */

extern t_bool atc_is_polling;                           /* TRUE if the ATC is polling for the simulation console */


/* Global CLK functions */

extern void clk_update_counter (void);                  /* update the system clock counter register */
