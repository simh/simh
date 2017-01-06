/* hp3000_iop.c: HP 3000 30003B I/O Processor simulator

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

   IOP          HP 3000 Series III I/O Processor

   03-Sep-16    JDB     Added "iop_assert_PFWARN" to warn devices of power loss
   01-Aug-16    JDB     Added "iop_reset" to initialize the IOP
   30-Jun-16    JDB     Changed REG type of filter array to BRDATA
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   13-May-16    JDB     Modified for revised SCP API function parameter types
   28-Aug-15    JDB     First release version
   11-Dec-12    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 30003B I/O Processor is an integral part of the HP 3000 system.  It
   works in conjunction with the CPU and Multiplexer Channel to service the
   device interfaces.  All I/O interfaces are connected to the IOP bus, which
   transfers programmed I/O orders to the interfaces and handles memory reads
   and writes between the interfaces and the CPU stack.  In addition, it
   provides the memory interface for multiplexer channel transfers and fetches
   I/O program orders from main memory for the channel.

   Interrupt requests are serviced by the IOP, which asserts an external
   interrupt signal to the CPU.  Device controllers request interrupts via the
   IOP, which prioritizes the requests and grants service to the
   highest-priority interrupt.  While that interrupt is active, lower-priority
   requests are held off until it becomes inactive, whereupon the next
   highest-priority request is granted.  The device number of the interrupting
   device is stored in the IOP's address register; this is used by the CPU
   microcode to access the proper entry in the Device Reference Table, which
   contains the starting address of the I/O handler.

   In hardware, a device requests an interrupt by asserting INTREQ to the IOP.
   In response, the IOP polls the interfaces by asserting INTPOLLIN to determine
   the highest-priority request.  The INTPOLLIN and INTPOLLOUT signals are
   daisy-chained between interfaces, with the position of the interface in the
   chain establishing its priority.  Interfaces that are not requesting or
   processing interrupts pass INTPOLLIN to INTPOLLOUT.  The first interface in
   the chain that has an interrupt request pending will inhibit INTPOLLOUT and
   will set its Interrupt Active flip-flop.  As long as the interrupt is active,
   an interface will break the poll chain by denying INTPOLLOUT.  This holds off
   requests from lower-priority devices.

   To avoid scanning each interface's DIB for interrupt requests, the IOP
   simulator maintains two 32-bit vectors: a global "iop_interrupt_request_set"
   and a local "interrupt_poll_set".  Each bit is associated with an interrupt
   priority number from 0-31.  The bits of the request set indicate which
   interfaces are requesting interrupts, and the bits of the poll set indicate
   which interfaces will break the poll chain when they are polled.  The lowest
   set bit in each indicates the highest-priority interrupting device and the
   highest-priority device handler currently executing, respectively.  An
   interface requests an interrupt by asserting INTREQ to the IOP.  The IOP then
   sets the request and poll bits corresponding to that interface's interrupt
   priority number.  The CPU checks the request set periodically to determine if
   an external interrupt is present.

   A device's DIB (Device Information Block) contains three values that pertain
   to interrupts: the "interrupt_priority" value determines which bit is set or
   cleared in the bit vectors, the "interrupt_request" flip-flop indicates that
   the interface is requesting an interrupt from the IOP, and the
   "interrupt_active" flip-flop indicates that the device's interrupt handler is
   executing.  The two flip-flop values indicate one of four possible interrupt
   states that are reflected in the associated bit of the bit vectors:

     Interrupt  Interrupt  Request  Poll
      Request    Active      Set    Set   Interrupt State
     ---------  ---------  -------  ----  ------------------------------------
       CLEAR      CLEAR       0      0    Not interrupting
        SET       CLEAR       1      1    Interrupt requested
       CLEAR       SET        0      1    Interrupt acknowledged
        SET        SET        1      1    Interrupt requested while in handler

   The "requested" state corresponds to the device interface asserting the
   INTREQ signal to the IOP.  The "acknowledged" state corresponds to the IOP
   conducting a poll via INTPOLL IN and INTPOLL OUT, and the device interface
   responding by inhibiting INTPOLL OUT and asserting INTACK to the IOP.

   If both the request and active flip-flops are set, the device has requested a
   second interrupt while the first is still being processed.  The ATC TDI, for
   example, does this when a CIO is issued to acknowledge an interrupt before
   the IXIT sends an ioRIN to the interface to reset the active flip-flop.

   Device interfaces maintain the states of their interrupt flip-flops for the
   benefit of IOP initialization.  During the instruction execution prelude, the
   IOP will reconstruct its bit vectors from the DIB values.  Thereafter, the
   interfaces change their interrupt states in response to signals, and the IOP
   adjusts the bit vectors as needed.  The only direct interaction needed is an
   "iop_assert_INTREQ" call from the device interface when an interrupt is
   initially requested.

   The IOP does not have a programmable interface.  It is manipulated directly
   by the CPU microcode to issue direct I/O commands to the device interfaces,
   and by the multiplexer channel to transfer data and I/O programs to and from
   memory.

   Direct I/O instructions are sent via the IOP Bus to all device interfaces.
   When executing I/O instructions, the CPU microcode writes a 16-bit command
   word to the IOP, which then places bits 5-7 of that word onto the IOP Bus as
   IOCMD0-2 as follows:

         CPU      Command  IOCMD  Generated               Action
     Instruction   Word    0 1 2   Signal               Performed
     -----------  -------  -----  ---------  --------------------------------
         SIN      100000   0 0 0  DSETINT    Set interrupt request flip-flop
         CIO      100400   0 0 1  DCONTSTB   Write a control word
         SIO      101000   0 1 0  DSTARTIO   Start a channel program
         WIO      101400   0 1 1  DWRITESTB  Write a data word
        IXIT      102000   1 0 0  DRESETINT  Reset interrupt active flip-flop
         TIO      102400   1 0 1  DSTATSTB   Read a status word
        SMSK      103000   1 1 0  DSETMASK   Set the interrupt mask flip-flop
         RIO      103400   1 1 1  DREADSTB   Read a data word

   The SIO instruction sends an SIO IOCMD to the device interface via the IOP
   to begin execution of a channel program.  The program consists of two-word
   programmed I/O orders, with each pair consisting of an I/O Control Word and
   an I/O Address Word.  They are encoded as follows:

       IOCW        IOCW            IOAW        Generated           Action
     0 1 2 3       4-15            0-15         Signal           Performed
     -------  --------------  --------------  -----------  ---------------------
     0 0 0 0  0 XXXXXXXXXXX   Jump Address        --       Unconditional Jump
     0 0 0 0  1 XXXXXXXXXXX   Jump Address    SETJMP       Conditional Jump
     0 0 0 1  0 XXXXXXXXXXX   Residue Count       --       Return Residue
     0 0 0 1  1 XXXXXXXXXXX   Bank Address        --       Set Bank
     0 0 1 0  X XXXXXXXXXXX   --- XXXXXX ---  SETINT       Interrupt
     0 0 1 1  0 XXXXXXXXXXX   Status Value    TOGGLESIOOK  End without Interrupt
     0 0 1 1  1 XXXXXXXXXXX   Status Value    SETINT       End with Interrupt
     0 1 0 0  Control Word 1  Control Word 2  PCONTSTB     Control
     0 1 0 1  X XXXXXXXXXXX   Status Value    PSTATSTB     Sense
     C 1 1 0  Neg Word Count  Write Address   PWRITESTB    Write
     C 1 1 1  Neg Word Count  Read Address    PREADSTB     Read

   The "Unconditional Jump," "Return Residue," and "Set Bank" orders are
   executed entirely by the channel and are not sent to the device interface.

   The IOP simulator provides the capability to trace direct I/O commands and
   interrupt requests, as well as memory accesses made on behalf of the
   multiplexer channel.  Devices that periodically interrupt, such as the system
   clock, may generate a large number of trace events.  To accommodate this, a
   filter may be applied to remove trace events from devices that are not of
   interest.
*/


#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_io.h"



/* Program constants */

#define TRACE_ALL           D32_UMAX            /* enable tracing of all devices */


/* Debug flags.

   The FILTER macro tests if the supplied device number is to be filtered out of
   the trace stream.  It returns the bit in the filter array corresponding to
   the device number.  If the bit is set, the trace will be generated;
   otherwise, it will be suppressed.


   Implementation notes:

    1. Bit 0 is reserved for the memory data trace flag.
*/

#define DEB_DIO             (1u << 1)           /* trace direct I/O commands */
#define DEB_IRQ             (1u << 2)           /* trace interrupt requests */

#define FILTER(d)           (1u << (d) % 32 & filter [(d) / 32])


/* IOP global data structures */

const SIO_ORDER to_sio_order [] = {             /* translation of IOCW bits 0-4 to SIO_ORDER */
    sioJUMP,                                    /*   00000 = Jump unconditionally */
    sioJUMPC,                                   /*   00001 = Jump conditionally */
    sioRTRES,                                   /*   00010 = Return residue */
    sioSBANK,                                   /*   00011 = Set bank */
    sioINTRP,                                   /*   00100 = Interrupt */
    sioINTRP,                                   /*   00101 = Interrupt */
    sioEND,                                     /*   00110 = End */
    sioENDIN,                                   /*   00111 = End with interrupt */
    sioCNTL,                                    /*   01000 = Control */
    sioCNTL,                                    /*   01001 = Control */
    sioSENSE,                                   /*   01010 = Sense */
    sioSENSE,                                   /*   01011 = Sense */
    sioWRITE,                                   /*   01100 = Write */
    sioWRITE,                                   /*   01101 = Write */
    sioREAD,                                    /*   01110 = Read */
    sioREAD,                                    /*   01111 = Read */
    sioJUMP,                                    /*   10000 = Jump unconditionally */
    sioJUMPC,                                   /*   10001 = Jump conditionally */
    sioRTRES,                                   /*   10010 = Return residue */
    sioSBANK,                                   /*   10011 = Set bank */
    sioINTRP,                                   /*   10100 = Interrupt */
    sioINTRP,                                   /*   10101 = Interrupt */
    sioEND,                                     /*   10110 = End */
    sioENDIN,                                   /*   10111 = End with interrupt */
    sioCNTL,                                    /*   11000 = Control */
    sioCNTL,                                    /*   11001 = Control */
    sioSENSE,                                   /*   11010 = Sense */
    sioSENSE,                                   /*   11011 = Sense */
    sioWRITEC,                                  /*   11100 = Write Chained */
    sioWRITEC,                                  /*   11101 = Write Chained */
    sioREADC,                                   /*   11110 = Read Chained */
    sioREADC                                    /*   11111 = Read Chained */
    };


const char *const sio_order_name [] = {         /* indexed by SIO_ORDER */
    "Jump",                                     /*   sioJUMP   */
    "Conditional Jump",                         /*   sioJUMPC  */
    "Return Residue",                           /*   sioRTRES  */
    "Set Bank",                                 /*   sioSBANK  */
    "Interrupt",                                /*   sioINTRP  */
    "End",                                      /*   sioEND    */
    "End with Interrupt",                       /*   sioENDIN  */
    "Control",                                  /*   sioCNTL   */
    "Sense",                                    /*   sioSENSE  */
    "Write",                                    /*   sioWRITE  */
    "Write Chained",                            /*   sioWRITEC */
    "Read",                                     /*   sioREAD   */
    "Read Chained"                              /*   sioREADC  */
    };


/* Global IOP state */

uint32 iop_interrupt_request_set = 0;           /* the set of interfaces requesting interrupts */


/* Local IOP state */

static uint32 IOA = 0;                          /* I/O Address Register */

static uint32 interrupt_poll_set = 0;           /* the set of interfaces breaking the poll chain */
static DIB    *devs [DEVNO_MAX + 1];            /* index by device number for I/O instruction dispatch */
static DIB    *irqs [INTPRI_MAX + 1];           /* index by interrupt priority number for interrupt requests */

static uint32 filter [4] = {                    /* filter bitmap for device numbers 0-127 */
    TRACE_ALL,
    TRACE_ALL,
    TRACE_ALL,
    TRACE_ALL
    };


/* IOP local SCP support routines */

static t_stat iop_reset       (DEVICE *dptr);
static t_stat iop_set_filter  (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat iop_show_filter (FILE *st,   UNIT  *uptr, int32 value, CONST void *desc);


/* IOP SCP data structures */


/* Unit list */

static UNIT iop_unit [] = {                     /* a dummy unit to satisfy SCP requirements */
    { UDATA (NULL, 0, 0) }
    };

/* Register list.


   Implementation notes:

    1. The "interrupt_poll_set", "devs", and "irqs" variables need not be SAVEd
       or RESTOREd, as they are rebuilt during the instruction execution
       prelude.
*/

static REG iop_reg [] = {
/*    Macro   Name    Location  Radix  Width  Depth   Flags  */
/*    ------  ------  --------  -----  -----  -----  ------- */
    { ORDATA (IOA,    IOA,               8),         REG_RO  },
    { BRDATA (FILTER, filter,     2,    32,     4),  REG_HRO },
    { NULL }
    };

/* Modifier list */

static MTAB iop_mod [] = {
/*    Entry Flags          Value  Print String  Match String  Validation       Display           Descriptor */
/*    -------------------  -----  ------------  ------------  ---------------  ----------------  ---------- */
    { MTAB_XDV | MTAB_NMO,   1,   "FILTER",     "FILTER",     &iop_set_filter, &iop_show_filter, NULL       },
    { MTAB_XDV | MTAB_NMO,   0,   "",           "NOFILTER",   &iop_set_filter, NULL,             NULL       },
    { 0 }
    };

/* Debugging trace list */

static DEBTAB iop_deb [] = {
    { "DIO",  DEB_DIO   },                      /* direct I/O commands issued */
    { "IRQ",  DEB_IRQ   },                      /* interrupt requests received */
    { "DATA", DEB_MDATA },                      /* I/O data accesses to memory */
    { NULL,   0         }
    };

/* Device descriptor */

DEVICE iop_dev = {
    "IOP",                                      /* device name */
    iop_unit,                                   /* unit array */
    iop_reg,                                    /* register array */
    iop_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    DV_WIDTH,                                   /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &iop_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    NULL,                                       /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    iop_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* IOP global routines */



/* Initialize the I/O processor.

   This routine is called in the instruction prelude to set up the IOP data
   structures prior to beginning execution.  It sets up two tables of DIB
   pointers -- one indexed by device number, and a second indexed by interrupt
   request number.  This allows fast access to the device interface routine by
   the direct I/O instruction and interrupt poll processors, respectively.

   It also sets the interrupt request and poll bit vectors from the interrupt
   flip-flop values in the device DIBs and clears the external interrupt flag if
   there are no devices with active interrupts (as the user may have set the
   flag or reset the interrupting device during a simulation stop).

   The value of the IOA register is returned.  This is zero unless a device
   requesting an interrupt has been acknowledged but not yet serviced, in which
   case the value is the device number.
*/

uint32 iop_initialize (void)
{
const DEVICE *dptr;
DIB    *dibptr;
uint32 i, irq;

iop_interrupt_request_set = 0;                          /* set all interrupt requests inactive */
interrupt_poll_set        = 0;                          /* set all poll continuity bits inactive */

memset (devs, 0, sizeof devs);                          /* clear the device number table */
memset (irqs, 0, sizeof irqs);                          /*   and the interrupt request table */

for (i = 0; sim_devices [i] != NULL; i++) {             /* loop through all of the devices */
    dptr = sim_devices [i];                             /* get a pointer to the device */
    dibptr = (DIB *) dptr->ctxt;                        /*   and to that device's DIB */

    if (dibptr && !(dptr->flags & DEV_DIS)) {           /* if the DIB exists and the device is enabled */
        if (dibptr->device_number != DEVNO_UNUSED)      /*   then if the device number is valid */
            devs [dibptr->device_number] = dibptr;      /*     then set the DIB pointer into the device dispatch table */

        if (dibptr->interrupt_priority != INTPRI_UNUSED) {  /* if the interrupt priority is valid */
            irqs [dibptr->interrupt_priority] = dibptr;     /*   then set the pointer into the interrupt dispatch table */

            irq = 1 << dibptr->interrupt_priority;      /* get the associated interrupt request bit */

            if (dibptr->interrupt_request) {            /* if the interface is requesting an interrupt */
                iop_interrupt_request_set |= irq;       /*   then set the request bit */
                interrupt_poll_set        |= irq;       /*     and the poll bit */
                }

            else if (dibptr->interrupt_active)          /* otherwise if the interface has acknowledged an interrupt */
                interrupt_poll_set |= irq;              /*   then just set the poll bit */
            }
        }
    }

if (interrupt_poll_set == 0 || IOA == 0)                /* if no device has an active interrupt in progress */
    CPX1 &= ~cpx1_EXTINTR;                              /*   then clear the interrupt flag */

return IOA;
}


/* Poll the interfaces for an active interrupt request.

   This routine is called in the instruction loop when the request set indicates
   that one or more interrupt requests are pending.  It polls the interface
   asserting the highest-priority request.  If the interface acknowledges the
   interrupt, the routine sets the "external interrupt" bit in the CPU's CPX1
   register to initiate interrupt processing, sets the IOP's IOA register to the
   the device number of the interrupting device, and returns that value to the
   caller.

   In hardware, an interface requesting an interrupt with its Interrupt Mask
   flip-flop set will assert a common INTREQ to the IOP.  In response, the IOP
   polls the interfaces to determine the highest-priority request by asserting
   INTPOLLIN.  The INTPOLLIN and INTPOLLOUT signals are daisy-chained between
   interfaces, with the position of the interface in the chain establishing its
   priority.  Interfaces that are not requesting interrupts pass INTPOLLIN to
   INTPOLLOUT.  The first interface in the chain that has its Interrupt Request
   flip-flop set will inhibit INTPOLLOUT, set its Interrupt Active flip-flop,
   and assert INTACK to the IOP.

   To avoid polling interfaces in simulation, an interface will set the
   Interrupt Request flip-flop in its DIB and then call iop_assert_INTREQ.  That
   routine sets the request set and poll set bits corresponding to the interrupt
   priority value in the DIB.

   In the instruction execution loop, if external interrupts are enabled (i.e.,
   the I bit in the status word is set), and iop_interrupt_request_set has one
   or more bits set, this routine is called to select the interrupt request to
   service.

   The end of priority chain is marked by the highest-priority (lowest-order)
   poll bit that is set.  When a poll is performed, a priority mask is generated
   that contains just the highest-priority bit.  The device corresponding to
   that bit will then be the recipient of the current interrupt acknowledgement
   cycle.  After the interrupt request has been cleared, the poll bit will
   prevent lower-priority interrupts from being serviced.

   For example:

     poll set      : ...0 0 1 0 0 1 0 0 0 0 0 0  (poll denied at INTPRI 6 and 9)
     priority mask : ...0 0 0 0 0 1 0 0 0 0 0 0  (poll stops at INTPRI 6)

   The request is then ANDed with the priority mask to determine if a request is
   to be granted:

     pri mask      : ...0 0 0 0 0 1 0 0 0 0 0 0  (allowed interrupt source)
     request set   : ...0 0 1 0 0 1 0 0 0 0 0 0  (devices requesting interrupts)
     ANDed value   : ...0 0 0 0 0 1 0 0 0 0 0 0  (request to grant = INTPRI 6)

   Once the interrupt request has been cleared, the poll state is:

     poll set      : ...0 0 1 0 0 1 0 0 0 0 0 0  (poll denied at INTPRI 6 and 9)
     priority mask : ...0 0 0 0 0 1 0 0 0 0 0 0  (poll stops at INTPRI 6)
     request set   : ...0 0 1 0 0 0 0 0 0 0 0 0  (devices requesting interrupts)
     ANDed value   : ...0 0 0 0 0 0 0 0 0 0 0 0  (request to grant = none)

   INTPRI 9 will continue to be held off until INTPRI 6 completes its interrupt
   handler and resets the Interrupt Active flip-flop on its interface, which
   also clears the associated poll set bit.  At the next poll, INTPRI 9 will be
   granted.

   This routine determines the request to grant, converts that back to an
   interrupt priority number, and uses that to index into the table of DIBs.
   The interface routine associated with the DIB is called with INTPOLLIN
   asserted.

   If the interface still has its Interrupt Request flip-flop set, it
   will assert INTACK and return the device number.  In response, the IOP will
   save the device number in IOA, set the external interrupt bit in CPX1, and
   return the device number to the CPU.  This will cause the CPU to service the
   interrupt.

   However, if some condition has occurred between the time of the original
   request and this poll, the interface will assert INTPOLLOUT.  In response,
   the IOP will clear IOA and the associated bit in the poll set to cancel the
   request.

   In either case, the associated bit in the request set is cleared.


   Implementation notes:

    1. The hardware inhibits the interrupt poll if the EXTINT flip-flop is set.
       This prevents a second interrupt from changing IOA until the microcode
       signals its readiness by clearing EXTINT.  In simulation, entry with
       cpx1_EXTINTR set returns IOA in lieu of conducting a poll.
*/

uint32 iop_poll (void)
{
DIB         *dibptr;
SIGNALS_DATA outbound;
uint32       ipn, priority_mask, request_granted;

if (CPX1 & cpx1_EXTINTR)                                /* if an external interrupt has been requested */
    return IOA;                                         /*   then return the device number in lieu of polling */

priority_mask   = IOPRIORITY (interrupt_poll_set);              /* calculate the priority mask */
request_granted = priority_mask & iop_interrupt_request_set;    /*   and determine the request to grant */

if (request_granted == 0)                               /* if no request has been granted */
    return 0;                                           /*   then return */

for (ipn = 0; !(request_granted & 1); ipn++)            /* determine the interrupt priority number */
    request_granted = request_granted >> 1;             /*   by counting the bits until the set bit is reached */

dibptr = irqs [ipn];                                    /* get the DIB pointer for the request */

outbound = dibptr->io_interface (dibptr, INTPOLLIN, 0); /* poll the interface that requested the interrupt */

if (outbound & INTACK) {                                /* if the interface acknowledged the interrupt */
    IOA = IODATA (outbound);                            /*   then save the returned device number */
    CPX1 |= cpx1_EXTINTR;                               /*     and tell the CPU */

    dprintf (iop_dev, FILTER (dibptr->device_number) ? DEB_IRQ : 0,
             "Device number %u acknowledged interrupt request at priority %u\n",
             dibptr->device_number, ipn);
    }

else if (outbound & INTPOLLOUT) {                       /* otherwise if the interface cancelled the request */
    IOA = 0;                                            /*   then clear the device number */
    interrupt_poll_set &= ~priority_mask;               /*     and the associated bit in the poll set */

    dprintf (iop_dev, FILTER (dibptr->device_number) ? DEB_IRQ : 0,
             "Device number %u canceled interrupt request at priority %u\n",
             dibptr->device_number, ipn);
    }

iop_interrupt_request_set &= ~priority_mask;            /* clear the request */

return IOA;                                             /* return the interrupting device number */
}


/* Dispatch an I/O command to an interface.

   This routine is called by the CPU when executing direct I/O instructions
   to send I/O orders to the indicated device interface.  It translates the
   "io_cmd" value to the appropriate I/O signal and calls the signal handler of
   the device interface indicated by the "device_number" with the supplied
   "write_value".  The handler return value, if any, is returned as the function
   value.  If the supplied device number does not correspond to an enabled
   device, the I/O Timeout bit in CPX1 is set.

   A "Set Interrupt Mask" order is sent to all active interfaces; the supplied
   device number is ignored.  If there are none, the I/O Timeout bit is set.
   All of the other orders are sent only to the specified device.  A "Reset
   Interrupt" order clears the corresponding bit from the poll set, unless there
   is a request pending on the device (which may occur if a second interrupt was
   requested while the first was still being processed).


   Implementation notes:

    1. For a "Set Interrupt Mask" order, it would be faster to cycle through the
       sim_devices array to find the active devices.  However, we use the devs
       array so that interfaces are accessed in DEVNO order, which makes traces
       easier to follow.  This is an acceptable tradeoff, as the SMSK
       instruction is used infrequently.
*/

HP_WORD iop_direct_io (HP_WORD device_number, IO_COMMAND io_cmd, HP_WORD write_value)
{
static const INBOUND_SIGNAL cmd_to_signal [] = {        /* indexed by IO_COMMAND */
    DSETINT,                                            /*   ioSIN  = set interrupt */
    DCONTSTB,                                           /*   ioCIO  = control I/O */
    DSTARTIO,                                           /*   ioSIO  = start I/O */
    DWRITESTB,                                          /*   ioWIO  = write I/O */
    DRESETINT,                                          /*   ioRIN  = reset interrupt */
    DSTATSTB,                                           /*   ioTIO  = test I/O */
    DSETMASK,                                           /*   ioSMSK = set interrupt mask */
    DREADSTB                                            /*   ioRIO  = read I/O */
    };

static const char *const io_command_name [] = {         /* indexed by IO_COMMAND */
    "Set Interrupt",                                    /*   ioSIN  */
    "Control I/O",                                      /*   ioCIO  */
    "Start I/O",                                        /*   ioSIO  */
    "Write I/O",                                        /*   ioWIO  */
    "Reset Interrupt",                                  /*   ioRIN  */
    "Test I/O",                                         /*   ioTIO  */
    "Set Interrupt Mask",                               /*   ioSMSK */
    "Read I/O"                                          /*   ioRIO  */
    };

uint32       irq, devno;
t_bool       no_response;
DIB          *dibptr;
SIGNALS_DATA outbound = NO_SIGNALS;

if (io_cmd == ioSMSK) {                                     /* if the I/O order is "Set Interrupt Mask" */
    no_response = TRUE;                                     /*   then check for responding devices */

    for (devno = 0; devno <= DEVNO_MAX; devno++) {          /* loop through the device number list */
        dibptr = devs [devno];                              /*   to get a device information block pointer */

        if (dibptr                                          /* if this device is defined */
          && dibptr->interrupt_mask != INTMASK_UNUSED) {    /*   and uses the interrupt mask */

            dprintf (iop_dev, FILTER (devno) ? DEB_DIO : 0,
                     "%s order sent to device number %u\n",
                     io_command_name [io_cmd], devno);

            outbound =
               dibptr->io_interface (dibptr, DSETMASK,      /* send the SET MASK signal to the device */
                                     write_value);          /*   and supply the new mask value */

            if (outbound & INTREQ)                          /* if an interrupt request was asserted */
                iop_assert_INTREQ (dibptr);                 /*   then set it up */

            no_response = FALSE;                            /* at least one device has responded */
            }
        }

    if (no_response)                                        /* if no devices responded */
        CPX1 |= cpx1_IOTIMER;                               /*   then indicate an I/O timeout */
    }

else {                                                      /* otherwise a device-specific order is present */
    device_number = device_number & DEVNO_MASK;             /* restrict the device number to 0-127 */

    dprintf (iop_dev, FILTER (device_number) ? DEB_DIO : 0,
             "%s order sent to device number %u\n",
             io_command_name [io_cmd], device_number);

    dibptr = devs [device_number];                          /* get the device information block pointer */

    if (dibptr == NULL)                                     /* if the device not present */
        CPX1 |= cpx1_IOTIMER;                               /*   then indicate an I/O timeout on access */

    else {                                                  /* otherwise call the device interface */
        outbound =                                          /*   with the indicated signal and write value */
           dibptr->io_interface (dibptr, cmd_to_signal [io_cmd],
                                 write_value);

        if (outbound & INTREQ)                              /* if an interrupt request was asserted */
            iop_assert_INTREQ (dibptr);                     /*   then set it up */

        if (outbound & SRn)                                 /* if a service request was asserted */
            mpx_assert_SRn (dibptr);                        /*   then set it up */

        if (io_cmd == ioRIN                                 /* if this a "Reset Interrupt" order */
          && dibptr->interrupt_priority != INTPRI_UNUSED) { /*   and the interrupt priority is valid */
            irq = 1 << dibptr->interrupt_priority;          /*     then calculate the device bit */

            if ((iop_interrupt_request_set & irq) == 0)     /* if no request is pending for this device */
                interrupt_poll_set &= ~irq;                 /*   then clear the associated poll bit */
            }
        }
    }

return IODATA (outbound);                                   /* return the outbound data value */
}


/* Request an interrupt.

   This routine is called by device interfaces to request an external interrupt.
   It corresponds in hardware to asserting the INTREQ signal.  The routine sets
   the request and poll set bits corresponding to the interrupt priority number.
*/

void iop_assert_INTREQ (DIB *dibptr)
{
uint32 irq;

dprintf (iop_dev, FILTER (dibptr->device_number) ? DEB_IRQ : 0,
         "Device number %u asserted INTREQ at priority %u\n",
         dibptr->device_number, dibptr->interrupt_priority);

if (dibptr->interrupt_priority != INTPRI_UNUSED) {      /* if the interrupt priority is valid */
    irq = 1 << dibptr->interrupt_priority;              /*   then calculate the corresponding priority bit */

    iop_interrupt_request_set |= irq;                   /* set the request */
    interrupt_poll_set        |= irq;                   /*   and the poll bits */
    }

return;
}


/* Warn devices of an impending power failure.

   This routine is called by the POWER FAIL command to send a warning
   to all devices that power is about to fail.  It corresponds in hardware to
   asserting the PFWARN signal.  Devices may process or ignore the signal as
   appropriate.  If the device returns the INTREQ signal, an interrupt is
   requested.
*/

void iop_assert_PFWARN (void)
{
uint32       devno;
DIB          *dibptr;
SIGNALS_DATA outbound;

for (devno = 0; devno <= DEVNO_MAX; devno++) {          /* loop through the device number list */
    dibptr = devs [devno];                              /*   and get the next device information block pointer */

    if (dibptr != NULL) {                               /* if this device is defined */
        outbound =                                      /*   then send the PFWARN signal to the device interface */
          dibptr->io_interface (dibptr, PFWARN, 0);

        if (outbound & INTREQ)                          /* if the device requested an interrupt */
            iop_assert_INTREQ (dibptr);                 /*   then set it up */
        }
    }

return;
}



/* IOP local SCP support routines */



/* Device reset routine.

   This routine is called for a RESET or RESET IOP command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.


   Implementation notes:

    1. In hardware, IORESET clears flip-flops associated with the state machines
       that implement the interrupt poll, SO/SI handshake, and multiplexer
       channel access.  In simulation, these are all represented by function
       calls and, as such, are atomic.  Therefore, the only state variable that
       IORESET clears is the external interrupt flip-flop, which is implemented
       as its respective bit in the CPX1 register rather than as a separate
       variable.  Setting IOA to 0 and calling iop_initialize clears this bit;
       it also sets up the devs array, which is used by the POWER FAIL command.

    2. In hardware, IORESET also clears the IOP address parity error, system
       parity error, and illegal address flip-flops.  However, these exist only
       to assert XFERERROR to devices.  In simulation, XFERERROR is sent to a
       device interface when the initiating condition is detected by the
       multiplexer channel, so these are not represented by state variables.
*/

static t_stat iop_reset (DEVICE *dptr)
{
IOA = 0;                                                /* clear the I/O Address register and initialize */
iop_initialize ();                                      /*   which clears the external interrupt flip-flop */

return SCPE_OK;
}


/* Set the trace omission filter.

   If the "value" parameter is 1, the filter array bits corresponding to the
   device number(s) in the buffer referenced by the "cptr" parameter are set to
   exclude those devices from the trace listing.  If the "value" parameter is 0,
   the filter array is reset to include all devices.  The unit and descriptor
   pointer parameters are not used.

   Each bit of the four, 32-bit filter array elements corresponds to a device
   number from 0-127, with the LSB of the first element representing device 0,
   and the MSB of the last element representing device 127.  A set bit enables
   tracing of that device.  The filter starts out with all bits set, implying
   that all devices are traced.  Specifying device numbers to filter out clears
   the corresponding bits.

   Example filter commands:

      SET IOP FILTER=3         --  omit tracing for device 3.
      SET IOP FILTER=4;7-9;11  --  omit tracing for devices 4, 7, 8, 9, and 11.
      SET IOP FILTER=ALL       --  omit tracing for all devices
      SET IOP NOFILTER         --  restore tracing for all devices

   On entry, the "cptr" parameter points to the first character of the range
   specification, which may be either a semicolon-separated list of device
   number ranges or the keyword ALL.  Each range is parsed and added to the new
   filter array.  Once the entire array has been set, it is copied over the old
   filter.  If an error occurs during parsing, the original filter set is not
   disturbed.
*/

static t_stat iop_set_filter (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
CONST char *tptr;
char       *mptr;
t_addr     dev, low, high;
t_stat     result = SCPE_OK;
uint32     new_filter [4] = { TRACE_ALL, TRACE_ALL, TRACE_ALL, TRACE_ALL };

if (value == 1) {                                       /* if we are setting the filter */
    if ((cptr == NULL) || (*cptr == '\0'))              /*   then if a line range was not supplied */
        return SCPE_MISVAL;                             /*     then report a "Missing value" error */

    mptr = (char *) malloc (strlen (cptr) + 2);         /* allocate space for the string, a semicolon, and a NUL */

    if (mptr == NULL)                                   /* if the allocation failed */
        return SCPE_MEM;                                /*   report memory exhaustion */

    strcpy (mptr, cptr);                                /* copy over the existing command string */
    tptr = strcat (mptr, ";");                          /*   and append a semicolon to make parsing easier */

    while (*tptr) {                                     /* parse the command string until it is exhausted */
        tptr = get_range (NULL, tptr, &low, &high,      /* get a semicolon-separated device number range */
                          10, 127, ';');                /*   in radix 10 with a maximum value of 127 */

        if (tptr == NULL || low > 127 || high > 127) {  /* if a parsing error occurred or a number was out of range */
            result = SCPE_ARG;                          /*   then report an "Invalid argument" error */
            break;                                      /*     and quit at this point */
            }

        else for (dev = low; dev <= high; dev++)        /* otherwise loop through the range of device numbers */
            new_filter [dev / 32] &= ~(1 << dev % 32);  /*   and clear each corresponding bit in the array */
        }

    free (mptr);                                        /* deallocate the temporary string */
    }

else if (cptr != NULL)                                  /* otherwise we are clearing the filter */
    return SCPE_2MARG;                                  /*   and no arguments are allowed or needed */

if (result == SCPE_OK) {                                /* if the filter assignment was successful */
    filter [0] = new_filter [0];                        /*   then copy */
    filter [1] = new_filter [1];                        /*     the new filter set */
    filter [2] = new_filter [2];                        /*       in place of */
    filter [3] = new_filter [3];                        /*         the current filter set */
    }

return result;                                          /* return the result of the command */
}


/* Show the omission filter.

   The device numbers in the filter array are printed as a semicolon-separated
   list on the stream designated by the "st" parameter.  The "uptr", "value",
   and "desc" parameters are not used.

   Ranges are printed where possible to shorten the output.  This is
   accomplished by tracking the starting and ending device numbers of a range of
   bits in the filter and then printing that range when a device number bit not
   in the filter is encountered.
*/

static t_stat iop_show_filter (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
int32  group, low, high;
uint32 test_filter;
t_bool first = TRUE, in_range = FALSE;

low = 0;                                                /* initialize the current starting value */

for (group = 0; group < 4; group++) {                   /* the filter values are stored in four elements */
    test_filter = filter [group];                       /* get the set of devices from current element */

    for (high = group * 32; high < group * 32 + 32; high++) {   /* loop through the represented device numbers */
        if ((test_filter & 1) == 0) {                           /* if the current device is filtered out */
            in_range = TRUE;                                    /*   then accumulate the omission range */
            }

        else if (in_range) {                            /* otherwise if an omission range was accumulated */
            if (first) {                                /*   then if this is the first range to be printed */
                fputs ("filter=", st);                  /*     then print a header to start */
                first = FALSE;
                }

            else                                        /* otherwise this is not the first range to be printed */
                fputc (';', st);                        /*   so print a separator after the prior range */

            if (low == high - 1)                        /* if the range is empty */
                fprintf (st, "%d", low);                /*   then print the single device number */

            else                                        /* otherwise a range was established */
                fprintf (st, "%d-%d", low, high - 1);   /*   so print the starting and ending device numbers */

            in_range = FALSE;                           /* start a new range */
            low = high + 1;                             /*   from this device number onward */
            }

        else                                            /* otherwise we are between ranges */
            low = low + 1;                              /*   so increment the current starting value */

        test_filter = test_filter >> 1;                 /* shift the next filter bit into place for testing */
        }
    }

if (first == TRUE)                                      /* if there is only a single range */
    if (in_range)                                       /*   then if it's an omission range */
        fprintf (st, "filter=%d-127\n", low);           /*     then report it */

    else                                                /* otherwise it's an inclusion range */
        fputs ("no filter\n", st);                      /*   so report that no devices are filtered out */

else                                                    /* otherwise one or more ranges has been printed */
    fputc ('\n', st);                                   /*   so add a line terminator */

return SCPE_OK;
}
