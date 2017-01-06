/* hp3000_cpu_ims.h: HP 3000 CPU-to-IOP/MPX/SEL interface declarations

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

   01-Sep-16    JDB     Added the cpu_cold_cmd and cpu_power_cmd routines
   15-Aug-16    JDB     Removed obsolete comment mentioning iop_read/write_memory
   15-Jul-16    JDB     Corrected the IOCW_COUNT macro to return the correct value
   11-Jun-16    JDB     Bit mask constants are now unsigned
   05-Sep-15    JDB     First release version
   11-Dec-12    JDB     Created


   This file contains declarations used by the CPU to interface with the HP 3000
   I/O Processor, Multiplexer Channel, and Selector Channel.
*/



/* Global data structures */


/* I/O commands.

   The enumeration values correspond to the IOP bus IOCMD0-2 signal
   representations.
*/

typedef enum {
    ioSIN  = 0,                                 /* set interrupt */
    ioCIO  = 1,                                 /* control I/O */
    ioSIO  = 2,                                 /* start I/O */
    ioWIO  = 3,                                 /* write I/O */
    ioRIN  = 4,                                 /* reset interrupt */
    ioTIO  = 5,                                 /* test I/O */
    ioSMSK = 6,                                 /* set interrupt mask */
    ioRIO  = 7                                  /* read I/O */
    } IO_COMMAND;


/* SIO program orders.

   32-bit I/O program words are formed from a 16-bit I/O control word (IOCW) and
   a 16-bit I/O address word (IOAW).  The Interrupt, Control, Sense, Write, and
   Read orders use this format:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | C |   order   |           control word 1/word count           |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 control word 2/status/address                 |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   For the Write and Read orders only, bit 0 of the IOCW is the "data chain"
   flag.  If it is set, then this transfer is a continuation of the previous
   Write or Read transfer.

   The Jump, End, Return Residue, and Set Bank orders require an additional bit
   (IOCW bit 4) to define their orders fully:

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - |     order     | -   -   -   -   -   -   -   -   -   -   - |  IOCW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     address/status/count                      |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   - |     bank      |  IOAW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   In simulation, IOCW bits 0-4 are used to index into a 32-element lookup table
   to produce the final I/O order (because some of the orders define IOCW bit 4
   as "don't care", there are only thirteen distinct orders).


   Implementation notes:

    1. The IOCW_COUNT(w) macro sign-extends the 12-bit two's-complement word
       count to a 16-bit value for the Return Residue order.

    2. The sioWRITE, sioWRITEC, sioREAD, and sioREADC enumeration constants must
       be contiguous and the final four values, so that a ">= sioWRITE" test
       identifies all four cases.
*/

#define IOCW_DC             0100000u            /* data chain */
#define IOCW_SIO_MASK       0070000u            /* general SIO order mask */
#define IOCW_ORDER_MASK     0174000u            /* fully decoded I/O order mask */
#define IOCW_CNTL_MASK      0007777u            /* control word mask */
#define IOCW_WCNT_MASK      0007777u            /* word count mask */

#define IOAW_BANK_MASK      0000017u            /* bank number mask */

#define IOCW_ORDER_SHIFT    11                  /* I/O order alignment shift */
#define IOCW_CNTL_SHIFT     0                   /* control word alignment shift */
#define IOCW_WCNT_SHIFT     0                   /* word count alignment shift */

#define IOAW_BANK_SHIFT     0                   /* bank number alignment shift */

#define IOCW_ORDER(w)       to_sio_order [((w) & IOCW_ORDER_MASK) >> IOCW_ORDER_SHIFT]

#define IOCW_CNTL(w)        (((w) & IOCW_CNTL_MASK) >> IOCW_CNTL_SHIFT)
#define IOCW_WCNT(w)        (((w) & IOCW_WCNT_MASK) >> IOCW_WCNT_SHIFT)
#define IOCW_COUNT(w)       ((w) | ~IOCW_WCNT_MASK & D16_MASK)

#define IOAW_BANK(w)        (((w) & IOAW_BANK_MASK) >> IOAW_BANK_SHIFT)


typedef enum {
    sioJUMP,                                    /* Jump unconditionally */
    sioJUMPC,                                   /* Jump conditionally */
    sioRTRES,                                   /* Return residue */
    sioSBANK,                                   /* Set bank */
    sioINTRP,                                   /* Interrupt */
    sioEND,                                     /* End */
    sioENDIN,                                   /* End with interrupt */
    sioCNTL,                                    /* Control */
    sioSENSE,                                   /* Sense */
    sioWRITE,                                   /* Write */
    sioWRITEC,                                  /* Write chained */
    sioREAD,                                    /* Read */
    sioREADC                                    /* Read chained */
    } SIO_ORDER;


/* Global CPU routine declarations.

   cpu_cold_cmd     : process the LOAD and DUMP commands
   cpu_power_cmd    : process the POWER commands
   cpu_read_memory  : read a word from main memory
   cpu_write_memory : write a word to main memory
*/

extern t_stat cpu_cold_cmd  (int32 arg, CONST char *buf);
extern t_stat cpu_power_cmd (int32 arg, CONST char *buf);

extern t_bool cpu_read_memory  (ACCESS_CLASS classification, uint32 offset, HP_WORD *value);
extern t_bool cpu_write_memory (ACCESS_CLASS classification, uint32 offset, HP_WORD  value);


/* Global SIO order structures.

   to_sio_order   : translates IOCW bits 1-4 to an SIO_ORDER
   sio_order_name : the name of the orders indexed by SIO_ORDER
*/

extern const SIO_ORDER   to_sio_order   [];
extern const char *const sio_order_name [];


/* Global I/O processor state and functions.

   iop_interrupt_request_set : the set of devices requesting interrupts

   iop_initialize   : initialize the I/O processor
   iop_poll         : poll the interfaces for an active interrupt request
   iop_direct_io    : dispatch an I/O command to an interface
*/

extern uint32 iop_interrupt_request_set;

extern uint32  iop_initialize   (void);
extern uint32  iop_poll         (void);
extern HP_WORD iop_direct_io    (HP_WORD device_number, IO_COMMAND io_cmd, HP_WORD write_value);


/* Global multiplexer channel state and functions.

   mpx_request_set : the set of pending channel service requests

   mpx_initialize : initialize the multiplexer channel
   mpx_service    : poll the interfaces for an active service request
*/

extern uint32 mpx_request_set;

extern void mpx_initialize (void);
extern void mpx_service    (uint32 ticks_elapsed);


/* Global selector channel state and functions

   sel_request : TRUE if a pending channel service request

   sel_initialize : initialize the selector channel
   sel_service    : service the interface with an active service request
*/

extern t_bool sel_request;

extern void sel_initialize (void);
extern void sel_service    (uint32 ticks_elapsed);
