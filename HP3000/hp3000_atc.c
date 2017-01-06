/* hp3000_atc.c: HP 3000 30032B Asynchronous Terminal Controller simulator

   Copyright (c) 2014-2016, J. David Bryan
   Copyright (c) 2002-2012, Robert M Supnik

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

   ATCD,ATCC    HP 30032B Asynchronous Terminal Controller

   16-Sep-16    JDB     Fixed atcd_detach to skip channel cancel if SIM_SW_REST
   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   20-Jul-16    JDB     Corrected poll_unit "wait" field initializer.
   26-Jun-16    JDB     Removed tmxr_set_modem_control_passthru call in atcc_reset
   09-Jun-16    JDB     Added casts for ptrdiff_t to int32 values
   16-May-16    JDB     Fixed interrupt mask setting
   13-May-16    JDB     Modified for revised SCP API function parameter types
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   26-Aug-15    JDB     First release version
   31-Jul-15    JDB     Passes the terminal control diagnostic (D438A)
   11-Aug-14    JDB     Passes the terminal data diagnostic (D427A)
   28-Jul-14    JDB     Created from the HP 2100 12920A MUX device simulator

   References:
     - 30032B Asynchronous Terminal Controller Installation and Service Manual
         (30032-90004, February 1977)
     - Stand-Alone HP 30032B Terminal Data Interface Diagnostic
         (30032-90011, October 1980)
     - Stand-Alone HP 30061A Terminal Controller Interface Diagnostic
         (30060-90004, February 1976)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 30032B Asynchronous Terminal Controller is a 16-channel terminal
   multiplexer used with the HP 3000 CX through Series III systems.  The ATC
   connects from 1 to 16 serial terminals or modems to the HP 3000 at
   programmable baud rates from 75 to 2400 bits per second.  Character sizes are
   also programmable from 5 to 12 bits in length, including the start and stop
   bits.  Each channel can be independently configured, including for separate
   send and receive rates.  The ATC is not buffered, so the CPU has to retrieve
   each character from a given channel before the next character arrives.  To
   avoid saturating the CPU with interrupt requests, the ATC maintains an
   internal "mini-interrupt" system that queues requests and holds additional
   interrupts off until the CPU acknowledges the current request.

   The HP 3000CX and Series I use a dedicated serial interface for the system
   console, while user terminals are connected to the ATC.  For the Series II
   and III, the separate card is eliminated, and channel 0 of the ATC is
   reserved for the console.

   This module is an adaptation of the code originally written by Bob Supnik for
   the HP2100 MUX simulator.  The MUX device simulates an HP 12920A interface
   for an HP 2100/1000 computer.  The 12920A is an ATC with the HP 3000 I/O bus
   connection replaced by an HP 2100 I/O bus connection.  Programming and
   operation of the two multiplexers are virtually identical.

   The ATC consists of a Terminal Data Interface, which provides direct
   connection for 16 serial terminals, and one or two optional Terminal Control
   Interfaces, which provides control and status lines for Bell 103 and 202 data
   sets, respectively.  The ATC base product, order number 30032, consisted of
   one TDI card.  Option -001 added one TCI, and option -002 added two.  A
   second ATC subsystem could be added to support an additional 16 terminals or
   modems.

   This simulation provides one TDI and one optional TCI.  Each of the channels
   may be connected either to a Telnet session or a serial port on the host
   machine.  Channel 0 is connected to the simulation console, which initially
   performs I/O to the controlling window but may be rerouted instead to a
   Telnet session or serial port, if desired.  Additional channel configuration
   options select the input mode (upshifted or normal), output mode (8-bit,
   7-bit, printable, or upshifted), and whether the HP-standard ENQ/ACK
   handshaking is done by the external device or internally by the simulator.

   A device mode specifies whether terminals or diagnostic loopback cables are
   connected to the TDI and TCI.  Enabling the diagnostic mode simulates the
   installation of eight HP 30062-60003 diagnostic test (loopback) cables
   between channels 0-1, 2-3, etc., as required by the multiplexer diagnostics.
   In this mode, sending data on one channel automatically receives the same
   data on the alternate channel.  In addition, all Telnet and serial sessions
   are disconnected, and the TDI is detached from the listening port.  While in
   diagnostic mode, the ATTACH command is not allowed.  Enabling terminal mode
   allows the TDI to be attached to accept incoming connections again.

   Another device mode specifies whether the TDI operates in real-time or
   optimized ("fast") timing mode.  In the former, character send and receive
   events occur at approximately the same rate (in machine instructions) as in
   hardware.  The latter mode increases the rate to the maximum value consistent
   with correct operation in MPE.

   Both the TDI and TCI are normally enabled, although the TCI will not be used
   unless MPE is configured to use data sets on one or more channels.  When so
   configured, logging off will cause the channel to disconnect the Telnet
   session or drop the Data Terminal Ready signal on the serial port.  A channel
   controlled by the TCI will be marked as "data set" in a unit listing;
   channels not controlled will be marked as "direct".

   The TDI and TCI may be disabled, if desired, although the TDI must be
   detached from the listening port first.  Disabling the TDI does not affect
   the simulation console, as the CPU process clock will take over console
   polling automatically.

   The Terminal Data Interface provides 16 send channels, 16 receive channels,
   and 5 auxiliary channels.  The auxiliary channels are receive-only and do
   not connect to external devices.  Rather, they may be connected as a group to
   one or more of the other channels.  Their primary purpose is to diagnose
   conditions (e.g., baud rate) on the connected channel(s).

   In hardware, a recirculating memory stores seven 8-bit words of data,
   parameters, and status for each of the 37 channels.  A set of registers form
   a "window" into the recirculating memory, and the memory makes one complete
   pass every 69.44 microseconds.  Serial transfer rates are determined by each
   channel's parameter word, which specifies the number of recirculations that
   occur for each bit sent or received.

   In simulation, the memory is represented by separate buffer, parameter, and
   status arrays.  Recirculation is simulated by indexing through each of the
   arrays in sequence.


   The TDI responds only to direct I/O instructions, as follows:

   TDI Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R |  channel number   | -   -   -   -   -   -   - | E | A |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset
     R = reset interrupts
     E = enable store of preceding data or parameter word to memory
     A = acknowledge interrupt


   TDI Status Word Format (TIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | - | C | R | L | B | -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK (always 0)
     D = direct read/write I/O OK
     I = interrupt request
     C = read/write completion flag
     R = receive/send (0/1) character interrupt
     L = character lost
     B = break status


   TDI Parameter Word Format (WIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | R | I | E | D | char size |           baud rate           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = receive/send (0/1) configuration
     I = enable channel completion interrupt
     E = enable echo (receive) or generate parity (send)
     D = diagnose using the auxiliary channels

   Character size:
     The three least-significant bits of the sum of the data, parity, and stop
     bits.  For example, 7E1 is 1001, so 001 is coded.

   Baud rate:
     The value (14400 / device bit rate) - 1.  For example, 2400 baud is 005.


   TDI Output Data Word Format (WIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 1 | -   - | S |                 send data                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S    = sync bit
     data = right-justified with leading ones


   TDI Input Data Word Format (RIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |      channel      | P |             receive data              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     P    = computed parity
     data = right-justified with leading ones


   The Terminal Control Interface provides two serial control outputs and two
   serial status inputs for each of 16 channels.  The first TCI connects to the
   Request to Send (CA) and Data Terminal Ready (CD) control lines and the Data
   Carrier Detect (CF) and Data Set Ready (CC) status lines.  Addressable
   latches hold the control line values and assert them continuously to the 16
   channels.  In addition, a 16-word by 4-bit RAM holds the expected state for
   each channel's status lines and the corresponding interrupt enable bits to
   provide notification if those lines change.

   The TCI responds only to direct I/O instructions, as follows:


   TCI Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | S | U |    channel    | W | X | Q | T | Y | Z | C | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset
     R = reset interrupts
     S = scan status
     U = enable DCD/DSR state update
     W = enable RTS change
     X = enable DTR change
     Q = new RTS state
     T = new DTR state
     Y = DCD interrupt enabled
     Z = DSR interrupt enabled
     C = expected DCD state
     D = expected DSR state


   TCI Status Word Format (TIO or RIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 1 | I | 1 |    channel    | 0 | 0 | J | K | Y | Z | C | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = interrupt request
     J = DCD interrupt present
     K = DSR interrupt present
     Y = DCD interrupt enabled
     Z = DSR interrupt enabled
     C = current DCD state
     D = current DSR state


   Implementation notes:

    1. The UNIT_MODEM flag indicates that a channel is controlled by the TCI.
       However, no modifier entry is provided, nor is one needed, as the flag is
       set automatically when the TCI first initializes the channel.  MPE
       defines separate terminal subtype numbers for directly connected
       terminals and modem-connected terminals, which are set at system
       generation time.

    2. Both TMXR_VALID and SCPE_KFLAG are set on internally generated ACKs only
       so that a debug trace will record the generation correctly.
*/



#include <ctype.h>

#include "hp3000_defs.h"
#include "hp3000_io.h"

#include "sim_tmxr.h"



/* Program limits */

#define TERM_COUNT          16                              /* number of terminal channels */
#define AUX_COUNT           5                               /* number of auxiliary channels */
#define POLL_COUNT          1                               /* number of poll units */

#define RECV_CHAN_COUNT     (TERM_COUNT + AUX_COUNT)        /* number of receive channels */
#define SEND_CHAN_COUNT     TERM_COUNT                      /* number of send channels */
#define UNIT_COUNT          (TERM_COUNT + POLL_COUNT)       /* number of units */

#define FIRST_TERM          0                               /* first terminal index */
#define LAST_TERM           (FIRST_TERM + TERM_COUNT - 1)   /* last terminal index */
#define FIRST_AUX           TERM_COUNT                      /* first auxiliary index */
#define LAST_AUX            (FIRST_AUX + AUX_COUNT - 1)     /* last auxiliary index */


/* Program constants */

#define FAST_IO_TIME        500                 /* initial fast receive/send time in event ticks */

#define POLL_RATE           100                 /* poll 100 times per second (unless synchronized) */
#define POLL_TIME           mS (10)             /* poll time is 10 milliseconds */

#define NUL                 '\000'              /* null */
#define ENQ                 '\005'              /* enquire */
#define ACK                 '\006'              /* acknowledge */
#define ASCII_MASK          000177u             /* 7-bit ASCII character set mask */

#define GEN_ACK             (TMXR_VALID | SCPE_KFLAG | ACK) /* a generated ACK character */

#define SCAN_ALL            (-1)                /* scan all channels for completion */


/* Parity functions derived from the global lookup table */

#define RECV_PARITY(c)      (odd_parity [(c) & D8_MASK] ? 0 : DDR_PARITY)
#define SEND_PARITY(c)      (odd_parity [(c) & D8_MASK] ? 0 : DDS_PARITY)


/* Debug flags */

#define DEB_CSRW            (1u << 0)           /* trace command initiations and completions */
#define DEB_XFER            (1u << 1)           /* trace data receptions and transmissions */
#define DEB_IOB             (1u << 2)           /* trace I/O bus signals and data words */
#define DEB_SERV            (1u << 3)           /* trace channel service scheduling calls */
#define DEB_PSERV           (1u << 4)           /* trace poll service scheduling calls */


/* Common per-unit multiplexer channel state variables */

#define recv_time           u3                  /* realistic receive time in event ticks */
#define send_time           u4                  /* realistic send time in event ticks */
#define stop_bits           u5                  /* stop bits to be added to each character received */


/* Device flags */

#define DEV_DIAG_SHIFT      (DEV_V_UF + 0)              /* diagnostic loopback */
#define DEV_REALTIME_SHIFT  (DEV_V_UF + 1)              /* timing mode is realistic */

#define DEV_DIAG            (1u << DEV_DIAG_SHIFT)      /* diagnostic mode flag */
#define DEV_REALTIME        (1u << DEV_REALTIME_SHIFT)  /* realistic timing flag */


/* Unit flags */

#define UNIT_CAPSLOCK_SHIFT (TTUF_V_UF + 0)             /* caps lock mode */
#define UNIT_LOCALACK_SHIFT (TTUF_V_UF + 1)             /* local ACK mode */
#define UNIT_MODEM_SHIFT    (TTUF_V_UF + 2)             /* modem control */

#define UNIT_CAPSLOCK       (1u << UNIT_CAPSLOCK_SHIFT) /* caps lock is down flag */
#define UNIT_LOCALACK       (1u << UNIT_LOCALACK_SHIFT) /* ENQ/ACK mode is local flag */
#define UNIT_MODEM          (1u << UNIT_MODEM_SHIFT)    /* channel connects to a data set flag */


/* Unit references */

#define line_unit           atcd_unit                   /* receive/send channel units */
#define poll_unit           atcd_unit [LAST_TERM + 1]   /* input polling unit */


/* Activation reasons */

typedef enum {
    Receive,
    Send,
    Loop,
    Stall
    } ACTIVATOR;


/* TDI control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R |  channel number   | -   -   -   -   -   -   - | E | A |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define DCN_MR              0100000u            /* (M) master reset */
#define DCN_IRQ_RESET       0040000u            /* (R) interrupt request reset */
#define DCN_CHAN_MASK       0037000u            /* channel number mask */
#define DCN_ENABLE          0000002u            /* (E) enable store of preceding data or parameter word */
#define DCN_ACKN            0000001u            /* (A) acknowledge interrupt */

#define DCN_CHAN_SHIFT      9                   /* channel number alignment shift */

#define DCN_CHAN(c)         (((c) & DCN_CHAN_MASK) >> DCN_CHAN_SHIFT)

static const BITSET_NAME tdi_control_names [] = {       /* TDI control word names */
    "master reset",                                     /*   bit  0 */
    "reset interrupt",                                  /*   bit  1 */
    NULL,                                               /*   bit  2 */
    NULL,                                               /*   bit  3 */
    NULL,                                               /*   bit  4 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  8 */
    NULL,                                               /*   bit  9 */
    NULL,                                               /*   bit 10 */
    NULL,                                               /*   bit 11 */
    NULL,                                               /*   bit 12 */
    NULL,                                               /*   bit 13 */
    "store word",                                       /*   bit 14 */
    "acknowledge interrupt"                             /*   bit 15 */
    };

static const BITSET_FORMAT tdi_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (tdi_control_names, 0, msb_first, no_alt, no_bar) };


/* TDI status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | D | I | - | C | R | L | B | -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define DST_DIO_OK          0040000u            /* (D) direct I/O OK to use */
#define DST_IRQ             0020000u            /* (I) interrupt requested */
#define DST_COMPLETE        0004000u            /* (C) operation is complete and channel is ready to interrupt */
#define DST_SEND_IRQ        0002000u            /* (R) interrupt request is for character sent */
#define DST_CHAR_LOST       0001000u            /* (L) character was lost */
#define DST_BREAK           0000400u            /* (B) break occurred */
#define DST_DIAGNOSE        0000000u            /* status is from an auxiliary channel (not used on ATC) */

#define DST_CHAN(n)         0                   /* position channel number for status (not used on ATC) */

static const BITSET_NAME tdi_status_names [] = {        /* TDI status word names */
    "DIO OK",                                           /*   bit  1 */
    "interrupt",                                        /*   bit  2 */
    NULL,                                               /*   bit  3 */
    "complete",                                         /*   bit  4 */
    "\1send\0receive",                                  /*   bit  5 */
    "lost",                                             /*   bit  6 */
    "break"                                             /*   bit  7 */
    };

static const BITSET_FORMAT tdi_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (tdi_status_names, 8, msb_first, has_alt, no_bar) };



/* TDI parameter word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | R | I | E | D | char size |           baud rate           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The baud rate is encoded as 14400 / device_bit_rate - 1, but the manual says
   to round the result, so that, e.g., the 110 baud rate encoding of 129.91 is
   rounded to 130.  To reconstruct the rate without floating-point calculations,
   the parameter print routine uses:

     baud_rate = (2 * 14400 / (encoded_rate + 1) + 1) / 2

   ...which is equivalent to:

     baud_rate = (int) (14400 / (encoded_rate + 1) + 0.5)

   The multiplexer pads the received character data to the left with one-bits.

   The PAD_BITS function generates the pad bits, assuming that the received
   character transmission has one stop bit.  This isn't always correct, e.g., a
   Teleprinter uses two stop bits at 110 baud, but there's no way to reconstruct
   the number of stop bits from the receive parameter word.
*/

#define DPI_IS_PARAM        0100000u            /* value is a parameter (always set) */
#define DPI_IS_SEND         0040000u            /* (R) value is a send parameter */
#define DPI_ENABLE_IRQ      0020000u            /* (I) enable interrupt requests */
#define DPI_ENABLE_PARITY   0010000u            /* (E) enable parity for send */
#define DPI_ENABLE_ECHO     0010000u            /* (E) enable echo for receive */
#define DPI_DIAGNOSE        0004000u            /* (D) connect to the auxiliary channels */
#define DPI_SIZE_MASK       0003400u            /* character size mask */
#define DPI_RATE_MASK       0000377u            /* baud rate mask */

#define DPI_CHAR_CONFIG     (DPI_SIZE_MASK | DPI_RATE_MASK) /* character configuration data */

#define DPI_SIZE_SHIFT      8                   /* character size alignment shift */
#define DPI_RATE_SHIFT      0                   /* baud rate alignment shift */

#define DPI_CHAR_SIZE(p)    (((p) & DPI_SIZE_MASK) >> DPI_SIZE_SHIFT)
#define DPI_BAUD_RATE(p)    (((p) & DPI_RATE_MASK) >> DPI_RATE_SHIFT)

#define BAUD_RATE(p)        ((28800 / (DPI_BAUD_RATE (p) + 1) + 1) / 2)

#define PAD_BITS(c)         (~((1u << bits_per_char [DPI_CHAR_SIZE (c)] - 2) - 1))


static const uint32 bits_per_char [8] = {       /* bits per character, indexed by DPI_CHAR_SIZE encoding */
    9, 10, 11, 12, 5, 6, 7, 8
    };

static const BITSET_NAME tdi_parameter_names [] = {     /* TDI parameter word names */
    "\1send\0receive",                                  /*   bit  1 */
    "enable interrupt",                                 /*   bit  2 */
    "enable parity/echo",                               /*   bit  3 */
    "diagnose"                                          /*   bit  4 */
    };

static const BITSET_FORMAT tdi_parameter_format =       /* names, offset, direction, alternates, bar */
    { FMT_INIT (tdi_parameter_names, 11, msb_first, has_alt, append_bar) };


/* TDI output (send) data word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 1 | -   - | S |                 send data                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define DDS_IS_SEND         0040000u            /* value is a send data word (always set) */
#define DDS_SYNC            0004000u            /* (S) sync */
#define DDS_DATA_MASK       0003777u            /* data value mask */
#define DDS_PARITY          0000200u            /* data parity bit */

#define DDS_MARK            (DDS_SYNC | DDS_DATA_MASK)  /* all-mark character */

#define DDS_DATA(d)         ((d) & DDS_DATA_MASK)


static const BITSET_NAME tdi_output_data_names [] = {   /* TDI output data word names */
    "send",                                             /*   bit  1 */
    NULL,                                               /*   bit  2 */
    NULL,                                               /*   bit  3 */
    "sync"                                              /*   bit  4 */
    };

static const BITSET_FORMAT tdi_output_data_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (tdi_output_data_names, 11, msb_first, no_alt, append_bar) };


/* TDI input (receive) data word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |      channel      | P |             receive data              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define DDR_CHAN_MASK       0174000u            /* channel number mask */
#define DDR_PARITY          0002000u            /* (P) computed parity bit */
#define DDR_DATA_MASK       0001777u            /* data value mask */

#define DDR_CHAN_SHIFT      11                  /* channel number alignment shift */
#define DDR_DATA_SHIFT      0                   /* data alignment shift */

#define DDR_CHAN(n)         ((n) << DDR_CHAN_SHIFT & DDR_CHAN_MASK)
#define DDR_DATA(d)         ((d) << DDR_DATA_SHIFT & DDR_DATA_MASK)

#define DDR_TO_CHAN(w)      (((w) & DDR_CHAN_MASK) >> DDR_CHAN_SHIFT)
#define DDR_TO_DATA(w)      (((w) & DDR_DATA_MASK) >> DDR_DATA_SHIFT)


static const BITSET_NAME tdi_input_data_names [] = {    /* TDI input data word names */
    "\1odd parity\0even parity",                        /*   bit  5 */
    };

static const BITSET_FORMAT tdi_input_data_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (tdi_input_data_names, 10, msb_first, has_alt, append_bar) };


/* TCI control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | S | U |    channel    | W | X | Q | T | Y | Z | C | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CCN_MR              0100000u            /* (M) master reset */
#define CCN_IRQ_RESET       0040000u            /* (R) interrupt request reset */
#define CCN_SCAN            0020000u            /* (S) scan enable */
#define CCN_UPDATE          0010000u            /* (U) update enable */
#define CCN_CHAN_MASK       0007400u            /* channel number mask */
#define CCN_ECX_MASK        0000300u            /* control output enable mask */
#define CCN_EC2             0000200u            /* (W) C2 output enable */
#define CCN_EC1             0000100u            /* (X) C1 output enable */
#define CCN_CX_MASK         0000060u            /* output mask */
#define CCN_C2              0000040u            /* (Q) C2 output [RTS] */
#define CCN_C1              0000020u            /* (T) C1 output [DTR] */
#define CCN_STAT_MASK       0000017u            /* status RAM mask */
#define CCN_ESX_MASK        0000014u            /* status interrupt enable mask */
#define CCN_ES2             0000010u            /* (Y) S2 interrupt enable */
#define CCN_ES1             0000004u            /* (Z) S1 interrupt enable */
#define CCN_SX_MASK         0000003u            /* status mask */
#define CCN_S2              0000002u            /* (C) S2 status [DCD]*/
#define CCN_S1              0000001u            /* (D) S1 status [DSR] */

#define CCN_CHAN_SHIFT      8                   /* channel number alignment shift */
#define CCN_CX_SHIFT        4                   /* control alignment shift */
#define CCN_ECX_SHIFT       2                   /* control output enable alignment shift (to Cx) */
#define CCN_ESX_SHIFT       2                   /* status interrupt enable alignment shift */

#define CCN_CHAN(c)         (((c) & CCN_CHAN_MASK) >> CCN_CHAN_SHIFT)
#define CCN_ECX(c)          (((c) & CCN_ECX_MASK)  >> CCN_ECX_SHIFT)
#define CCN_CX(c)           (((c) & CCN_CX_MASK)   >> CCN_CX_SHIFT)
#define CCN_ESX(c)          (((c) & CCN_ESX_MASK)  >> CCN_ESX_SHIFT)

static const BITSET_NAME tci_control_names [] = {       /* TCI control word names */
    "master reset",                                     /*   bit  0 */
    "reset interrupt",                                  /*   bit  1 */
    "scan",                                             /*   bit  2 */
    "update",                                           /*   bit  3 */
    NULL,                                               /*   bit  4 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  7 */
    "EC2",                                              /*   bit  8 */
    "EC1",                                              /*   bit  9 */
    "\1C2\0~C2",                                        /*   bit 10 */
    "\1C1\0~C1",                                        /*   bit 11 */
    "ES2",                                              /*   bit 12 */
    "ES1",                                              /*   bit 13 */
    "\1S2\0~S2",                                        /*   bit 14 */
    "\1S1\0~S1"                                         /*   bit 15 */
    };

static const BITSET_FORMAT tci_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (tci_control_names, 0, msb_first, has_alt, no_bar) };


/* TCI status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 1 | I | 1 |    channel    | - | - | J | K | Y | Z | C | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CST_DIO_OK          0040000u            /* direct I/O OK to use (always set) */
#define CST_IRQ             0020000u            /* (I) interrupt request */
#define CST_ON              0010000u            /* (always set) */
#define CST_CHAN_MASK       0007400u            /* channel number mask */
#define CST_IX_MASK         0000060u            /* status interrupt mask */
#define CST_I2              0000040u            /* (J) S2 interrupt */
#define CST_I1              0000020u            /* (K) S1 interrupt */
#define CST_ESX_MASK        0000014u            /* status interrupt enable mask */
#define CST_ES2             0000010u            /* (Y) S2 interrupt enable */
#define CST_ES1             0000004u            /* (Z) S1 interrupt enable */
#define CST_SX_MASK         0000003u            /* status mask */
#define CST_S2              0000002u            /* (C) S2 status [DCD] */
#define CST_S1              0000001u            /* (D) S1 status [DSR] */

#define CST_CHAN_SHIFT      8                   /* channel number alignment shift */
#define CST_IX_SHIFT        4                   /* status interrupt alignment shift */

#define CST_CHAN(n)         ((n) << CST_CHAN_SHIFT & CST_CHAN_MASK)
#define CST_IX(i)           ((i) << CST_IX_SHIFT   & CST_IX_MASK)

static const BITSET_NAME tci_status_names [] = {        /* TCI status word names */
    "interrupt",                                        /*   bit  2 */
    NULL,                                               /*   bit  3 */
    NULL,                                               /*   bit  4 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  8 */
    NULL,                                               /*   bit  9 */
    "I2",                                               /*   bit 10 */
    "I1",                                               /*   bit 11 */
    "ES2",                                              /*   bit 12 */
    "ES1",                                              /*   bit 13 */
    "\1S2\0~S2",                                        /*   bit 14 */
    "\1S1\0~S1"                                         /*   bit 15 */
    };

static const BITSET_FORMAT tci_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (tci_status_names, 0, msb_first, has_alt, no_bar) };


/* TCI #1 serial line bits */

#define RTS                 CCN_C2              /* TCI #1 C2 = Request to Send */
#define DTR                 CCN_C1              /* TCI #1 C1 = Data Terminal Ready */
#define DCD                 CCN_S2              /* TCI #1 S2 = Data Carrier Detect */
#define DSR                 CCN_S1              /* TCI #1 S1 = Data Set Ready */

static const BITSET_NAME tci_line_names [] = {  /* TCI serial line status names */
    "RTS",                                      /*   bit 10 */
    "DTR",                                      /*   bit 11 */
    NULL,                                       /*   bit 12 */
    NULL,                                       /*   bit 13 */
    "DCD",                                      /*   bit 14 */
    "DSR"                                       /*   bit 15 */
    };

static const BITSET_FORMAT tci_line_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (tci_line_names, 0, msb_first, no_alt, no_bar) };


/* ATC global state */

t_bool atc_is_polling = TRUE;                   /* TRUE if the ATC is polling for the simulation console */


/* TDI interface state */

static HP_WORD tdi_control_word = 0;            /* control word */
static HP_WORD tdi_status_word  = 0;            /* status word */
static HP_WORD tdi_read_word    = 0;            /* read word */
static HP_WORD tdi_write_word   = 0;            /* write word */

static FLIP_FLOP tdi_interrupt_mask = SET;      /* interrupt mask flip-flop */
static FLIP_FLOP tdi_data_flag      = CLEAR;    /* data flag */

static int32 fast_data_time = FAST_IO_TIME;     /* fast receive/send time */


/* TDI per-channel state */

static HP_WORD recv_status [RECV_CHAN_COUNT];   /* receive status words */
static HP_WORD recv_param  [RECV_CHAN_COUNT];   /* receive parameter words */
static HP_WORD recv_buffer [RECV_CHAN_COUNT];   /* receive character buffers */

static HP_WORD send_status [SEND_CHAN_COUNT];   /* send status words */
static HP_WORD send_param  [SEND_CHAN_COUNT];   /* send parameter words */
static HP_WORD send_buffer [SEND_CHAN_COUNT];   /* send character buffers */


/* TCI interface state */

static HP_WORD tci_control_word = 0;            /* control word */
static HP_WORD tci_status_word  = 0;            /* status word */
static uint32  tci_cntr         = 0;            /* channel counter */

static FLIP_FLOP tci_interrupt_mask = SET;      /* interrupt mask flip-flop */
static FLIP_FLOP tci_scan           = CLEAR;    /* scanning enabled flip-flop */


/* TCI per-channel state */

static uint8 cntl_status [TERM_COUNT];          /* C2/C1/S2/S1 line status */
static uint8 cntl_param  [TERM_COUNT];          /* ES2/ES1/S2/S1 parameter RAM */


/* ATC local SCP support routines */

static CNTLR_INTRF atcd_interface;
static CNTLR_INTRF atcc_interface;

static t_stat atc_set_endis   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat atc_set_mode    (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat atc_show_mode   (FILE *st,   UNIT  *uptr, int32 value, CONST void *desc);
static t_stat atc_show_status (FILE *st,   UNIT  *uptr, int32 value, CONST void *desc);

static t_stat atcd_reset (DEVICE *dptr);
static t_stat atcc_reset (DEVICE *dptr);

static t_stat atcd_attach (UNIT *uptr, CONST char *cptr);
static t_stat atcd_detach (UNIT *uptr);


/* ATC local utility routines */

static void tdi_set_interrupt (void);
static void tdi_master_reset  (void);
static void tci_master_reset  (void);

static t_stat  line_service  (UNIT    *uptr);
static t_stat  poll_service  (UNIT    *uptr);
static t_stat  activate_unit (UNIT    *uptr,   ACTIVATOR reason);
static uint32  service_time  (HP_WORD control, ACTIVATOR reason);
static void    store         (HP_WORD control, HP_WORD   data);
static void    receive       (int32   channel, int32 data, t_bool loopback);
static void    diagnose      (HP_WORD control, int32 data);
static void    scan_channels (int32   channel);
static HP_WORD scan_status   (void);


/* ATC SCP data structures */

DEVICE atcd_dev;                                /* incomplete device structure */
DEVICE atcc_dev;                                /* incomplete device structure */


/* Terminal multiplexer library structures.

   The ATC uses the connection line order feature to bypass channel 0, which is
   dedicated to the system console.  For convenience, the system console is
   connected to the simulation console.  As such, it calls the console I/O
   routines instead of the terminal multiplexer routines.

   User-defined line order is not supported.
*/

static int32 atcd_order [TERM_COUNT] = {        /* line connection order */
    1,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11, 12, 13, 14, 15 };

static TMLN atcd_ldsc [TERM_COUNT] = {          /* line descriptors */
    { 0 }
    };

static TMXR atcd_mdsc = {                       /* multiplexer descriptor */
    TERM_COUNT,                                 /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    atcd_ldsc,                                  /* line descriptors */
    atcd_order,                                 /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
    };


/* Device information blocks */

static DIB atcd_dib = {
    &atcd_interface,                            /* device interface */
    7,                                          /* device number */
    SRNO_UNUSED,                                /* service request number */
    0,                                          /* interrupt priority */
    INTMASK_E                                   /* interrupt mask */
    };

static DIB atcc_dib = {
    &atcc_interface,                            /* device interface */
    8,                                          /* device number */
    SRNO_UNUSED,                                /* service request number */
    8,                                          /* interrupt priority */
    INTMASK_E                                   /* interrupt mask */
    };


/* Unit lists.

   The first sixteen TDI units correspond to the sixteen multiplexer main
   send/receive channels.  These handle character I/O via the Telnet library.  A
   seventeenth unit is responsible for polling for connections and socket I/O.
   It also holds the master socket.

   Channel 0 is reserved for the system console and is connected to the
   simulation console.  As such, it's not likely to be using an HP terminal
   emulator, so the default is CAPSLOCK input mode and 7P output mode.  The
   remainder of the channels default to NOCAPSLOCK and 7B, as they're likely to
   be connected to HP terminals or terminal emulators.  All channels initially
   omit the UNIT_MODEM flag to allow the MPE terminal subtype configuration to
   determine which channels support data sets and which do not.

   The TDI line service routine runs only when there are characters to read or
   write.  It is scheduled either at a realistic rate corresponding to the
   programmed baud rate of the channel to be serviced, or at a somewhat faster
   optimized rate.  The multiplexer connection and input poll must run
   continuously, but it may operate much more slowly, as the only requirement is
   that it must not present a perceptible lag to human input.  It is coscheduled
   with the process clock to permit idling.  The poll unit is hidden by
   disabling it, so as to present a logical picture of the multiplexer to the
   user.

   The TCI does not use any units, but a dummy one is defined to satisfy SCP
   requirements.


   Implementation notes:

    1. There are no units corresponding to the auxiliary receive channels.  This
       is because reception isn't scheduled on these channels but instead occurs
       concurrently with the main channel that is connected to the auxiliary
       channels.
*/

static UNIT atcd_unit [UNIT_COUNT] = {
    { UDATA (&line_service, TT_MODE_7P | UNIT_LOCALACK | UNIT_CAPSLOCK,  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&line_service, TT_MODE_7B | UNIT_LOCALACK,                  0)            },
    { UDATA (&poll_service, UNIT_ATTABLE | UNIT_DIS | UNIT_IDLE,         0), POLL_TIME }  /* multiplexer poll unit */
    };

static UNIT atcc_unit [] = {                    /* a dummy unit to satisfy SCP requirements */
    { UDATA (NULL, 0, 0) }
    };


/* Register lists.

   The internal state of the TDI and TCI are exposed to the user and to ensure
   that SAVE and RESTORE pick up the values.  The user may set FTIME explicitly
   as needed to accommodate software that does not work with the default
   setting.


   Implementation notes:

    1. The TCI control and status line register definitions use the VM-defined
       FBDATA macro.  This macro defines a flag that is replicated in the same
       bit position in each element of an array.
*/

static REG atcd_reg [] = {
/*    Macro   Name    Location             Radix  Width  Offset       Depth             Flags      */
/*    ------  ------  -------------------  -----  -----  ------  ----------------  --------------- */
    { ORDATA (CNTL,   tdi_control_word,            16),                                    REG_FIT },
    { ORDATA (STAT,   tdi_status_word,             16),                                    REG_FIT },
    { ORDATA (READ,   tdi_read_word,               16),                            REG_A | REG_FIT },
    { ORDATA (WRITE,  tdi_write_word,              16),                            REG_A | REG_FIT },
    { FLDATA (FLAG,   tdi_data_flag,                        0)                                     },
    { FLDATA (MASK,   tdi_interrupt_mask,                   0)                                     },
    { DRDATA (FTIME,  fast_data_time,              24),                            PV_LEFT         },
    { BRDATA (RSTAT,  recv_status,           8,    16,           RECV_CHAN_COUNT)                  },
    { BRDATA (RPARM,  recv_param,            8,    16,           RECV_CHAN_COUNT)                  },
    { BRDATA (RBUFR,  recv_buffer,           8,    16,           RECV_CHAN_COUNT), REG_A           },
    { BRDATA (SSTAT,  send_status,           8,    16,           SEND_CHAN_COUNT)                  },
    { BRDATA (SPARM,  send_param,            8,    16,           SEND_CHAN_COUNT)                  },
    { BRDATA (SBUFR,  send_buffer,           8,    16,           SEND_CHAN_COUNT), REG_A           },
    { FLDATA (POLL,   atc_is_polling,                       0),                    REG_HRO         },

      DIB_REGS (atcd_dib),

    { NULL }
    };

static REG atcc_reg [] = {
/*    Macro   Name    Location             Width  Offset    Depth      Flags   */
/*    ------  ------  -------------------  -----  ------  ----------  -------  */
    { ORDATA (CNTL,   tci_control_word,     16),                      REG_FIT  },
    { ORDATA (STAT,   tci_status_word,      16),                      REG_FIT  },
    { DRDATA (CNTR,   tci_cntr,              4)                                },
    { FLDATA (SCAN,   tci_scan,                      0)                        },
    { FLDATA (MASK,   tci_interrupt_mask,            0)                        },

    { FBDATA (C2,     cntl_status,                   5,   TERM_COUNT, PV_RZRO) },
    { FBDATA (C1,     cntl_status,                   4,   TERM_COUNT, PV_RZRO) },
    { FBDATA (S2,     cntl_status,                   1,   TERM_COUNT, PV_RZRO) },
    { FBDATA (S1,     cntl_status,                   0,   TERM_COUNT, PV_RZRO) },

    { FBDATA (ES2,    cntl_param,                    3,   TERM_COUNT, PV_RZRO) },
    { FBDATA (ES1,    cntl_param,                    2,   TERM_COUNT, PV_RZRO) },
    { FBDATA (MS2,    cntl_param,                    1,   TERM_COUNT, PV_RZRO) },
    { FBDATA (MS1,    cntl_param,                    0,   TERM_COUNT, PV_RZRO) },

      DIB_REGS (atcc_dib),

    { NULL }
    };


/* Modifier lists */

typedef enum {
    Fast_Time,
    Real_Time,
    Terminal,
    Diagnostic
    } DEVICE_MODES;


static MTAB atcd_mod [] = {
/*    Mask Value     Match Value    Print String      Match String  Validation  Display  Descriptor */
/*    -------------  -------------  ----------------  ------------  ----------  -------  ---------- */
    { UNIT_MODEM,    UNIT_MODEM,    "data set",       NULL,         NULL,       NULL,    NULL       },
    { UNIT_MODEM,    0,             "direct",         NULL,         NULL,       NULL,    NULL       },

    { UNIT_LOCALACK, UNIT_LOCALACK, "local ENQ/ACK",  "LOCALACK",   NULL,       NULL,    NULL       },
    { UNIT_LOCALACK, 0,             "remote ENQ/ACK", "REMOTEACK",  NULL,       NULL,    NULL       },

    { UNIT_CAPSLOCK, UNIT_CAPSLOCK, "CAPS LOCK down", "CAPSLOCK",   NULL,       NULL,    NULL       },
    { UNIT_CAPSLOCK, 0,             "CAPS LOCK up",   "NOCAPSLOCK", NULL,       NULL,    NULL       },

    { TT_MODE,       TT_MODE_UC,    "UC output",      "UC",         NULL,       NULL,    NULL       },
    { TT_MODE,       TT_MODE_7B,    "7b output",      "7B",         NULL,       NULL,    NULL       },
    { TT_MODE,       TT_MODE_7P,    "7p output",      "7P",         NULL,       NULL,    NULL       },
    { TT_MODE,       TT_MODE_8B,    "8b output",      "8B",         NULL,       NULL,    NULL       },

/*    Entry Flags           Value        Print String   Match String  Validation       Display           Descriptor          */
/*    --------------------  -----------  -------------  ------------  ---------------  ----------------  ------------------- */
    { MTAB_XUN | MTAB_NC,   0,           "LOG",         "LOG",        &tmxr_set_log,   &tmxr_show_log,   (void *) &atcd_mdsc },
    { MTAB_XUN | MTAB_NC,   0,           NULL,          "NOLOG",      &tmxr_set_nolog, NULL,             (void *) &atcd_mdsc },
    { MTAB_XUN,             0,           NULL,          "DISCONNECT", &tmxr_dscln,     NULL,             (void *) &atcd_mdsc },

    { MTAB_XDV,             Fast_Time,   NULL,          "FASTTIME",   &atc_set_mode,   NULL,             (void *) &atcd_dev  },
    { MTAB_XDV,             Real_Time,   NULL,          "REALTIME",   &atc_set_mode,   NULL,             (void *) &atcd_dev  },
    { MTAB_XDV,             Terminal,    NULL,          "TERMINAL",   &atc_set_mode,   NULL,             (void *) &atcd_dev  },
    { MTAB_XDV,             Diagnostic,  NULL,          "DIAGNOSTIC", &atc_set_mode,   NULL,             (void *) &atcd_dev  },
    { MTAB_XDV,             0,           "MODES",       NULL,         NULL,            &atc_show_mode,   (void *) &atcd_dev  },

    { MTAB_XDV,             0,           "",            NULL,         NULL,            &atc_show_status, (void *) &atcd_mdsc },
    { MTAB_XDV | MTAB_NMO,  1,           "CONNECTIONS", NULL,         NULL,            &tmxr_show_cstat, (void *) &atcd_mdsc },
    { MTAB_XDV | MTAB_NMO,  0,           "STATISTICS",  NULL,         NULL,            &tmxr_show_cstat, (void *) &atcd_mdsc },

    { MTAB_XDV,             VAL_DEVNO,   "DEVNO",       "DEVNO",      &hp_set_dib,     &hp_show_dib,     (void *) &atcd_dib  },
    { MTAB_XDV,             VAL_INTMASK, "INTMASK",     "INTMASK",    &hp_set_dib,     &hp_show_dib,     (void *) &atcd_dib  },
    { MTAB_XDV,             VAL_INTPRI,  "INTPRI",      "INTPRI",     &hp_set_dib,     &hp_show_dib,     (void *) &atcd_dib  },

    { MTAB_XDV | MTAB_NMO,  1,           NULL,          "ENABLED",    &atc_set_endis,  NULL,             NULL                },
    { MTAB_XDV | MTAB_NMO,  0,           NULL,          "DISABLED",   &atc_set_endis,  NULL,             NULL                },
    { 0 }
    };

static MTAB atcc_mod [] = {
/*    Entry Flags  Value        Print String  Match String  Validation     Display         Descriptor         */
/*    -----------  -----------  ------------  ------------  -------------  --------------  ------------------ */
    { MTAB_XDV,    Terminal,    NULL,         "TERMINAL",   &atc_set_mode, NULL,           (void *) &atcc_dev },
    { MTAB_XDV,    Diagnostic,  NULL,         "DIAGNOSTIC", &atc_set_mode, NULL,           (void *) &atcc_dev },
    { MTAB_XDV,    1,           "MODES",      NULL,         NULL,          &atc_show_mode, (void *) &atcc_dev },

    { MTAB_XDV,    VAL_DEVNO,   "DEVNO",      "DEVNO",      &hp_set_dib,   &hp_show_dib,   (void *) &atcc_dib },
    { MTAB_XDV,    VAL_INTMASK, "INTMASK",    "INTMASK",    &hp_set_dib,   &hp_show_dib,   (void *) &atcc_dib },
    { MTAB_XDV,    VAL_INTPRI,  "INTPRI",     "INTPRI",     &hp_set_dib,   &hp_show_dib,   (void *) &atcc_dib },
    { 0 }
    };


/* Debugging trace lists */

static DEBTAB atcd_deb [] = {
    { "CSRW",  DEB_CSRW  },                     /* Interface control, status, read, and write actions */
    { "SERV",  DEB_SERV  },                     /* Channel unit service scheduling calls */
    { "PSERV", DEB_PSERV },                     /* Poll unit service scheduling calls */
    { "XFER",  DEB_XFER  },                     /* Data receptions and transmissions */
    { "IOBUS", DEB_IOB   },                     /* Interface I/O bus signals and data words */
    { NULL,    0         }
    };

static DEBTAB atcc_deb [] = {
    { "CSRW",  DEB_CSRW  },                     /* Interface control, status, read, and write actions */
    { "PSERV", DEB_PSERV },                     /* Poll unit service scheduling calls */
    { "XFER",  DEB_XFER  },                     /* Control and status line changes */
    { "IOBUS", DEB_IOB   },                     /* Interface I/O bus signals and data words */
    { NULL,    0         }
    };


/* Device descriptors.

   Both devices may be disabled.  However, we want to be able to disable the TDI
   while it is polling for the simulation console, which the standard SCP
   routine will not do (it refuses if any unit is active).  So we define our own
   DISABLED and ENABLED modifiers and a validation routine that sets or clears
   the DEV_DIS flag and then calls atcd_reset.  The reset routine cancels or
   reenables the poll as indicated.


   Implementation notes:

    1. The ATCD device does not specify the DEV_DISABLE flag to avoid the
       DISABLED and ENABLED modifiers from being listed twice for a SHOW ATCD
       MODIFIERS command.  SIMH 3.9 tested for user-defined ENABLED/DISABLED
       modifiers and skipped the printing that results from specifying
       DEV_DISABLE.  SIMH 4.0 no longer does this, so we omit the flag to
       suppress the duplicate printing (the flag is otherwise used only to
       validate the SET DISABLED command).
*/

DEVICE atcd_dev = {
    "ATCD",                                     /* device name */
    atcd_unit,                                  /* unit array */
    atcd_reg,                                   /* register array */
    atcd_mod,                                   /* modifier array */
    UNIT_COUNT,                                 /* number of units */
    10,                                         /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    DV_WIDTH,                                   /* data width */
    &tmxr_ex,                                   /* examine routine */
    &tmxr_dep,                                  /* deposit routine */
    &atcd_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    &atcd_attach,                               /* attach routine */
    &atcd_detach,                               /* detach routine */
    &atcd_dib,                                  /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    atcd_deb,                                   /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL };                                     /* logical device name */

DEVICE atcc_dev = {
    "ATCC",                                     /* device name */
    atcc_unit,                                  /* unit array */
    atcc_reg,                                   /* register array */
    atcc_mod,                                   /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    PA_WIDTH,                                   /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    DV_WIDTH,                                   /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &atcc_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &atcc_dib,                                  /* device information block pointer */
    DEV_DEBUG | DEV_DISABLE,                    /* device flags */
    0,                                          /* debug control flags */
    atcc_deb,                                   /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL };                                     /* logical device name */



/* ATC local SCP support routines */



/* TDI interface.

   The interface is installed on the IOP bus and receives direct I/O commands
   from the IOP.  In simulation, the asserted signals on the bus are represented
   as bits in the inbound_signals set.  Each signal is processed sequentially in
   numerical order, and a set of similar outbound_signals is assembled and
   returned to the caller, simulating assertion of the corresponding backplane
   signals.

   Before a channel can receive or send, it must be configured.  The number of
   the channel to configure is set via a CIO instruction, followed by parameters
   for baud rate and character size via WIO instructions.  Data to be sent is
   passed to the interface via WIO, while received data is picked up with RIO
   instructions.

   When a channel has completed sending or receiving a character, it will set
   its completion flag.  If the TDI data flag is clear, indicating that all
   prior interrupts have been serviced, a scan of the serviced channel is made
   to see if the channel is enabled to interrupt.  If it is, the TDI data flag
   will be set, the channel flag will be cleared, and an interrupt will be
   requested.  When the interrupt is serviced and acknowledged, the flag will be
   cleared, and the scan will continue to look for other channel flags.

   The status word is set during the scan to reflect the interrupting channel
   status.  If status bit 3 (DST_COMPLETE) is clear, then status bits 5, 6, and
   7 (DST_SEND_IRQ, DST_CHAR_LOST, and DST_BREAK) retain their values from the
   prior send or receive interrupt.


   Implementation notes:

    1. In hardware, the DIO OK status bit (bit 1) is denied when a store to the
       recirculating memory is pending and is reasserted once the designated
       channel rotates into the window and the parameter or data is stored.  The
       duration of the denial varies from 0 to 69.44 microseconds, depending on
       the location of the window in memory when DWRITESTB is asserted.  In
       simulation, DIO OK is always asserted.

    2. Receipt of a DRESETINT signal clears the interrupt request and active
       flip-flops but does not cancel a request pending but not yet serviced by
       the IOP.  However, when the IOP does service the request by asserting
       INTPOLLIN, the interface routine returns INTPOLLOUT, which will cancel
       the request.
*/

static SIGNALS_DATA atcd_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (atcd_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case DCONTSTB:
            dprintf (atcd_dev, DEB_CSRW, (inbound_value & DCN_ENABLE
                                           ? "Control is %s | channel %u\n"
                                           : "Control is %s\n"),
                     fmt_bitset (inbound_value, tdi_control_format),
                     DCN_CHAN (inbound_value));

            tdi_control_word = inbound_value;           /* save the control word */

            if (tdi_control_word & DCN_MR)              /* if master reset is requested */
                tdi_master_reset ();                    /*   then perform an I/O reset */

            if (tdi_control_word & DCN_IRQ_RESET)       /* if reset interrupt is requested */
                dibptr->interrupt_request = CLEAR;      /*   then clear the interrupt request */

            if (tdi_control_word & DCN_ENABLE)              /* if output is enabled */
                store (tdi_control_word, tdi_write_word);   /*   then store the parameter or data word */

            if (tdi_control_word & DCN_ACKN) {          /* if acknowledge interrupt is requested */
                tdi_data_flag = CLEAR;                  /*   then clear the data flag */

                scan_channels (SCAN_ALL);               /* scan all channels for a new interrupt request */
                }
            break;


        case DSTATSTB:
            tdi_status_word |= DST_DIO_OK;              /* the interface is always ready for commands */

            if (dibptr->interrupt_request == SET)       /* reflect the interrupt request value */
                tdi_status_word |= DST_IRQ;             /*   in the status word */
            else                                        /*     to indicate */
                tdi_status_word &= ~DST_IRQ;            /*       whether or not a request is pending */

            if (tdi_data_flag == SET)                   /* reflect the data flag value */
                tdi_status_word |= DST_COMPLETE;        /*   in the status word */
            else                                        /*     to indicate */
                tdi_status_word &= ~DST_COMPLETE;       /*       whether or not a channel has completed */

            outbound_value = tdi_status_word;           /* return the status word */

            dprintf (atcd_dev, DEB_CSRW, "Status is %s\n",
                     fmt_bitset (outbound_value, tdi_status_format));
            break;


        case DWRITESTB:
            tdi_write_word = inbound_value;             /* save the data or parameter word */

            if (DPRINTING (atcd_dev, DEB_CSRW))
                if (inbound_value & DPI_IS_PARAM)
                    hp_debug (&atcd_dev, DEB_CSRW, "Parameter is %s%u bits | %u baud\n",
                              fmt_bitset (inbound_value, tdi_parameter_format),
                              bits_per_char [DPI_CHAR_SIZE (inbound_value)],
                              BAUD_RATE (inbound_value));

                else
                    hp_debug (&atcd_dev, DEB_CSRW, "Output data is %s%04o\n",
                              fmt_bitset (inbound_value, tdi_output_data_format),
                              DDS_DATA (inbound_value));
            break;


        case DREADSTB:
            outbound_value = tdi_read_word;             /* return the data word */

            dprintf (atcd_dev, DEB_CSRW, "Input data is channel %u | %s%04o\n",
                     DDR_TO_CHAN (outbound_value),
                     fmt_bitset (outbound_value, tdi_input_data_format),
                     DDR_TO_DATA (outbound_value));
            break;


        case DSETINT:
            dibptr->interrupt_request = SET;            /* request an interrupt */

            if (tdi_interrupt_mask)                     /* if the interrupt mask is satisfied */
                outbound_signals |= INTREQ;             /*   then assert the INTREQ signal */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* reset the interrupt active flip-flop */
            break;


        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it now active */

                outbound_signals |= INTACK;             /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }
            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case DSETMASK:
            if (dibptr->interrupt_mask == INTMASK_E)                /* if the mask is always enabled */
                tdi_interrupt_mask = SET;                           /*   then set the mask flip-flop */
            else                                                    /* otherwise */
                tdi_interrupt_mask = D_FF (dibptr->interrupt_mask   /*   set the mask flip-flop if the mask bit */
                                           & inbound_value);        /*     is present in the mask value */

            if (tdi_interrupt_mask && dibptr->interrupt_request)    /* if the mask is enabled and a request is pending */
                outbound_signals |= INTREQ;                         /*   then assert INTREQ */
            break;


        case DSTARTIO:                                  /* not used by this interface */
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

dprintf (atcd_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* TCI interface.

   The interface is installed on the IOP bus and receives direct I/O commands
   from the IOP.  In simulation, the asserted signals on the bus are represented
   as bits in the inbound_signals set.  Each signal is processed sequentially in
   numerical order, and a set of similar outbound_signals is assembled and
   returned to the caller, simulating assertion of the corresponding backplane
   signals.  For this interface, a read order executes identically to a test
   order, and a write order is ignored.

   The control word contains three independent enables that affect the
   interpretation of the rest of the word.  Bit 3 (CCN_UPDATE) must be set to
   enable storing bits 12-15 (CCN_ES2/1 and CCN_S2/1) into the state RAM.  Bits
   8 (CCN_EC2) and 9 (CCN_EC1) must be set to enable storing bits 10 (CCN_C2)
   and 11 (CCN_C1), respectively, into the addressable latch.  If none of these
   enables are set, then only bits 0-2 are interpreted.


   Implementation notes:

    1. The cntl_status array contains the values for the serial device control
       and status lines.  The line bit positions in the array correspond to the
       C2/C1 and S2/S1 positions in the control word.

    2. A control word write directed to a given channel sets that channel's
       UNIT_MODEM flag to indicate that the serial line status should be
       updated at each input poll service.

    3. The terminal multiplexer library will disconnect an associated Telnet
       session if DTR is dropped.

    4. Receipt of a DRESETINT signal clears the interrupt request and active
       flip-flops but does not cancel a request pending but not yet serviced by
       the IOP.  However, when the IOP does service the request by asserting
       INTPOLLIN, the interface routine returns INTPOLLOUT, which will cancel
       the request.
*/

static SIGNALS_DATA atcc_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;
int32          set_lines, clear_lines;

dprintf (atcc_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case DCONTSTB:
            tci_cntr = CCN_CHAN (inbound_value);        /* set the counter to the target channel */

            dprintf (atcc_dev, DEB_CSRW, "Control is channel %u | %s\n",
                     tci_cntr, fmt_bitset (inbound_value, tci_control_format));

            tci_control_word = inbound_value;           /* save the control word */

            line_unit [tci_cntr].flags |= UNIT_MODEM;   /* set the modem control flag on this unit */

            if (tci_control_word & CCN_MR)              /* if master reset is requested */
                tci_master_reset ();                    /*   then perform an I/O reset */

            if (tci_control_word & CCN_IRQ_RESET)       /* if reset interrupt is requested */
                dibptr->interrupt_request = CLEAR;      /*   then clear the interrupt request */

            cntl_status [tci_cntr] = cntl_status [tci_cntr]             /* set the control lines */
                                       & ~CCN_ECX (tci_control_word)    /*   that are enabled for output */
                                     | CCN_CX_MASK                      /*     to the control bits */
                                       & CCN_ECX (tci_control_word)     /*       that are enabled */
                                       & tci_control_word;              /*         in the control word */

            dprintf (atcc_dev, DEB_XFER, "Channel %u line status is %s\n",
                     tci_cntr, fmt_bitset (cntl_status [tci_cntr], tci_line_format));

            if (atcc_dev.flags & DEV_DIAG) {                /* if the interface is in diagnostic mode */
                cntl_status [tci_cntr ^ 1] =                /*   then loop the control lines */
                  cntl_status [tci_cntr ^ 1] & ~CCN_SX_MASK /*     back to the alternate channel */
                  | CCN_CX (cntl_status [tci_cntr]);        /*       from the selected channel */

                dprintf (atcc_dev, DEB_XFER, "Channel %u line status is %s\n",
                         tci_cntr ^ 1, fmt_bitset (cntl_status [tci_cntr ^ 1], tci_line_format));
                }

            else if (tci_control_word & CCN_ECX_MASK) {     /* otherwise if either control line is enabled */
                set_lines = 0;                              /*   then prepare the multiplexer library to set */
                clear_lines = 0;                            /*      the modem status (either real or simulated) */

                if (tci_control_word & CCN_EC2)             /* if control line 2 is enabled for output */
                    if (RTS & cntl_status [tci_cntr])       /*   then if the line is asserted */
                        set_lines |= TMXR_MDM_RTS;          /*     then set the RTS line up */
                    else                                    /*   otherwise */
                        clear_lines |= TMXR_MDM_RTS;        /*     set it down */

                if (tci_control_word & CCN_EC1)             /* if control line 1 is enabled for output */
                    if (DTR & cntl_status [tci_cntr])       /*   then if the line is asserted */
                        set_lines |= TMXR_MDM_DTR;          /*     then set the DTR line up */
                    else {                                  /*   otherwise */
                        clear_lines |= TMXR_MDM_DTR;        /*     set it down */

                        if (cntl_status [tci_cntr] & DCD)    /* setting DTR down will disconnect the channel */
                            dprintf (atcc_dev, DEB_CSRW, "Channel %u disconnected by DTR drop\n",
                                     tci_cntr);
                        }

                tmxr_set_get_modem_bits (&atcd_ldsc [tci_cntr],     /* tell the multiplexer library */
                                         set_lines, clear_lines,    /*   to set or clear the indicated lines */
                                         NULL);                     /*     and omit returning the current status */
                }

            if (tci_control_word & CCN_UPDATE)              /* if the status output is enabled */
                cntl_param [tci_cntr] = tci_control_word    /*   then store the status line enables and states */
                                          & CCN_STAT_MASK;  /*     in the parameter RAM */

            tci_scan = D_FF (tci_control_word & CCN_SCAN);  /* set or clear the scan flip-flop as directed */

            if (tci_scan)                               /* if scanning is enabled */
                scan_status ();                         /*   then look for channel status changes */
            break;


        case DREADSTB:                                  /* RIO and TIO return the same value */
        case DSTATSTB:
            tci_status_word = CST_DIO_OK | CST_ON       /* form the status word */
                               | CST_CHAN (tci_cntr)
                               | cntl_param [tci_cntr] & CST_ESX_MASK
                               | cntl_status [tci_cntr] & CST_SX_MASK
                               | scan_status ();

            if (dibptr->interrupt_request == SET)       /* reflect the interrupt request value */
                tci_status_word |= CST_IRQ;             /*   in the status word */

            outbound_value = tci_status_word;           /* return the status word */

            dprintf (atcc_dev, DEB_CSRW, "Status is channel %u | %s\n",
                     tci_cntr, fmt_bitset (outbound_value, tci_status_format));
            break;


        case DSETINT:
            dibptr->interrupt_request = SET;            /* request an interrupt */

            if (tci_interrupt_mask)                     /* if the interrupt mask is satisfied */
                outbound_signals |= INTREQ;             /*   then assert the INTREQ signal */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* reset the interrupt active flip-flop */
            break;


        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it now active */

                outbound_signals |= INTACK;             /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }
            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case DSETMASK:
            if (dibptr->interrupt_mask == INTMASK_E)                /* if the mask is always enabled */
                tci_interrupt_mask = SET;                           /*   then set the mask flip-flop */
            else                                                    /* otherwise */
                tci_interrupt_mask = D_FF (dibptr->interrupt_mask   /*   set the mask flip-flop if the mask bit */
                                           & inbound_value);        /*     is present in the mask value */

            if (tci_interrupt_mask && dibptr->interrupt_request)    /* if the mask is enabled and a request is pending */
                outbound_signals |= INTREQ;                         /*   then assert INTREQ */
            break;


        case DWRITESTB:                                 /* not used by this interface */
        case DSTARTIO:                                  /* not used by this interface */
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

dprintf (atcc_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Enable or disable the TDI.

   This validation routine is entered with "value" set to 1 for an ENABLE and 0
   for a DISABLE, and "cptr" pointing to the next character after the keyword.
   If the TDI is already enabled or disabled, respectively, the routine returns
   with no further action.  Otherwise, if "value" is 1, the device is enabled by
   clearing the DEV_DIS flag, and the polling flag is set TRUE to indicate that
   the TDI is polling for the simulation console.  If "value" is 0, a check is
   made to see if the TDI is listening for connections.  If it is, the disable
   request is rejected; the device must be detached first.  Otherwise, the
   device is disabled by setting the DEV_DIS flag, and the polling flag is set
   FALSE to indicate that the TDI is no longer polling for the simulation
   console (the PCLK device will take over when the polling flag is FALSE).

   In either case, the device is reset, which will restart or cancel the poll,
   as appropriate.
*/

static t_stat atc_set_endis (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (value)                                              /* if this is an ENABLE request */
    if (atcd_dev.flags & DEV_DIS) {                     /*   then if the device is disabled */
        atcd_dev.flags &= ~DEV_DIS;                     /*     then reenable it */
        atc_is_polling = TRUE;                          /*       and set the polling flag */
        }

    else                                                /*   otherwise the device is already enabled */
        return SCPE_OK;                                 /*     so there's nothing to do */

else                                                    /* otherwise this is a DISABLE request */
    if (atcd_dev.flags & DEV_DIS)                       /*   so if the device is already disabled */
        return SCPE_OK;                                 /*     so there's nothing to do */

    else if (poll_unit.flags & UNIT_ATT)                /*   otherwise if the poll unit is still attached */
        return SCPE_NOFNC;                              /*     then report that the command failed */

    else {                                              /*   otherwise */
        atcd_dev.flags |= DEV_DIS;                      /*     disable the device */
        atc_is_polling = FALSE;                         /*       and clear the polling flag */
        }

return atcd_reset (&atcd_dev);                          /* reset the TDI and restart or cancel polling */
}


/* Set the device modes.

   The device flag implied by the DEVICE_MODES "value" passed to the routine is
   set or cleared in the device specified by the "desc" parameter.  The unit and
   character pointers are not used.


   Implementation notes:

    1. In hardware, terminals and modems must be disconnected from the ATC and
       loopback cables installed between each pair or channels when the
       diagnostic is run.  In simulation, setting DIAG mode detaches any
       existing listening port, so that Telnet sessions will not interfere with
       the internal loopback connections from the send to the receive channels.
*/

static t_stat atc_set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
DEVICE * const dptr = (DEVICE *) desc;                  /* a pointer to the device */

switch ((DEVICE_MODES) value) {                         /* dispatch based on the mode to set */

    case Fast_Time:                                     /* entering optimized timing mode */
        dptr->flags &= ~DEV_REALTIME;                   /*   so clear the real-time flag */
        break;


    case Real_Time:                                     /* entering realistic timing mode */
        dptr->flags |= DEV_REALTIME;                    /*   so set the flag */
        break;


    case Terminal:                                      /* entering terminal mode */
        dptr->flags &= ~DEV_DIAG;                       /*   so clear the diagnostic flag */
        break;


    case Diagnostic:                                    /* entering the diagnostic mode */
        dptr->flags |= DEV_DIAG;                        /*   so set the flag */

        if (dptr == &atcd_dev)                          /* if we're setting the TDI mode */
            atcd_detach (&poll_unit);                   /*   then detach any existing connections */
        break;
    }

return SCPE_OK;
}


/* Show the device modes.

   The output stream and device pointer are passed in the "st" and "desc"
   parameters, respectively.  If "value" is 0, then all of the flags are checked
   for the TDI.  If "value" is 1, then only the diagnostic flag is checked for
   the TCI.  The unit pointer is not used.
*/

static t_stat atc_show_mode (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const DEVICE * const dptr = (const DEVICE *) desc;      /* a pointer to the device */

if (value == 0)                                         /* if this is the TDI */
    if (dptr->flags & DEV_REALTIME)                     /*   then if the real-time flag is set */
        fputs ("realistic timing, ", st);               /*     then report that we are using realistic timing */
    else                                                /*   otherwise */
        fputs ("fast timing, ", st);                    /*     report that we are using optimized timing */

if (dptr->flags & DEV_DIAG)                             /* if the diagnostic flag is set */
    fputs ("diagnostic mode", st);                      /*   then report that we're in loopback mode */
else                                                    /* otherwise */
    fputs ("terminal mode", st);                        /*   we're in normal (terminal) mode */

return SCPE_OK;
}


/* Show the TDI device status.

   The attachment condition and connection count are printed to the stream
   specified by "st" as part of the ATCD device display.  The "desc" parameter
   is a pointer to the terminal multiplexer library descriptor; the unit pointer
   and value parameters are not used.
*/

static t_stat atc_show_status (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
if (poll_unit.flags & UNIT_ATT)                         /* if the poll unit is attached */
    fprintf (st, "attached to port %s, ",               /*   then report it */
             poll_unit.filename);                       /*     with the listening port number */
else                                                    /* otherwise */
    fprintf (st, "not attached, ");                     /*   report the condition */

tmxr_show_summ (st, uptr, value, desc);                 /* also report the count of connections */

return SCPE_OK;
}


/* TDI device reset.

   This routine is called for a RESET or RESET ATCD command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   If a power-on reset (RESET -P) is being done, the poll timer is initialized.
   In addition, the original FASTTIME setting is restored, in case it's been
   changed by the user.

   If the polling flag is set, then start or resynchronize the poll unit with
   the process clock to enable idling.  If the CPU process clock is calibrated,
   then the poll event service is synchronized with the process clock service.
   Otherwise, the service time is set up but is otherwise asynchronous with the
   process clock.

   If the polling flag is clear, then the poll is stopped, as it's not needed.


   Implementation notes:

    1. To synchronize events, the poll must be activated absolutely, as a
       service event may already be scheduled, and normal activation will not
       disturb an existing event.
*/

static t_stat atcd_reset (DEVICE *dptr)
{
tdi_master_reset ();                                    /* perform a master reset */

if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    sim_rtcn_init (poll_unit.wait, TMR_ATC);            /*   then initialize the poll timer */
    fast_data_time = FAST_IO_TIME;                      /* restore the initial fast data time */
    }

if (atc_is_polling) {                                       /* if we're polling for the simulation console */
    if (cpu_is_calibrated)                                  /*   then if the process clock is calibrated */
        poll_unit.wait = sim_activate_time (cpu_pclk_uptr); /*     then synchronize with it */
    else                                                    /*   otherwise */
        poll_unit.wait = POLL_TIME;                         /*     set up an independent poll time */

    sim_activate_abs (&poll_unit, poll_unit.wait);          /* restart the poll timer */
    }

else                                                    /* otherwise */
    sim_cancel (&poll_unit);                            /*   cancel the poll */

return SCPE_OK;
}


/* TCI device reset.

   This routine is called for a RESET or RESET ATCC command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   If a power-on reset (RESET -P) is being done, then local modem control is
   established by setting DTR on all channels.  This is necessary so that
   channels not controlled by the TCI will be able to connect (TCI-controlled
   channels will have their DTR and RTS state set by the MPE TCI initialization
   routine).
*/

static t_stat atcc_reset (DEVICE *dptr)
{
uint32 channel;

tci_master_reset ();                                        /* perform a master reset */

if (sim_switches & SWMASK ('P')) {                          /* if this is a power-on reset */
    for (channel = 0; channel < TERM_COUNT; channel++)      /*   then for each terminal channel */
        tmxr_set_get_modem_bits (&atcd_ldsc [channel],      /*     set the DTR line on */
                                 TMXR_MDM_DTR,              /*       to allow non-TCI channels to connect */
                                 0, NULL);
    }

return SCPE_OK;
}


/* Attach the TDI to a Telnet listening port.

   This routine is called by the ATTACH ATCD <port> command to attach the TDI to
   the listening port indicated by <port>.  Logically, it is the ATCD device
   that is attached; however, SIMH only allows units to be attached.  This makes
   sense for devices such as tape drives, where the attached media is a property
   of a specific drive.  In our case, though, the listening port is a property
   of the TDI card, not of any given serial line.  As ATTACH ATCD is equivalent
   to ATTACH ATCD0, the port would, by default, be attached to the first channel
   and be reported there in a SHOW ATCD command.

   To preserve the logical picture, we attach the port to the Telnet poll unit,
   which is normally disabled to inhibit its display.  Attaching to a disabled
   unit is not allowed, so we first enable the unit, then attach it, then
   disable it again.  Attachment is reported by the "atc_show_status" routine.

   A direct attach to the poll unit is allowed only when restoring a previously
   saved session via the RESTORE command.
*/

static t_stat atcd_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status;

if (atcd_dev.flags & DEV_DIAG)                              /* if the TDI is in diagnostic mode */
    return SCPE_NOFNC;                                      /*   then the command is not allowed */

if (uptr != line_unit                                       /* if we're not attaching unit 0 */
  && (uptr != &poll_unit || !(sim_switches & SIM_SW_REST))) /*   and not we're not restoring the poll unit */
    return SCPE_NOATT;                                      /*     then the unit specified is not attachable */

poll_unit.flags &= ~UNIT_DIS;                               /* enable the poll unit */
status = tmxr_attach (&atcd_mdsc, &poll_unit, cptr);        /*   and attach it to the specified listening port */
poll_unit.flags |= UNIT_DIS;                                /*     and then disable it again */

return status;
}


/* Detach the TDI.

   Normally, this routine is called by the DETACH ATCD command, which is
   equivalent to DETACH ATCD0.  However, it may be called with other units in
   three cases.

   A DETACH ALL command will call us for unit 16 (the poll unit) if it is
   attached.  A RESTORE command also will call us for unit 16 if it is attached.
   In the latter case, the terminal channels will have already been rescheduled
   as appropriate, so canceling them is skipped.  Also, during simulator
   shutdown, we will be called for units 0-15 (detach_all in scp.c calls the
   detach routines of all units that do NOT have UNIT_ATTABLE), as well as for
   unit 16 if it is attached.  In all cases, it is imperative that we not reject
   the request for unit 16; otherwise any remaining device detaches will not be
   performed.
*/

static t_stat atcd_detach (UNIT *uptr)
{
uint32 channel;
t_stat status = SCPE_OK;

if (uptr == line_unit || uptr == &poll_unit) {                  /* if we're detaching the base unit or poll unit */
    status = tmxr_detach (&atcd_mdsc, &poll_unit);              /*   then detach the listening port */

    if ((sim_switches & SIM_SW_REST) == 0)                      /* if this is not a RESTORE call */
        for (channel = 0; channel < TERM_COUNT; channel++) {    /*   then for each terminal channel */
            atcd_ldsc [channel].rcve = FALSE;                   /*     disable reception */
            sim_cancel (&line_unit [channel]);                  /*       and cancel any transfer in progress */
            }
    }

return status;
}



/* ATC local utility routines */



/* Request a TDI interrupt.

   The data flag and interrupt request flip-flops are set.  If the interrupt
   mask permits, the interrupt request is passed to the IOP.
*/

static void tdi_set_interrupt (void)
{
tdi_data_flag = SET;                                    /* set the data flag */

atcd_dib.interrupt_request = SET;                       /* request an interrupt */

if (tdi_interrupt_mask)                                 /* if the interrupt mask is satisfied */
    iop_assert_INTREQ (&atcd_dib);                      /*   then assert the INTREQ signal to the IOP */

return;
}


/* TDI master reset.

   A master reset is generated either by an IORESET signal or a programmed
   master reset (CIO bit 0 set).  It clears any pending or active interrupt,
   sets the interrupt mask, clears the status word and data flag, and resets all
   channels to their initial, unconfigured state.


   Implementation notes:

    1. In hardware, a master reset sets the Initialize flip-flop.  This causes a
       direct clear of the recirculating memory window registers, thereby
       clearing each channel's buffer, parameter, and status values as they
       pass through the window.  The flip-flop is cleared when a control word is
       sent with the master clear bit (CIO bit 0) cleared.  A full recirculation
       takes 69.44 microseconds, so the CPU must allow at least this time for
       each channel to pass through the window to ensure that all memory
       locations are reset.  In simulation, the clear occurs "instantaneously."
*/

static void tdi_master_reset (void)
{
uint32 chan;

atcd_dib.interrupt_request = CLEAR;                     /* clear any current */
atcd_dib.interrupt_active  = CLEAR;                     /*   interrupt request */

tdi_interrupt_mask = SET;                               /* set the interrupt mask */

tdi_status_word = 0;                                    /* clear the status word */
tdi_data_flag   = CLEAR;                                /*   and the data flag */

for (chan = FIRST_TERM; chan <= LAST_TERM; chan++) {    /* for each terminal channel */
    recv_buffer [chan] = 0;                             /*   clear the receive data buffer */
    recv_param  [chan] = 0;                             /*     and parameter */
    recv_status [chan] = 0;                             /*       and status */

    send_buffer [chan] = 0;                             /* also clear the send data buffer */
    send_param  [chan] = 0;                             /*   and parameter */
    send_status [chan] = 0;                             /*     and status */

    sim_cancel (&line_unit [chan]);                     /* cancel any transfer in progress */
    }

for (chan = FIRST_AUX; chan <= LAST_AUX; chan++) {      /* for each auxiliary channel */
    recv_buffer [chan] = 0;                             /*   clear the receive data buffer */
    recv_param  [chan] = 0;                             /*     and parameter */
    recv_status [chan] = 0;                             /*       and status */
    }

return;
}


/* TCI master reset.

   A master reset is generated either by an IORESET signal or a programmed
   master reset (CIO bit 0 set).  It clears any pending or active interrupt,
   sets the interrupt mask, clears the control word and channel counter, and
   resets all channels to their initial, unconfigured state.


   Implementation notes:

    1. In hardware, a master reset sets the Status Clear flip-flop.  This causes
       a direct clear of the Control Word Holding Register and enables writing
       into each location of the addressable latches and state RAM.  The
       flip-flop is reset automatically when the channel counter rolls over.
       This takes approximately 12 microseconds, so the CPU must allow at least
       this time before sending new control information.  In simulation, the
       master reset occurs "instantaneously."

    2. In hardware, the C2 and C1 control line outputs are cleared by a master
       clear.  In simulation, we also clear the S2 and S1 status line input
       values.  This is OK, because they will be reestablished at the next poll
       service entry.
*/


static void tci_master_reset (void)
{
uint32 chan;

atcc_dib.interrupt_request = CLEAR;                     /* clear any current */
atcc_dib.interrupt_active  = CLEAR;                     /*   interrupt request */

tci_interrupt_mask = SET;                               /* set the interrupt mask */

tci_control_word = 0;                                   /* clear the control word */
tci_cntr = 0;                                           /*   and the channel counter */

for (chan = FIRST_TERM; chan <= LAST_TERM; chan++) {    /* for each terminal channel */
    cntl_status [chan] = 0;                             /*   clear all serial line values */
    cntl_param  [chan] = 0;                             /*     and the parameter RAM */
    }

return;
}


/* Multiplexer channel service.

   The channel service routine runs only when there are characters to read or
   write.  It is scheduled either at a realistic rate corresponding to the
   programmed baud rate of the channel to be serviced, or at a somewhat faster
   optimized rate.  It is entered when a channel buffer is ready for output or
   when the poll routine determines that there are characters ready for input.

   On entry, the receive channel buffer is checked for a character.  If one is
   not already present, then the terminal multiplexer library is called to
   retrieve the waiting character.  If a valid character is now available, it is
   processed.  If the receive channel has its "diagnose" bit set, the character
   is also passed to the auxiliary channels.

   The send channel buffer is then checked for a character to output.  If one is
   present, then if it is an all-mark (sync) character, it is discarded, as the
   receiver would never see it.  Otherwise, if the TDI is in diagnostic mode,
   then the character is looped back to the associated receive channel by
   storing it in that channel's receive buffer and then recursively calling the
   routine for that channel.

   If the TDI is in terminal mode, then if the channel flag is set for local
   ENQ/ACK handshaking, and the character is an ENQ, it is discarded, an ACK is
   stored in the channel's receive buffer, and its reception is scheduled.
   Otherwise, the character is processed and then transmitted either to the
   simulation console (if output is to channel 0) or to the terminal multiplexer
   library for output via Telnet or a serial port on the host machine.  If the
   channel has its "diagnose" bit set, the character is also passed to the
   auxiliary channels.

   If the data flag is clear, the indicated receive and send channels are
   checked for completion flags.  If either is set, an interrupt is requested.


   Implementation notes:

    1. Calling "tmxr_getc_ln" for channel 0 is OK, as reception is disabled by
       default and therefore will return 0.

    2. The send channel buffer will always be non-zero if a character is present
       (even a NUL) because the data word will have DDS_IS_SEND set.

       The receive buffer will always be non-zero if a character is present
       (even a NUL) because characters from the console will have SCPE_KFLAG
       set, characters from the terminal multiplexer library will have
       TMXR_VALID set, and characters looped back from sending will have
       DDS_IS_SEND set.

    3. Reception of a loopback character is performed immediately because the
       reception occurs concurrently with transmission.  Reception of a locally
       generated ACK is scheduled with a one-character delay to reflect the
       remote device transmission delay.

    4. If storing an ACK locally overwrites a character already present but not
       yet processed, then the receive routine will set the character lost flag.

    5. Both TMXR_VALID and SCPE_KFLAG are set on internally generated ACKs only
       so that a debug trace will record the generation correctly.

    6. The console library "sim_putchar_s" routine and the terminal multiplexer
       library "tmxr_putc_ln" routine return SCPE_STALL if the Telnet output
       buffer is full.  In this case, transmission is rescheduled with a delay
       to allow the buffer to drain.

       They also return SCPE_LOST if the line has been dropped on the remote
       end.  We ignore the error here to allow the simulation to continue while
       ignoring the output.

    7. The receive/send completion flag (buffer flag) will not set unless the
       interrupt enable flag for that channel is also set.  If enable is not
       set, the completion indication will be lost.
*/

static t_stat line_service (UNIT *uptr)
{
const  int32 channel = (int32) (uptr - line_unit);          /* the channel number */
const  int32 alt_channel = channel ^ 1;                     /* alternate channel number for diagnostic mode */
const  t_bool loopback = (atcd_dev.flags & DEV_DIAG) != 0;  /* TRUE if device is set for diagnostic mode */
int32  recv_data, send_data, char_data, cvtd_data;
t_stat result = SCPE_OK;

dprintf (atcd_dev, DEB_SERV, "Channel %d service entered\n",
         channel);


/* Reception service */

recv_data = recv_buffer [channel];                          /* get the current buffer character */

if (recv_data == 0)                                         /* if there's none present */
    recv_data = tmxr_getc_ln (&atcd_ldsc [channel]);        /*   then see if there's a character ready via Telnet */

if (recv_data & ~DDR_DATA_MASK) {                           /* if we now have a valid character */
    receive (channel, recv_data, loopback);                 /*   then process the reception */

    if (recv_param [channel] & DPI_DIAGNOSE)                /* if a diagnosis is requested */
        diagnose (recv_param [channel], recv_data);         /*   then route the data to the auxiliary channels */
    }

/* Transmission service */

if (send_buffer [channel]) {                                /* if data is available to send */
    send_data = DDS_DATA (send_buffer [channel]);           /*   then pick up the data and stop bits */
    char_data = send_data & ASCII_MASK;                     /*     and also the ASCII character value */

    if (send_status [channel] & DST_COMPLETE) {             /* if the last completion hasn't been acknowledged */
        send_status [channel] |= DST_CHAR_LOST;             /*   then indicate an overrun condition */

        dprintf (atcd_dev, DEB_CSRW, "Channel %d send data overrun\n",
                 channel);
        }

    if ((send_buffer [channel] & DDS_MARK) == DDS_MARK) {   /* if it's an all-mark character */
        send_buffer [channel] = 0;                          /*   then the receiver won't see it */

        if (send_param [channel] & DPI_ENABLE_IRQ)          /* if this channel is enabled to interrupt */
            send_status [channel] |= DST_COMPLETE;          /*   then set the completion flag */

        dprintf (atcd_dev, DEB_XFER, (loopback
                                       ? "Channel %d sync character sent to channel %d\n"
                                       : "Channel %d sync character sent\n"),
                 channel, alt_channel);
        }

    else if (loopback) {                                    /* otherwise if the device is in loopback mode */
        if (send_param [channel] & DPI_DIAGNOSE)            /*   then if a diagnosis is requested */
            diagnose (send_param [channel], send_data);     /*     then route the data to the auxiliary channels */

        if ((send_buffer [channel] & DDR_DATA_MASK) == 0)       /* if all bits are clear */
            recv_buffer [alt_channel] = SCPE_BREAK;             /*   then it will be seen as a BREAK */
        else                                                    /* otherwise a character will be received */
            recv_buffer [alt_channel] = send_buffer [channel];  /*   so store it in the buffer */

        send_buffer [channel] = 0;                          /* clear the send buffer */

        if (send_param [channel] & DPI_ENABLE_IRQ)          /* if this channel is enabled to interrupt */
            send_status [channel] |= DST_COMPLETE;          /*   then set the completion flag */

        dprintf (atcd_dev, DEB_XFER, "Channel %d character %s sent to channel %d\n",
                 channel, fmt_char (char_data), alt_channel);

        line_service (&line_unit [alt_channel]);            /* receive the character on the alternate channel */
        }

    else if (char_data == ENQ && uptr->flags & UNIT_LOCALACK) { /* otherwise if it's an ENQ and local reply is enabled */
        recv_buffer [channel] = GEN_ACK;                        /*   then "receive" an ACK on the channel */

        send_buffer [channel] = 0;                              /* discard the ENQ */

        if (send_param [channel] & DPI_ENABLE_IRQ)              /* if this channel is enabled to interrupt */
            send_status [channel] |= DST_COMPLETE;              /*   then set the completion flag */

        dprintf (atcd_dev, DEB_XFER, "Channel %d character ENQ absorbed internally\n",
                 channel);

        activate_unit (uptr, Receive);                          /* schedule the reception */
        }

    else {                                                      /* otherwise it's a normal character */
        cvtd_data = sim_tt_outcvt (LOWER_BYTE (send_data),      /*   so convert it as directed */
                                   TT_GET_MODE (uptr->flags));  /*     by the output mode flag */

        if (cvtd_data >= 0)                                     /* if the converted character is printable */
            if (channel == 0)                                   /*   then if we are writing to channel 0 */
                result = sim_putchar_s (cvtd_data);             /*     then output it to the simulation console */
            else                                                /*   otherwise */
                result = tmxr_putc_ln (&atcd_ldsc [channel],    /*     output it to the multiplexer line */
                                       cvtd_data);

        if (result == SCPE_STALL) {                             /* if the buffer is full */
            activate_unit (uptr, Stall);                        /*   then retry the output a while later */
            result = SCPE_OK;                                   /*     and return OK to continue */
            }

        else if (result == SCPE_OK || result == SCPE_LOST) {    /* otherwise if the character is queued to transmit */
            tmxr_poll_tx (&atcd_mdsc);                          /*   then send (or ignore) it */

            if (DPRINTING (atcd_dev, DEB_XFER))
                if (result == SCPE_LOST)
                    hp_debug (&atcd_dev, DEB_XFER, "Channel %d character %s discarded by connection loss\n",
                              channel, fmt_char (char_data));

                else if (cvtd_data >= 0)
                    hp_debug (&atcd_dev, DEB_XFER, "Channel %d character %s sent\n",
                              channel, fmt_char (cvtd_data));

                else
                    hp_debug (&atcd_dev, DEB_XFER, "Channel %d character %s discarded by output filter\n",
                              channel, fmt_char (char_data));

            if (send_param [channel] & DPI_DIAGNOSE)            /* if a diagnosis is requested */
                diagnose (send_param [channel], send_data);     /*   then route the data to the auxiliary channels */

            send_buffer [channel] = 0;                          /* clear the buffer */

            if (send_param [channel] & DPI_ENABLE_IRQ)          /* if this channel is enabled to interrupt */
                send_status [channel] |= DST_COMPLETE;          /*   then set the completion flag */

            result = SCPE_OK;                                   /* return OK in case the connection was lost */
            }
        }
    }


if (tdi_data_flag == CLEAR)                             /* if an interrupt is not currently pending */
    scan_channels (channel);                            /*   then scan the channels for completion flags */

return result;                                          /* return the result of the service */
}


/* Multiplexer poll service.

   The poll service routine is used to poll for Telnet connections and incoming
   characters.  It also polls the simulation console for channel 0.  Polling
   starts at simulator startup or when the TDI is enabled and stops when it is
   disabled.


   Implementation notes:

    1. The poll service routine may be entered with the TCI either enabled or
       disabled.  It will not be entered if the TDI is disabled, as it may be
       disabled only when it is detached from a listening port.

    2. If a character is received on the simulation console, we must call the
       channel 0 line service directly.  This is necessary because the poll time
       may be shorter than the channel service time, and as the console provides
       no buffering, a second character received before the channel service had
       been entered would be lost.
*/

static t_stat poll_service (UNIT *uptr)
{
int32 chan, line_state;
t_stat status = SCPE_OK;

dprintf (atcd_dev, DEB_PSERV, "Poll service entered\n");

if ((atcc_dev.flags & DEV_DIS) == 0)
    dprintf (atcc_dev, DEB_PSERV, "Poll service entered\n");

if ((atcd_dev.flags & DEV_DIAG) == 0) {                 /* if we're not in diagnostic mode */
    chan = tmxr_poll_conn (&atcd_mdsc);                 /*   then check for a new multiplex connection */

    if (chan != -1) {                                   /* if a new connection was established */
        atcd_ldsc [chan].rcve = TRUE;                   /*   then enable the channel to receive */

        dprintf (atcc_dev, DEB_XFER, "Channel %d connected\n",
                 chan);
        }
    }

tmxr_poll_rx (&atcd_mdsc);                              /* poll the multiplex connections for input */

if ((atcc_dev.flags & (DEV_DIAG | DEV_DIS)) == 0)       /* if we're not in diagnostic mode or are disabled */
    for (chan = FIRST_TERM; chan <= LAST_TERM; chan++)  /*   then scan the channels for line state changes */
        if (line_unit [chan].flags & UNIT_MODEM) {      /* if the channel is controlled by the TCI */
            tmxr_set_get_modem_bits (&atcd_ldsc [chan], /*   then get the current line state */
                                     0, 0, &line_state);

            if (line_state & TMXR_MDM_DCD)              /* if DCD is set */
                cntl_status [chan] |= DCD;              /*   then set the corresponding line flag */

            else {                                      /* otherwise DCD is clear */
                if (cntl_status [chan] & DCD)           /*   and a disconnect occurred if DCD was previously set */
                    dprintf (atcc_dev, DEB_XFER, "Channel %d disconnect dropped DCD and DSR\n",
                             chan);

                cntl_status [chan] &= ~DCD;             /* clear the corresponding flag */
                }

            if (line_state & TMXR_MDM_DSR)              /* if DSR is set */
                cntl_status [chan] |= DSR;              /*   then set the corresponding line flag */
            else                                        /* otherwise */
                cntl_status [chan] &= ~DSR;             /*   clear the flag */
            }

status = sim_poll_kbd ();                               /* poll the simulation console keyboard for input */

if (status >= SCPE_KFLAG) {                             /* if a character was present */
    recv_buffer [0] = (HP_WORD) status;                 /*   then save it for processing */
    status = SCPE_OK;                                   /*     and then clear the status */

    line_service (&line_unit [0]);                      /* run the system console's I/O service */
    }

for (chan = FIRST_TERM; chan <= LAST_TERM; chan++)      /* check each of the receive channels for available input */
    if (tmxr_rqln (&atcd_ldsc [chan]))                  /* if characters are available on this channel */
        activate_unit (&line_unit [chan], Receive);     /*   then activate the channel's I/O service */

if (cpu_is_calibrated)                                  /* if the process clock is calibrated */
    uptr->wait = sim_activate_time (cpu_pclk_uptr);     /*   then synchronize with it */
else                                                    /* otherwise */
    uptr->wait = sim_rtcn_calb (POLL_RATE, TMR_ATC);    /*   calibrate the poll timer independently */

sim_activate (uptr, uptr->wait);                        /* continue polling */

if (tci_scan)                                           /* if scanning is active */
    scan_status ();                                     /*   then check for line status changes */

return status;                                          /* return the service status */
}


/* Activate a channel unit.

   The specified unit is activated to receive or send a character.  The reason
   for the activation is specified by the "reason" parameter.  If the TDI is in
   real-time mode, the previously calculated service time is used to schedule
   the event.  Otherwise, the current value of the optimized timing delay is
   used.  If tracing is enabled, the activation is logged to the debug file.


   Implementation notes:

    1. The loopback time is the difference between the reception and
       transmission times, as the latter event has already occurred when we are
       called.
*/

static t_stat activate_unit (UNIT *uptr, ACTIVATOR reason)
{
const int32 channel = (int32) (uptr - line_unit);       /* the channel number */
int32 delay = 0;

if (atcd_dev.flags & (DEV_DIAG | DEV_REALTIME))         /* if either diagnostic or real-time mode is set */
    switch (reason) {                                   /*   then dispatch the REALTIME activation */

        case Receive:                                   /* reception event */
            delay = uptr->recv_time;                    /* schedule for the realistic reception time */
            break;

        case Send:                                      /* transmission event */
            delay = uptr->send_time;                    /* schedule for the realistic transmission time */
            break;

        case Loop:                                      /* diagnostic loopback reception event */
            delay = uptr->recv_time - uptr->send_time;  /* schedule the additional reception overhead */

            if (delay < 0)                              /* if the receive time is less than the send time */
                delay = 0;                              /*   then schedule the reception immediately */
            break;

        case Stall:                                     /* transmission stall event */
            delay = uptr->send_time / 10;               /* reschedule the transmission after a delay */
            break;
        }

else                                                    /* otherwise, we are in optimized timing mode */
    switch (reason) {                                   /*   so dispatch the FASTTIME activation */

        case Receive:                                   /* reception event */
        case Send:                                      /* transmission event */
            delay = fast_data_time;                     /* use the optimized timing value */
            break;

        case Loop:                                      /* diagnostic loopback reception event */
            delay = 1;                                  /* use a nominal delay */
            break;

        case Stall:                                     /* transmission stall event */
            delay = fast_data_time / 10;                /* reschedule the transmission after a delay */
            break;
        }

dprintf (atcd_dev, DEB_SERV, "Channel %d delay %d service scheduled\n",
         channel, delay);

return sim_activate (uptr, delay);                      /* activate the unit and return the activation status */
}


/* Calculate the service time.

   The realistic channel service time in event ticks per character is calculated
   from the encoded character size and baud rate in the supplied control word.
   The time consists of the transfer time plus a small overhead, which is
   different for receiving and sending.

   The character size field in the control word is generated by this equation:

     encoded_size = (bits_per_character - 1) AND 7

   That is, the encoded character size is the value expressed by the three
   least-significant bits of the count of the data and stop bits.  Therefore,
   the actual number of bits per character (including the start bit) is encoded
   as:

     Actual  Encoded
     ------  -------
        5       4
        6       5
        7       6
        8       7
        9       0
       10       1
       11       2
       12       3

   The baud rate field in the control word is generated by this equation:

                      14400
     encoded_rate = --------- - 1
                    baud_rate

   The transmission and overhead times are related to the recirculation of the
   multiplexer's internal memory, which contains the data, parameters, and
   status for each of the 16 send channels, 16 receive channels, and 5 auxiliary
   channels.  Data for a given channel can be accessed only once per
   recirculation, which takes 69.44 microseconds (1/14400 of a second).  The
   encoded rate plus one gives the number of recirculations corresponding to a
   single bit time; multiplying by the number of bits per character gives the
   number of recirculations to send or receive an entire character.

   All operations encounter two overhead delays.  First, an average of one-half
   of a recirculation must occur to align the memory with the channel of
   interest.  Second, a full recirculation is required after receiving or
   sending is complete before an interrupt may be generated.

   For receiving, there is an additional delay to right-justify the received
   character in the data accumulator.  The accumulator is a 12-bit shift
   register, with received data bits are shifted from left to right.  When the
   final bit is entered, the register must be shifted additionally until the
   first data bit is in the LSB (i.e., until the start bit is shifted out of the
   register).  One shift per recirculation is performed, and the number of
   additional shifts required is 12 + 1 - the number of bits per character.

   Justification begins immediately after the stop bit has been received, so the
   full set of recirculations for that bit are skipped in lieu of justification.
   Also, reception of the start bit is delayed by one-half of the bit time to
   improve noise immunity.

   Therefore, given R = encoded_rate + 1 and B = bits_per_character, the number
   of memory recirculations required for sending is:

        0.5  to align memory with the target channel (on average)
     + R * B to send the start, data, and stop bits
     +   1   to set the data flag to request an interrupt

   For example, at 2400 baud (encoded rate 5), a 10-bit character size, and
   69.44 microseconds per circulation, the service time would be:

       34.7 usec to align
     4166.7 usec to send the start, data, and stop bits
       69.4 usec to set the data flag
     ===========
     4270.8 usec from initiation to data flag

  The number of memory recirculations required for receiving is:

           0.5      to align memory with the target channel (on average)
     +    R / 2     to receive the start bit
     + R * (B - 1)  to receive the data and stop bits
     + (12 - B + 1) to right-justify the data
     +      1       to set the data flag to request an interrupt

   Using the same example as above, the service time would be:

       34.7 usec to align
      208.3 usec to receive the start bit
     3750.0 usec to receive the data and stop bits
      208.3 usec to right-justify the data
       69.4 usec to set the data flag
     ===========
     4270.7 usec from initiation to data flag


   Implementation notes:

    1. The multiplexer uses an 8-bit field to set the baud rate.  In practice,
       only the common rates (110, 150, 300, 600, 1200, 2400) will be used.
       Still, the real-time calculation must accommodate any valid setting, so a
       lookup table is infeasible.

    2. The receive calculation is simplified by noting that R / 2 + R * (B - 1)
       is equivalent to R * B - R / 2, so the send calculation may be reused.
       Note that the receive time may be less than the send time, typically when
       the baud rate is low, so that the time to send the stop bits is longer
       than the time to right-justify the reception.  This means that the
       "addition" of the receive overhead may actually be a subtraction.
*/

static uint32 service_time (HP_WORD control, ACTIVATOR reason)
{
const  double recirc_time = 69.44;                                  /* microseconds per memory recirculation */
const  uint32 recirc_per_bit = DPI_BAUD_RATE (control) + 1;         /* number of memory recirculations per bit */
const  uint32 char_size = bits_per_char [DPI_CHAR_SIZE (control)];  /* number of bits per character */
double usec_per_char;

usec_per_char = recirc_time *                           /* calculate the overhead for sending */
                  (char_size * recirc_per_bit + 1.5);

if (reason == Receive)                                  /* if we're receiving */
    usec_per_char += recirc_time *                      /*   then add the additional receiving overhead */
                       (12 - char_size + 1
                        - recirc_per_bit / 2.0);

return (uint32) (usec_per_char / USEC_PER_EVENT);       /* return the service time for indicated rate */
}


/* Store a word in the recirculating memory.

   A parameter or data word is stored in the recirculating memory for the
   channel indicated by the associated field of the "control" parameter.  If the
   channel number is out of range, the store is ignored.

   For receive and send parameters, the realistic service time is calculated and
   stored in the unit for use when a receive or send event is scheduled.  For
   send data, parity is calculated and added if specified by the channel's
   parameter, and the transmission event is scheduled.  For a receive parameter,
   the pad bits that would normally be added during right-justification after
   reception are calculated and stored in the unit.


   Implementation notes:

    1. Service times are not calculated or set for auxiliary channels because
       events are not scheduled on them (and so no units are allocated for
       them).

    2. Pad bits begin with the stop bit and continue until the character is
       right-justified in the receive buffer.  The calculation assumes one stop
       bit, but there is no way of ascertaining the actual number of stop bits
       from the parameter word.
*/

static void store (HP_WORD control, HP_WORD data)
{
const uint32 channel = DCN_CHAN (control);              /* current channel number */

if (data & DDS_IS_SEND)                                 /* if this is a send parameter or data */
    if (channel > LAST_TERM)                            /*   then report if the channel number is out of range */
        dprintf (atcd_dev, DEB_CSRW, "Send channel %u invalid\n",
                 channel);

    else if (data & DPI_IS_PARAM) {                     /* otherwise if this is a parameter store */
        send_param [channel] = data;                    /*   then save it */
        line_unit [channel].send_time =                 /*    and set the service time */
          service_time (data, Send);

        dprintf (atcd_dev, DEB_CSRW, "Channel %u send parameter %06o stored\n",
                 channel, data);
        }

    else {                                              /* otherwise this is a data store */
        if (send_param [channel] & DPI_ENABLE_PARITY)   /* if parity is enabled */
            data = data & ~DDS_PARITY                   /*   then replace the parity bit */
                     | SEND_PARITY (data);              /*     with the calculated value */

        send_buffer [channel] = data;                   /* store it in the buffer */

        dprintf (atcd_dev, DEB_CSRW, "Channel %u send data %06o stored\n",
                 channel, data);

        activate_unit (&line_unit [channel], Send);     /* schedule the transmission event */
        }

else                                                    /* otherwise this is a receive parameter */
    if (channel >= RECV_CHAN_COUNT)                     /* report if the channel number is out of range */
        dprintf (atcd_dev, DEB_CSRW, "Receive channel %u invalid\n",
                 channel);

    else if (data & DPI_IS_PARAM) {                     /* otherwise this is a parameter store */
        recv_param [channel] = data;                    /*   then save it */

        if (channel <= LAST_TERM) {                     /* if this is a terminal channel */
            line_unit [channel].recv_time =             /*   and not an auxiliary channel */
              service_time (data, Receive);             /*     then set the service time */

            line_unit [channel].stop_bits =             /* set the stop bits mask for reception */
              PAD_BITS (data);
            }

        dprintf (atcd_dev, DEB_CSRW, "Channel %u receive parameter %06o stored\n",
                 channel, data);
        }

    else                                                /* otherwise a data store to a receive channel is invalid */
        dprintf (atcd_dev, DEB_CSRW, "Channel %u receive output data word %06o invalid\n",
                 channel, data);
}


/* Process a character received from a channel.

   This routine is called to process received data on a channel, typically when
   a character exists in the channel's receive buffer, but also when a character
   is received on an auxiliary channel.  The "channel" parameter indicates the
   channel on which reception occurred, "data" is the (full) character data
   as received from the console or terminal multiplexer libraries, and
   "loopback" is TRUE if the data should be looped back to the alternate channel
   for diagnostic execution.

   On entry, the bits required to pad the character are obtained.  If a BREAK
   was detected, then break status is set, and the character is set to NUL,
   reflecting the all-space reception.  Otherwise, if a character is already
   present in the receive buffer, "character lost" status is set to indicate
   that it will be overwritten.

   If this is a loopback reception, and echo is enabled on the channel, the
   character is sent back to the alternate channel.  Otherwise, if this is a
   main and not auxiliary channel reception, the character is upshifted if the
   UNIT_CAPSLOCK flag is set.  If echo is enabled, the character is written back
   to the console or terminal multiplexer library line.  Finally, the completion
   flag is set if enabled.


   Implementation notes:

    1. The echo to a terminal multiplexer library line will return SCPE_LOST if
       the line has been dropped on the remote end.  We can ignore the error
       here, as the line drop will be picked up when the next input poll is
       performed.

       In addition, the SCPE_STALL returned for a full output buffer is also
       ignored, as there's no way of queuing echoed characters while waiting for
       the buffer to empty.
*/

static void receive (int32 channel, int32 data, t_bool loopback)
{
int32 recv_data, char_data, char_echo, pad;

recv_data = data & DDR_DATA_MASK;                       /* mask to just the character data */
char_data = recv_data & ASCII_MASK;                     /*   and to the equivalent ASCII character */

if (channel <= LAST_TERM)                               /* if this is a receive channel */
    pad = line_unit [channel].stop_bits;                /*   then set the stop-bit padding from the unit */
else                                                    /* otherwise it's an auxiliary channel */
    pad = PAD_BITS (recv_param [channel]);              /*   so calculate the padding */


if (data & SCPE_BREAK) {                                /* if a break was detected */
    recv_buffer [channel] = NUL;                        /*   then return a NUL character */
    recv_status [channel] |= DST_BREAK;                 /*     and set break reception status */

    dprintf (atcd_dev, DEB_XFER, "Channel %d break detected\n",
             channel);
    }

else {                                                  /* otherwise a normal character was received */
    if (recv_status [channel] & DST_COMPLETE) {         /* if a character is already pending */
        recv_status [channel] |= DST_CHAR_LOST;         /*   then the previous character will be lost */

        dprintf (atcd_dev, DEB_CSRW, "Channel %d receive data overrun\n",
                 channel);
        }

    recv_buffer [channel] = recv_data | pad;            /* save the character and padding in the buffer */

    if (loopback) {                                     /* if this channel has a loopback cable installed */
        if (recv_param [channel] & DPI_ENABLE_ECHO) {   /*   and the channel has echo enabled */
            recv_buffer [channel ^ 1] = data;           /*     then send the data back to the other channel */

            activate_unit (&line_unit [channel ^ 1], Loop); /* schedule the reception */

            dprintf (atcd_dev, DEB_XFER, "Channel %d character %s echoed to channel %d\n",
                     channel, fmt_char (char_data), channel ^ 1);
            }
        }

    else if (channel <= LAST_TERM) {                        /* otherwise if it's a receive channel */
        if (line_unit [channel].flags & UNIT_CAPSLOCK) {    /*   then if caps lock is down */
            recv_data = toupper (recv_data);                /*     then convert to upper case if lower */
            recv_buffer [channel] = recv_data | pad;        /*       and replace the character in the buffer */
            }

        if (recv_param [channel] & DPI_ENABLE_ECHO) {       /* if the channel has echo enabled */
            char_echo = sim_tt_outcvt (recv_data,           /*   then convert the character per the output mode */
                                       TT_GET_MODE (line_unit [channel].flags));

            if (char_echo >= 0) {                           /* if the converted character is valid for the mode */
                if (channel == 0)                           /*   then if this is for channel 0 */
                    sim_putchar (char_echo);                /*     then write it back to the simulation console */

                else {                                              /* otherwise */
                    tmxr_putc_ln (&atcd_ldsc [channel], char_echo); /*   write it to the multiplexer output line */
                    tmxr_poll_tx (&atcd_mdsc);                      /*     and poll to transmit it now */
                    }

                dprintf (atcd_dev, DEB_XFER, ("Channel %d character %s echoed\n"),
                         channel, fmt_char (char_echo));
                }

            else                                            /* otherwise the echo character was discarded */
                dprintf (atcd_dev, DEB_XFER, "Channel %d character %s echo discarded by output filter\n",
                         channel, fmt_char (char_data));
            }
        }
    }

if (recv_param [channel] & DPI_ENABLE_IRQ)              /* if the channel is enabled to interrupt */
    recv_status [channel] |= DST_COMPLETE;              /*   then set the completion flag */

dprintf (atcd_dev, DEB_XFER, "Channel %d character %s %s\n",
         channel, fmt_char (char_data),
         (data == GEN_ACK ? "generated internally" : "received"));

return;
}


/* Check for a character received on an auxiliary channel.

   If a send or receive channel has its "diagnose" bit set, then this routine is
   called to check if any of the auxiliary channels would receive the character
   too.  If one or more would, then the "receive" routine is called to store the
   character in the appropriate buffer.

   The diagnosis mode is typically used to speed-sense a receive channel.
   In hardware, reception on a given channel is simultaneously received on the
   five auxiliary channels, with each channel set for a different baud rate.
   When a specific character (e.g., CR) is sent, only the channel with the
   correct baud rate setting will receive the intended character.  By
   determining which channel received the correct data, the baud rate of the
   sending terminal may be obtained.

   In simulation, a main channel will receive a character regardless of the baud
   rate configuration.  Therefore, an auxiliary channel will receive the same
   character only if it is configured for the same baud rate and character size.
*/

static void diagnose (HP_WORD control, int32 data)
{
const HP_WORD config = control & DPI_CHAR_CONFIG;           /* main channel character size and baud rate */
int32 channel;

for (channel = FIRST_AUX; channel <= LAST_AUX; channel++)   /* scan the auxiliary channels */
    if ((recv_param [channel] & DPI_CHAR_CONFIG) == config) /* if the character configurations match */
        receive (channel, data, FALSE);                     /*   then receive the data on this channel */

return;
}


/* Scan the channels for a transfer completion interrupt.

   If the multiplexer data flag is not set, this routine is called to scan the
   channels for completion flags.  If the "channel" parameter value is SCAN_ALL,
   then all of the channels are checked.  Otherwise, only the specified channel
   is checked.

   If a channel has its completion flag set, the multiplexer data and status
   words are set for return to the CPU, the data flag is set, and an interrupt
   is requested.  The channel requesting the interrupt is contained in the
   status word.

   In hardware, the recirculating buffer consists of the sixteen receive
   channels, then the sixteen send channels, and then the five auxiliary
   channels.  The completion flags are checked in this order during the
   recirculation after a completion flag is set.  If the scan has been inhibited
   by the data flag, it will commence with the channel currently in the
   recirculation window at the time the flag was cleared and then continue in
   the order indicated.

   In simulation, the scan is always initiated as though at the beginning of a
   recirculation.


   Implementation notes:

    1. After a send completion, the data word contains all ones (stop bits).
*/

static void scan_channels (int32 channel)
{
int32 chan, first_chan, last_chan;

if (channel == SCAN_ALL) {                              /* if all channels are to be scanned */
    first_chan = FIRST_TERM;                            /*   then set the loop limits */
    last_chan  = LAST_TERM;                             /*     to the full range of channels */
    }

else                                                    /* otherwise scan just the channel indicated */
    first_chan = last_chan = channel;                   /*   plus the auxiliary channels if requested */

for (chan = first_chan; chan <= last_chan; chan++) {    /* scan the receive channels */
    if (recv_status [chan] & DST_COMPLETE) {            /* if this channel's completion flag is set */
        tdi_read_word = DDR_DATA (recv_buffer [chan])   /*   then form the input data word */
                          | DDR_CHAN (chan)             /*     from the character, channel, and parity */
                          | RECV_PARITY (recv_buffer [chan]);

        tdi_status_word = recv_status [chan]            /* form the partial status word */
                            | DST_CHAN (chan);

        recv_buffer [chan] = 0;                         /* clear the receive buffer */
        recv_status [chan] = 0;                         /*   and the channel status */

        dprintf (atcd_dev, DEB_CSRW, "Channel %d receive interrupt requested\n",
                 chan);

        tdi_set_interrupt ();                           /* set the data flag and request an interrupt */
        return;                                         /*   and terminate scanning */
        }
    }

for (chan = first_chan; chan <= last_chan; chan++) {    /* scan the send channels */
    if (send_status [chan] & DST_COMPLETE) {            /* if this channel's completion flag is set */
        tdi_read_word = DDR_DATA_MASK                   /*   then form the input data word from the */
                          | DDR_CHAN (chan);            /*     data input buffer and the channel number */

        tdi_status_word = send_status [chan]            /* form the partial status word */
                            | DST_CHAN (chan)
                            | DST_SEND_IRQ;

        send_status [chan] = 0;                         /* clear the channel status */

        dprintf (atcd_dev, DEB_CSRW, "Channel %d send interrupt requested\n",
                 chan);

        tdi_set_interrupt ();                           /* set the data flag and request an interrupt */
        return;                                         /*   and terminate scanning */
        }
    }

if (channel == SCAN_ALL                                     /* if we're scanning all channels */
  || send_param [channel] & DPI_DIAGNOSE                    /*   or the indicated channel is diagnosing */
  || recv_param [channel] & DPI_DIAGNOSE)                   /*     its transmission or reception */
    for (chan = FIRST_AUX; chan <= LAST_AUX; chan++) {      /*       then scan the auxiliary channels */
        if (recv_status [chan] & DST_COMPLETE) {            /* if this channel's completion flag is set */
            tdi_read_word = DDR_DATA (recv_buffer [chan])   /*   then form the input data word */
                              | DDR_CHAN (chan)             /*     from the character, channel, and parity */
                              | RECV_PARITY (recv_buffer [chan]);

            tdi_status_word = recv_status [chan]            /* form the partial status word */
                                | DST_CHAN (chan)
                                | DST_DIAGNOSE;

            recv_buffer [chan] = 0;                         /* clear the receive buffer */
            recv_status [chan] = 0;                         /*   and the channel status */

            dprintf (atcd_dev, DEB_CSRW, "Channel %d receive interrupt requested\n",
                     chan);

            tdi_set_interrupt ();                           /* set the data flag and request an interrupt */
            return;                                         /*   and terminate scanning */
            }
        }

return;                                                     /* no channel has completed */
}


/* Check for a control interrupt.

   If the scan flag is clear, then return the interrupt status bits for the
   channel indicated by the current control counter value.  Otherwise, scan all
   of the control channels, starting with the current counter, to check for a
   status mismatch.  This occurs when either of the incoming status bits does
   not match the stored status, and the corresponding mismatch detection is
   enabled.  If an enabled mismatch is found, request an interrupt from the CPU,
   clear the scan flag, and return the interrupt status bits with the counter
   pointing at the interrupting channel.
*/

static HP_WORD scan_status (void)
{
uint32  chan_count;
HP_WORD interrupts;

if (tci_scan)                                               /* if the control interface is scanning */
    chan_count = TERM_COUNT;                                /*   then look at all of the channels */
else                                                        /* otherwise */
    chan_count = 1;                                         /*   look at only the current channel */

while (chan_count > 0) {                                    /* scan the control channels */
    interrupts = CST_IX (CCN_ESX (cntl_param [tci_cntr])    /* check for an enabled status mismatch */
                   & (cntl_param [tci_cntr] ^ cntl_status [tci_cntr]));

    if (tci_scan) {                                         /* if the interface is scanning */
        if (interrupts) {                                   /*   and a mismatch was found */
            atcc_dib.interrupt_request = SET;               /*     then request an interrupt */

            if (tci_interrupt_mask)                         /* if the interrupt mask is satisfied */
                iop_assert_INTREQ (&atcc_dib);              /*   then assert the INTREQ signal */

            tci_scan = CLEAR;                               /* stop the scan at the current channel */

            dprintf (atcc_dev, DEB_CSRW, "Channel %u interrupt requested\n",
                                         tci_cntr);
            break;
            }

        tci_cntr = (tci_cntr + 1) % TERM_COUNT;             /* set the counter to the next channel in sequence */
        }

    chan_count = chan_count - 1;                            /* drop the count of channels to check */
    }

return interrupts;                                          /* return the interrupt status bits */
}
