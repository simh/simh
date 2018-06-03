/* hp2100_mux.c: HP 2100 12920A Asynchronous Multiplexer Interface simulator

   Copyright (c) 2002-2016, Robert M. Supnik
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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   MUX,MUXL,MUXC        12920A Asynchronous Multiplexer Interface

   01-May-18    JDB     Removed ioCRS counter, as consecutive ioCRS calls are no longer made
   28-Apr-18    JDB     Fixed output completion IRQ when port is not connected
   03-Aug-17    JDB     Control card device renamed from MUXM to MUXC
                        MUXC now enabled/disabled independently of MUX and MUXL
                        Modified to use the "odd_parity" array in hp2100_sys.c
   15-Mar-17    JDB     Trace flags are now global
                        Changed DEBUG_PRI calls to tprintfs
   10-Mar-17    JDB     Added IOBUS to the debug table
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   29-Jun-15    JDB     Corrected typo in RTS macro definition
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Jan-13    MP      Added DEV_MUX and additional DEVICE field values
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Removed DEV_NET to allow restoration of listening port
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   25-Nov-08    JDB     Revised for new multiplexer library SHOW routines
   09-Oct-08    JDB     "muxl_unit" defined one too many units (17 instead of 16)
   10-Sep-08    JDB     SHOW MUX CONN/STAT with SET MUX DIAG is no longer disallowed
   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   27-Aug-08    JDB     Added LINEORDER support
   12-Aug-08    JDB     Added BREAK deferral to allow RTE break-mode to work
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   16-Apr-08    JDB     Sync mux poll with console poll for idle compatibility
   06-Mar-07    JDB     Corrected "mux_sta" size from 16 to 21 elements
                        Fixed "muxc_reset" to clear lines 16-20
   26-Feb-07    JDB     Added debug printouts
                        Fixed control card OTx to set current channel number
                        Fixed to set "muxl_ibuf" in response to a transmit interrupt
                        Changed "mux_xbuf", "mux_rbuf" declarations from 8 to 16 bits
                        Fixed to set "mux_rchp" when a line break is received
                        Fixed incorrect "odd_par" table values
                        Reversed test in "RCV_PAR" to return "LIL_PAR" on odd parity
                        Fixed mux reset (ioCRS) to clear port parameters
                        Fixed to use PUT_DCH instead of PUT_CCH for data channel status
   10-Feb-07    JDB     Added DIAG/TERM modifiers to implement diagnostic mode
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   02-Jun-06    JDB     Fixed compiler warning for mux_ldsc init
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Jun-05    RMS     Added SET MUXLn DISCONNECT
   07-Oct-04    JDB     Allow enable/disable from any device
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   05-Jan-04    RMS     Revised for tmxr library changes
   21-Dec-03    RMS     Added invalid character screening for TSB (from Mike Gemeny)
   09-May-03    RMS     Added network device flag
   01-Nov-02    RMS     Added 7B/8B support
   22-Aug-02    RMS     Updated for changes to sim_tmxr

   Reference:
   - 12920A Asynchronous Multiplexer Interface Kits Operating and Service Manual
        (12920-90001, Oct-1972)


   The 12920A was a 16-channel asynchronous terminal multiplexer.  It supported
   direct-connected terminals as well as modems at speeds up to 2400 baud.  It
   was the primary terminal multiplexer for the HP 2000 series of Time-Shared
   BASIC systems.

   The multiplexer was implemented as a three-card set consisting of a lower
   data card, an upper data card, and a modem control card.  Under simulation,
   these are implemented by three devices:

     MUXL   lower data card (lines)
     MUX    upper data card (scanner)
     MUXC   control card (modem control)

   The lower and upper data cards must be in adjacent I/O slots.  The control
   card may be placed in any slot, although in practice it was placed in the
   slot above the upper data card, so that all three cards were physically
   together.

   The 12920A supported one or two control cards (two cards were used with
   801-type automatic dialers).  Under simulation, only one control card is
   supported.

   The multiplexer responds to I/O instructions as follows:

   Upper Data Card output word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - |  channel number   | -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Upper Data Card input word format (LIA, LIB, MIA, and MIB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S |  channel number   | -   -   -   -   -   - | D | B | L | R |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = Seeking
     D = Diagnose
     B = Break status
     L = Character lost
     R = Receive/send (0/1) character interrupt


   Lower Data Card output control word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | R | I | E | D | char size |           baud rate           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = Receive/send (0/1) configuration
     I = Enable interrupt
     E = Echo (receive)/parity (send)
     D = Diagnose

   Character size:

     The three least-significant bits of the sum of the data, parity, and stop
     bits.  For example, 7E1 is 1001, so 001 is coded.

   Baud rate:

     The value (14400 / device bit rate) - 1.  For example, 2400 baud is 005.


   Lower Data Card output data word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 1 | -   - | S |               transmit data               |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = Sync bit

   Transmit data:

     Right-justified with leading one bits.


   Lower Data Card input word format (LIA, LIB, MIA, and MIB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | P |      channel      |             receive data              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     P = Computed parity

   Receive data:

     Right-justified with leading one bits


   Control Card output word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | U |channel number | -   - |EC2|EC1|C2 |C1 |ES2|ES1|SS2|SS1|
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Control Card input word format (LIA, LIB, MIA, and MIB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1 |channel number |I2 |I1 | 0   0   0   0 |ES2|ES1|S2 |S1 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S   = Scan
     U   = Update
     ECx = Enable command bit x
     Cx  = Command bit x
     ESx = Enable status bit x
     Sx  = Status bit x
     SSx = Stored status bit x
     Ix  = Interrupt bit x

   The control card provides two serial control outputs and two serial status
   inputs for each of the 16 channels.  The card connects to the Request to Send
   (CA) and Data Terminal Ready (CD) control lines and the Data Carrier Detect
   (CF) and Data Set Ready (CC) status lines.  Addressable latches hold the
   control line values and assert them continuously to the 16 channels.  In
   addition, a 16-word by 4-bit RAM holds the expected state for each channel's
   status lines and the corresponding interrupt enable bits to provide
   notification if those lines change.


   Implementation notes:

    1. If a BREAK is detected during an input poll, and we are not in diagnostic
       mode, we defer recognition until either a character is output or a second
       successive input poll occurs.  This is necessary for RTE break-mode
       operation.  Without this deferral, a BREAK during output would be ignored
       by the RTE driver, making it impossible to stop a long listing.

       The problem is due to timing differences between simulated and real time.
       The RTE multiplexer driver is a privileged driver.  Privileged drivers
       bypass RTE to provide rapid interrupt handling.  To inform RTE that an
       operation is complete, e.g., that a line has been written, the interrupt
       section of the driver sets a device timeout of one clock tick (10
       milliseconds).  When that timeout occurs, RTE is entered normally to
       complete the I/O transaction.  While the completion timeout is pending,
       the driver ignores any further interrupts from the multiplexer line.

       The maximum communication rate for the multiplexer is 2400 baud, or
       approximately 4.2 milliseconds per character transferred.  A typical line
       of 20 characters would therefore take ~85 milliseconds, plus the 10
       millisecond completion timeout, or about 95 milliseconds total.  BREAK
       recognition would be ignored for roughly 10% of that time.  At lower baud
       rates, recognition would be ignored for a correspondingly smaller
       percentage of the time.

       However, SIMH uses an optimized timing of 500 instructions per character
       transfer, rather than the ~6600 instructions that a character transfer
       should take, and so a typical 20-character line will take about 11,000
       instructions.  On the other hand, the clock tick is calibrated to real
       time, and 10 milliseconds of real time takes about 420,000 instructions
       on a 2.0 GHz PC.  To be recognized, then, the BREAK key must be pressed
       in a window that is open for about 2.5% of the time.  Therefore, the
       BREAK key will be ignored about 97.5% of the time, and RTE break-mode
       effectively will not work.

       Deferring BREAK recognition until the next character is output ensures
       that the BREAK interrupt will be accepted (the simulator delivers input
       interrupts before output interrupts, so the BREAK interrupt arrives
       before the output character transmit interrupt).  If an output operation
       is not in progress, then the BREAK will be recognized at the next input
       poll.

    2. In simulation, establishing a port connection asserts DSR to the control
       card.  If the port is configured as a dataset connection (SET MUXLn
       DATASET), DCD is also asserted.  Disconnecting denies DSR and DCD.  The
       control card responds to DTR denying by dropping the port connection.
       The RTS setting has no effect.

    3. When a Bell 103 dataset answers a call, it asserts DSR first.  After the
       handshake with the remote dataset completes, DCD asserts, typically
       between 1.3 and 3.6 seconds later.  Similarly, when the remote dataset
       terminates the call by sending a long (1.5 second) space, the local
       dataset drops DSR first, followed by DCD after approximately 30
       milliseconds.  The dataset simulation does not model these delays; DSR
       and DCD transition up and down together.  This implies that the control
       card software driver will see only one interrupt for each transition pair
       instead of the expected two (presuming both DSR and DCD are enabled to
       interrupt).
*/



#include <ctype.h>

#include "hp2100_defs.h"

#include "sim_tmxr.h"



/* Program limits */

#define TERM_COUNT          16                              /* number of terminal channels */
#define AUX_COUNT           5                               /* number of auxiliary channels */

#define RECV_CHAN_COUNT     (TERM_COUNT + AUX_COUNT)        /* number of receive channels */
#define SEND_CHAN_COUNT     TERM_COUNT                      /* number of send channels */
#define UNIT_COUNT          TERM_COUNT                      /* number of units */

#define FIRST_TERM          0                               /* first terminal index */
#define LAST_TERM           (FIRST_TERM + TERM_COUNT - 1)   /* last terminal index */
#define FIRST_AUX           TERM_COUNT                      /* first auxiliary index */
#define LAST_AUX            (FIRST_AUX + AUX_COUNT - 1)     /* last auxiliary index */


/* Service times */

#define MUXL_WAIT       500                             /* initial fast receive/send time in event ticks */


/* Unit flags */

#define UNIT_V_MDM      (TTUF_V_UF + 0)                 /* modem control */
#define UNIT_V_DIAG     (TTUF_V_UF + 1)                 /* loopback diagnostic */
#define UNIT_MDM        (1 << UNIT_V_MDM)
#define UNIT_DIAG       (1 << UNIT_V_DIAG)


/* Channel number (OTA upper, LIA lower or upper) */

#define MUX_V_CHAN      10                              /* channel num */
#define MUX_M_CHAN      037
#define MUX_CHAN(x)     (((x) >> MUX_V_CHAN) & MUX_M_CHAN)

/* OTA, lower = parameters or data */

#define OTL_P           0100000                         /* parameter */
#define OTL_TX          0040000                         /* transmit */
#define OTL_ENB         0020000                         /* enable */
#define OTL_TPAR        0010000                         /* xmt parity */
#define OTL_ECHO        0010000                         /* rcv echo */
#define OTL_DIAG        0004000                         /* diagnose */
#define OTL_SYNC        0004000                         /* sync */
#define OTL_V_LNT       8                               /* char length */
#define OTL_M_LNT       07
#define OTL_LNT(x)      (((x) >> OTL_V_LNT) & OTL_M_LNT)
#define OTL_V_BAUD      0                               /* baud rate */
#define OTL_M_BAUD      0377
#define OTL_BAUD(x)     (((x) >> OTL_V_BAUD) & OTL_M_BAUD)
#define OTL_CHAR        03777                           /* char mask */
#define OTL_PAR         0200                            /* char parity */

#define BAUD_RATE(p)        ((28800 / (OTL_BAUD (p) + 1) + 1) / 2)

static const uint32 bits_per_char [8] = {       /* bits per character, indexed by OTL_LNT encoding */
    9, 10, 11, 12, 5, 6, 7, 8
    };

static const BITSET_NAME lower_parameter_names [] = {   /* lower data card parameter word names */
    "\1send\0receive",                                  /*   bit 14 */
    "enable interrupt",                                 /*   bit 13 */
    "enable parity/echo",                               /*   bit 12 */
    "diagnose"                                          /*   bit 11 */
    };

static const BITSET_FORMAT lower_parameter_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (lower_parameter_names, 11, msb_first, has_alt, append_bar) };

static const BITSET_NAME lower_data_names [] = {        /* lower data card output data word names */
    "send",                                             /*   bit 14 */
    NULL,                                               /*   bit 13 */
    NULL,                                               /*   bit 12 */
    "sync"                                              /*   bit 11 */
    };

static const BITSET_FORMAT lower_data_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (lower_data_names, 11, msb_first, no_alt, append_bar) };


/* LIA, lower = received data */

#define LIL_PAR         0100000                         /* parity */
#define PUT_DCH(x)      (((x) & MUX_M_CHAN) << MUX_V_CHAN)
#define LIL_CHAR        01777                           /* character */

static const BITSET_NAME lower_input_names [] = {       /* lower data card input data word names */
    "\1odd parity\0even parity",                        /*   bit 15 */
    };

static const BITSET_FORMAT lower_input_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (lower_input_names, 0, msb_first, has_alt, append_bar) };


/* LIA, upper = status */

#define LIU_SEEK        0100000                         /* seeking NI */
#define LIU_DG          0000010                         /* diagnose */
#define LIU_BRK         0000004                         /* break */
#define LIU_LOST        0000002                         /* char lost */
#define LIU_TR          0000001                         /* trans/rcv */

static const BITSET_NAME upper_status_names [] = {      /* upper data card status word names */
    "seeking",                                          /*   bit 15 */
    NULL,                                               /*   bit 14 */
    NULL,                                               /*   bit 13 */
    NULL,                                               /*   bit 12 */
    NULL,                                               /*   bit 11 */
    NULL,                                               /*   bit 10 */
    NULL,                                               /*   bit  9 */
    NULL,                                               /*   bit  8 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  4 */
    "diagnose",                                         /*   bit  3 */
    "break"                                             /*   bit  2 */
    "lost",                                             /*   bit  1 */
    "\1send\0receive"                                   /*   bit  0 */
    };

static const BITSET_FORMAT upper_status_format =        /* names, offset, direction, alternates, bar */
    { FMT_INIT (upper_status_names, 0, msb_first, has_alt, no_bar) };


/* OTA, control */

#define OTC_SCAN        0100000                         /* scan */
#define OTC_UPD         0040000                         /* update */
#define OTC_V_CHAN      10                              /* channel */
#define OTC_M_CHAN      017
#define OTC_CHAN(x)     (((x) >> OTC_V_CHAN) & OTC_M_CHAN)
#define OTC_EC2         0000200                         /* enable Cn upd */
#define OTC_EC1         0000100
#define OTC_C2          0000040                         /* Cn flops */
#define OTC_C1          0000020
#define OTC_V_C         4                               /* S1 to C1 */
#define OTC_ES2         0000010                         /* enb comparison */
#define OTC_ES1         0000004
#define OTC_V_ES        2
#define OTC_SS2         0000002                         /* SSn flops */
#define OTC_SS1         0000001
#define OTC_RW          (OTC_ES2|OTC_ES1|OTC_SS2|OTC_SS1)

static const BITSET_NAME cntl_control_names [] = {      /* control card control word names */
    "scan",                                             /*   bit 15 */
    "update",                                           /*   bit 14 */
    NULL,                                               /*   bit 13 */
    NULL,                                               /*   bit 12 */
    NULL,                                               /*   bit 11 */
    NULL,                                               /*   bit 10 */
    NULL,                                               /*   bit  9 */
    NULL,                                               /*   bit  8 */
    "EC2",                                              /*   bit  7 */
    "EC1",                                              /*   bit  6 */
    "\1C2\0~C2",                                        /*   bit  5 */
    "\1C1\0~C1",                                        /*   bit  4 */
    "ES2",                                              /*   bit  3 */
    "ES1",                                              /*   bit  2 */
    "\1S2\0~S2",                                        /*   bit  1 */
    "\1S1\0~S1"                                         /*   bit  0 */
    };

static const BITSET_FORMAT cntl_control_format =        /* names, offset, direction, alternates, bar */
    { FMT_INIT (cntl_control_names, 0, msb_first, has_alt, no_bar) };


/* LIA, control */

#define LIC_MBO         0140000                         /* always set */
#define LIC_V_CHAN      10                              /* channel */
#define LIC_M_CHAN      017
#define PUT_CCH(x)      (((x) & OTC_M_CHAN) << OTC_V_CHAN)
#define LIC_I2          0001000                         /* change flags */
#define LIC_I1          0000400
#define LIC_S2          0000002                         /* Sn flops */
#define LIC_S1          0000001
#define LIC_V_I         8                               /* S1 to I1 */

#define LIC_TSTI(ch)    (((muxc_lia[ch] ^ muxc_ota[ch]) & \
                          ((muxc_ota[ch] & (OTC_ES2|OTC_ES1)) >> OTC_V_ES)) \
                         << LIC_V_I)

static const BITSET_NAME cntl_status_names [] = {       /* control card status word names */
    "I2",                                               /*   bit  9 */
    "I1",                                               /*   bit  8 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  4 */
    "ES2",                                              /*   bit  3 */
    "ES1",                                              /*   bit  2 */
    "\1S2\0~S2",                                        /*   bit  1 */
    "\1S1\0~S1"                                         /*   bit  0 */
    };

static const BITSET_FORMAT cntl_status_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (cntl_status_names, 0, msb_first, has_alt, no_bar) };

/* Control card #1 serial line bits */

#define RTS                 OTC_C2              /* Control card #1 C2 = Request to Send */
#define DTR                 OTC_C1              /* Control card #1 C1 = Data Terminal Ready */
#define DCD                 LIC_S2              /* Control card #1 S2 = Data Carrier Detect */
#define DSR                 LIC_S1              /* Control card #1 S1 = Data Set Ready */

static const BITSET_NAME cntl_line_names [] = { /* Control card serial line status names */
    "RTS",                                      /*   bit  5 */
    "DTR",                                      /*   bit  4 */
    NULL,                                       /*   bit  3 */
    NULL,                                       /*   bit  2 */
    "DCD",                                      /*   bit  1 */
    "DSR"                                       /*   bit  0 */
    };

static const BITSET_FORMAT cntl_line_format =   /* names, offset, direction, alternates, bar */
    { FMT_INIT (cntl_line_names, 0, msb_first, no_alt, no_bar) };


/* Program constants */

#define RCV_PAR(x)      (odd_parity [(x) & 0377] ? 0 : LIL_PAR)
#define XMT_PAR(x)      (odd_parity [(x) & 0377] ? 0 : OTL_PAR)


/* Multiplexer controller state variables */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } muxl = { CLEAR, CLEAR, CLEAR };

uint32 muxl_ibuf = 0;                                   /* low in: rcv data */
uint32 muxl_obuf = 0;                                   /* low out: param */

uint32 muxu_ibuf = 0;                                   /* upr in: status */
uint32 muxu_obuf = 0;                                   /* upr out: chan */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } muxc = { CLEAR, CLEAR, CLEAR };

uint32 muxc_chan = 0;                                   /* ctrl chan */
uint32 muxc_scan = 0;                                   /* ctrl scan */


/* Multiplexer per-line state variables */

uint16 mux_sta   [RECV_CHAN_COUNT];             /* line status */
uint16 mux_rpar  [RECV_CHAN_COUNT];             /* rcv param */
uint16 mux_xpar  [SEND_CHAN_COUNT];             /* xmt param */

uint8  mux_rchp  [RECV_CHAN_COUNT];             /* rcv chr pend */
uint8  mux_defer [RECV_CHAN_COUNT];             /* rcv break deferred flags */
uint8  mux_xdon  [SEND_CHAN_COUNT];             /* xmt done */

uint8  muxc_ota  [TERM_COUNT];                  /* ctrl: Cn,ESn,SSn */
uint8  muxc_lia  [TERM_COUNT];                  /* ctrl: Sn */


/* Multiplexer per-line buffer variables */

uint16 mux_rbuf [RECV_CHAN_COUNT];              /* rcv buf */
uint16 mux_xbuf [SEND_CHAN_COUNT];              /* xmt buf */


/* Multiplexer local routines */

void mux_receive (int32 ln, int32 c, t_bool diag);
void mux_data_int (void);
void mux_ctrl_int (void);
void mux_diag (int32 c);


/* Multiplexer global routines */

IOHANDLER muxlio;
IOHANDLER muxuio;
IOHANDLER muxcio;

t_stat muxi_svc (UNIT *uptr);
t_stat muxo_svc (UNIT *uptr);
t_stat muxc_reset (DEVICE *dptr);
t_stat mux_attach (UNIT *uptr, CONST char *cptr);
t_stat mux_detach (UNIT *uptr);
t_stat mux_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc);


/* Multiplexer SCP data structures */


/* Terminal multiplexer library structures */

static int32 mux_order [TERM_COUNT] = {         /* line connection order */
    -1                                          /*   use the default order */
    };

static TMLN mux_ldsc [TERM_COUNT] = {           /* line descriptors */
    { 0 }
    };

static TMXR mux_desc = {                        /* multiplexer descriptor */
    TERM_COUNT,                                 /*   number of terminal lines */
    0,                                          /*   listening port (reserved) */
    0,                                          /*   master socket  (reserved) */
    mux_ldsc,                                   /*   line descriptors */
    mux_order,                                  /*   line connection order */
    NULL                                        /*   multiplexer device (derived internally) */
    };


/* Device information blocks.

   The DIBs of adjacent cards must be contained in an array, so they are defined
   here and referenced in the lower and upper card device structures.
*/

DIB mux_dib [] = {
    { &muxlio, MUXL, 0 },
    { &muxuio, MUXU, 0 }
    };

#define muxl_dib            mux_dib [0]
#define muxu_dib            mux_dib [1]


/* Unit list */

static UNIT muxl_unit [UNIT_COUNT] = {
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT }
    };


/* Register list */

static REG muxl_reg [] = {
/*    Macro   Name   Location              Radix  Width  Offset       Depth              Flags       */
/*    ------  -----  --------------------  -----  -----  ------ -----------------  ----------------- */
    { FLDATA (CTL,   muxl.control,                         0)                                        },
    { FLDATA (FLG,   muxl.flag,                            0)                                        },
    { FLDATA (FBF,   muxl.flagbuf,                         0)                                        },
    { BRDATA (STA,   mux_sta,                8,    16,           RECV_CHAN_COUNT)                    },
    { BRDATA (RPAR,  mux_rpar,               8,    16,           RECV_CHAN_COUNT)                    },
    { BRDATA (XPAR,  mux_xpar,               8,    16,           SEND_CHAN_COUNT)                    },
    { BRDATA (RBUF,  mux_rbuf,               8,    16,           RECV_CHAN_COUNT), REG_A             },
    { BRDATA (XBUF,  mux_xbuf,               8,    16,           SEND_CHAN_COUNT), REG_A             },
    { BRDATA (RCHP,  mux_rchp,               8,     1,           RECV_CHAN_COUNT)                    },
    { BRDATA (XDON,  mux_xdon,               8,     1,           SEND_CHAN_COUNT)                    },
    { BRDATA (BDFR,  mux_defer,              8,     1,           TERM_COUNT)                         },
    { URDATA (TIME,  muxl_unit[0].wait,     10,    24,     0,    TERM_COUNT,       REG_NZ | PV_LEFT) },
    { ORDATA (SC,    muxl_dib.select_code,          6),                            REG_HRO           },
    { ORDATA (DEVNO, muxl_dib.select_code,          6),                            REG_HRO           },
    { NULL }
    };


/* Modifier list */

static MTAB muxl_mod [] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL },

    { UNIT_MDM, UNIT_MDM, "data set", "DATASET",   NULL, NULL, NULL },
    { UNIT_MDM,        0, "direct",   "NODATASET", NULL, NULL, NULL },

    { MTAB_XUN | MTAB_NC, 0, "LOG", "LOG",   &tmxr_set_log,   &tmxr_show_log, (void *) &mux_desc },
    { MTAB_XUN | MTAB_NC, 0, NULL,  "NOLOG", &tmxr_set_nolog, NULL,           (void *) &mux_desc },

    { MTAB_XUN,             0,   NULL,    "DISCONNECT", &tmxr_dscln, NULL,         (void *) &mux_desc },

    { MTAB_XDV,             2u,  "SC",    "SC",         &hp_set_dib, &hp_show_dib, (void *) &mux_dib },
    { MTAB_XDV | MTAB_NMO, ~2u,  "DEVNO", "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &mux_dib },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB muxl_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* Interface control, status, read, and write actions */
    { "SERV",  TRACE_SERV  },                   /* Channel unit service scheduling calls */
    { "XFER",  TRACE_XFER  },                   /* Data receptions and transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE muxl_dev = {
    "MUXL",                                     /* device name */
    muxl_unit,                                  /* unit array */
    muxl_reg,                                   /* register array */
    muxl_mod,                                   /* modifier array */
    UNIT_COUNT,                                 /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &muxc_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &muxl_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    muxl_deb,                                   /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL,                                       /* logical device name */
    NULL,                                       /* help routine */
    NULL,                                       /* help attach routine*/
    NULL                                        /* help context */
    };


/* Unit list */

static UNIT muxu_unit = { UDATA (&muxi_svc, UNIT_ATTABLE, 0), POLL_FIRST };


/* Register list */

static REG muxu_reg [] = {
/*    Macro   Name   Location              Width   Flags  */
/*    ------  -----  --------------------  -----  ------- */
    { ORDATA (IBUF,  muxu_ibuf,             16)           },
    { ORDATA (OBUF,  muxu_obuf,             16)           },
    { ORDATA (SC,    muxu_dib.select_code,   6),  REG_HRO },
    { ORDATA (DEVNO, muxu_dib.select_code,   6),  REG_HRO },
    { NULL }
    };


/* Modifier list */

static MTAB muxu_mod [] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAGNOSTIC", &mux_setdiag, NULL,            NULL               },
    { UNIT_DIAG, 0,         "terminal mode",   "TERMINAL",   &mux_setdiag, NULL,            NULL               },
    { UNIT_ATT,  UNIT_ATT,  "",                NULL,         NULL,         &tmxr_show_summ, (void *) &mux_desc },

    { MTAB_XDV | MTAB_NMO,  0, "LINEORDER", "LINEORDER", &tmxr_set_lnorder, &tmxr_show_lnorder, &mux_desc },

    { MTAB_XDV | MTAB_NMO,  1, "CONNECTIONS", NULL,         NULL,        &tmxr_show_cstat, (void *) &mux_desc },
    { MTAB_XDV | MTAB_NMO,  0, "STATISTICS",  NULL,         NULL,        &tmxr_show_cstat, (void *) &mux_desc },
    { MTAB_XDV,             1, NULL,          "DISCONNECT", &tmxr_dscln, NULL,             (void *) &mux_desc },

    { MTAB_XDV,             2u, "SC",          "SC",         &hp_set_dib, &hp_show_dib, (void *) &mux_dib },
    { MTAB_XDV | MTAB_NMO, ~2u, "DEVNO",       "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &mux_dib },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB muxu_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* Interface control, status, read, and write actions */
    { "PSERV", TRACE_PSERV },                   /* Poll unit service scheduling calls */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE muxu_dev = {
    "MUX",                                      /* device name */
    &muxu_unit,                                 /* unit array */
    muxu_reg,                                   /* register array */
    muxu_mod,                                   /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    &tmxr_ex,                                   /* examine routine */
    &tmxr_dep,                                  /* deposit routine */
    &muxc_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    &mux_attach,                                /* attach routine */
    &mux_detach,                                /* detach routine */
    &muxu_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG | DEV_MUX,          /* device flags */
    0,                                          /* debug control flags */
    muxu_deb,                                   /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL,                                       /* logical device name */
    NULL,                                       /* help routine */
    NULL,                                       /* help attach routine*/
    (void *) &mux_desc                          /* help context */
    };


/* Device information block */

static DIB muxc_dib = {
    &muxcio,                                    /* device interface */
    MUXC,                                       /* select code */
    0                                           /* card index */
    };


/* Unit list */

UNIT muxc_unit = { UDATA (NULL, 0, 0) };


/* Register list */

static REG muxc_reg [] = {
/*    Macro   Name   Location              Radix  Width  Offset       Depth              Flags       */
/*    ------  -----  --------------------  -----  -----  ------ -----------------  ----------------- */
    { FLDATA (CTL,   muxc.control,                          0)                                       },
    { FLDATA (FLG,   muxc.flag,                             0)                                       },
    { FLDATA (FBF,   muxc.flagbuf,                          0)                                       },
    { FLDATA (SCAN,  muxc_scan,                             0)                                       },
    { ORDATA (CHAN,  muxc_chan,                     4)                                               },
    { BRDATA (DSO,   muxc_ota,               2,     6,          TERM_COUNT)                          },
    { BRDATA (DSI,   muxc_lia,               2,     2,          TERM_COUNT)                          },
    { ORDATA (SC,    muxc_dib.select_code,          6),                            REG_HRO           },
    { ORDATA (DEVNO, muxc_dib.select_code,          6),                            REG_HRO           },
    { NULL }
    };


/* Modifier list */

static MTAB muxc_mod [] = {
    { MTAB_XTD | MTAB_VDV,             1u, "SC",    "SC",    &hp_set_dib, &hp_show_dib, (void *) &muxc_dib },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, ~1u, "DEVNO", "DEVNO", &hp_set_dib, &hp_show_dib, (void *) &muxc_dib },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB muxc_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* Interface control, status, read, and write actions */
    { "XFER",  TRACE_XFER  },                   /* Data receptions and transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE muxc_dev = {
    "MUXM",                                     /* device name (deprecated; use MUXC) */
    &muxc_unit,                                 /* unit array */
    muxc_reg,                                   /* register array */
    muxc_mod,                                   /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &muxc_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &muxc_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    muxc_deb,                                   /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL,                                       /* logical device name */
    NULL,                                       /* help routine */
    NULL,                                       /* help attach routine*/
    NULL                                        /* help context */
    };


/* Lower data card I/O signal handler.

   Implementation notes:

    1. The operating manual says that "at least 100 milliseconds of CLC 0s must
       be programmed" by systems employing the multiplexer to ensure that the
       multiplexer resets.  In practice, such systems issue 128K CLC 0
       instructions.  In simulation, only one ioCRS invocation is required to
       reset the multiplexer.
*/

uint32 muxlio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
int32    ln;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            muxl.flag = muxl.flagbuf = CLEAR;
            mux_data_int ();                            /* look for new int */
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            muxl.flag = muxl.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (muxl);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (muxl);
            break;


        case ioIOI:                                     /* I/O data input */
            tprintf (muxl_dev, TRACE_CSRW, "Input data is channel %u | %s%04o\n",
                     MUX_CHAN (muxl_ibuf),
                     fmt_bitset (muxl_ibuf, lower_input_format),
                     muxl_ibuf & LIL_CHAR);

            stat_data = IORETURN (SCPE_OK, muxl_ibuf);  /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            muxl_obuf = IODATA (stat_data);             /* store data */


            if (muxl_obuf & OTL_P)
                tprintf (muxl_dev, TRACE_CSRW, "Parameter is %s%u bits | %u baud\n",
                         fmt_bitset (muxl_obuf, lower_parameter_format),
                         bits_per_char [OTL_LNT (muxl_obuf)],
                         BAUD_RATE (muxl_obuf));
            else
                tprintf (muxl_dev, TRACE_CSRW, "Output data is %s%04o\n",
                         fmt_bitset (muxl_obuf, lower_data_format),
                         muxl_obuf & OTL_CHAR);
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            muxl.flag = muxl.flagbuf = SET;             /* set flag andflag buffer */
            break;


        case ioCRS:                                     /* control reset */
            muxl.control = CLEAR;                       /* clear control flip-flop */

            for (ln = 0; ln < SEND_CHAN_COUNT; ln++) {  /* clear transmit info */
                mux_xbuf[ln] = mux_xpar[ln] = 0;
                muxc_ota[ln] = muxc_lia[ln] = mux_xdon[ln] = 0;
                }

            for (ln = 0; ln < RECV_CHAN_COUNT; ln++) {
                mux_rbuf[ln] = mux_rpar[ln] = 0;        /* clear receive info */
                mux_sta[ln] = mux_rchp[ln] = 0;
                }

            break;


        case ioCLC:                                     /* clear control flip-flop */
            muxl.control = CLEAR;
            break;


        case ioSTC:                                             /* set control flip-flop */
            muxl.control = SET;                                 /* set control */

            ln = MUX_CHAN (muxu_obuf);                          /* get chan # */

            if (muxl_obuf & OTL_TX)                             /* if this is a send parameter or data */
                if (ln >= SEND_CHAN_COUNT)                      /*   then report if the channel number is out of range */
                    tprintf (muxl_dev, TRACE_CSRW, "Send channel %d invalid\n",
                             ln);

                else if (muxl_obuf & OTL_P) {                   /* otherwise if this is a parameter store */
                    mux_xpar[ln] = (uint16) muxl_obuf;          /*   then save it */

                    tprintf (muxl_dev, TRACE_CSRW, "Channel %d send parameter %06o stored\n",
                             ln, muxl_obuf);
                    }

                else {                                          /* otherwise this is a data store */
                    if (mux_xpar[ln] & OTL_TPAR)                /* if parity is enabled */
                        muxl_obuf = muxl_obuf & ~OTL_PAR        /*   then replace the parity bit */
                                      | XMT_PAR (muxl_obuf);    /*     with the calculated value */

                    mux_xbuf[ln] = (uint16) muxl_obuf;          /* load buffer */

                    if (sim_is_active (&muxl_unit[ln])) {       /* still working? */
                        mux_sta[ln] = mux_sta[ln] | LIU_LOST;   /* char lost */

                        tprintf (muxl_dev, TRACE_CSRW, "Channel %d send data overrun\n",
                                 ln);
                        }

                    else {
                        if (muxu_unit.flags & UNIT_DIAG)        /* loopback? */
                            mux_ldsc[ln].conn = 1;              /* connect this line */

                        sim_activate (&muxl_unit[ln], muxl_unit[ln].wait);

                        tprintf (muxl_dev, TRACE_CSRW, "Channel %d send data %06o stored\n",
                                 ln, muxl_obuf);

                        tprintf (muxl_dev, TRACE_SERV, "Channel %d delay %d service scheduled\n",
                                 ln, muxl_unit [ln].wait);
                        }
                    }

            else                                        /* otherwise this is a receive parameter */
                if (ln >= RECV_CHAN_COUNT)              /* report if the channel number is out of range */
                    tprintf (muxl_dev, TRACE_CSRW, "Receive channel %d invalid\n",
                             ln);

                else if (muxl_obuf & OTL_P) {           /* otherwise if this is a parameter store */
                    mux_rpar[ln] = (uint16) muxl_obuf;  /*   then save it */

                    tprintf (muxl_dev, TRACE_CSRW, "Channel %d receive parameter %06o stored\n",
                             ln, muxl_obuf);
                    }

                else                                    /* otherwise a data store to a receive channel is invalid */
                    tprintf (muxl_dev, TRACE_CSRW, "Channel %d receive output data word %06o invalid\n",
                             ln, muxl_obuf);

            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (muxl);                           /* set standard PRL signal */
            setstdIRQ (muxl);                           /* set standard IRQ signal */
            setstdSRQ (muxl);                           /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            muxl.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Upper data card I/O signal handler.

   The upper data card does not have a control, flag, or flag buffer flip-flop.
   It does not drive the IRQ or SRQ lines, so the I/O dispatcher does not handle
   the ioSIR signal.

   Implementation notes:

    1. The upper and lower data card hardware takes a number of actions in
       response to the CRS signal.  Under simulation, these actions are taken by
       the lower data card CRS handler.
*/

uint32 muxuio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, muxu_ibuf);  /* merge in return status */

            tprintf (muxu_dev, TRACE_CSRW, "Status is channel %u | %s\n",
                     MUX_CHAN (muxu_ibuf),
                     fmt_bitset (muxu_ibuf, upper_status_format));
            break;


        case ioIOO:                                     /* I/O data output */
            muxu_obuf = IODATA (stat_data);             /* store data */

            tprintf (muxu_dev, TRACE_CSRW, "Channel %d is selected\n",
                     MUX_CHAN (muxu_obuf));
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Control card I/O signal handler.

   In diagnostic mode, the control signals C1 and C2 are looped back to status
   signals S1 and S2.  Changing the control signals may cause an interrupt, so a
   test is performed after IOO processing.
*/

uint32 muxcio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
int32 ln, old;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            muxc.flag = muxc.flagbuf = CLEAR;
            mux_ctrl_int ();                            /* look for new int */
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            muxc.flag = muxc.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (muxc);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (muxc);
            break;


        case ioIOI:                                                         /* I/O data input */
            data = (uint16) (LIC_MBO | PUT_CCH (muxc_chan) |                /* mbo, chan num */
                             LIC_TSTI (muxc_chan) |                         /* I2, I1 */
                             (muxc_ota[muxc_chan] & (OTC_ES2 | OTC_ES1)) |  /* ES2, ES1 */
                             (muxc_lia[muxc_chan] & (LIC_S2 | LIC_S1)));    /* S2, S1 */

            tprintf (muxc_dev, TRACE_CSRW, "Status is channel %u | %s\n",
                     muxc_chan, fmt_bitset (data, cntl_status_format));

            muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;               /* incr channel */
            stat_data = IORETURN (SCPE_OK, data);                   /* merge in return status */
            break;


        case ioIOO:                                             /* I/O data output */
            data = IODATA (stat_data);                          /* clear supplied status */
            ln = muxc_chan = OTC_CHAN (data);                   /* set channel */

            tprintf (muxc_dev, TRACE_CSRW, "Control is channel %u | %s\n",
                     muxc_chan, fmt_bitset (data, cntl_control_format));

            if (data & OTC_SCAN) muxc_scan = 1;                 /* set scan flag */
            else muxc_scan = 0;

            if (data & OTC_UPD) {                               /* update? */
                old = muxc_ota[ln];                             /* save prior val */
                muxc_ota[ln] =                                  /* save ESn,SSn */
                    (muxc_ota[ln] & ~OTC_RW) | (data & OTC_RW);

                if (data & OTC_EC2)                             /* if EC2, upd C2 */
                    muxc_ota[ln] =
                        (muxc_ota[ln] & ~OTC_C2) | (data & OTC_C2);

                if (data & OTC_EC1)                             /* if EC1, upd C1 */
                    muxc_ota[ln] =
                        (muxc_ota[ln] & ~OTC_C1) | (data & OTC_C1);

                tprintf (muxc_dev, TRACE_XFER, "Channel %d line status is %s\n",
                         ln, fmt_bitset (muxc_ota [ln], cntl_line_format));

                if (muxu_unit.flags & UNIT_DIAG) {              /* loopback? */
                    muxc_lia[ln ^ 1] =                          /* set S1, S2 to C1, C2 */
                        (muxc_lia[ln ^ 1] & ~(LIC_S2 | LIC_S1)) |
                        (muxc_ota[ln] & (OTC_C1 | OTC_C2)) >> OTC_V_C;

                    tprintf (muxc_dev, TRACE_XFER, "Channel %d line status is %s\n",
                             ln ^ 1, fmt_bitset (muxc_lia [ln ^ 1], cntl_line_format));
                    }

                else if ((muxl_unit[ln].flags & UNIT_MDM)       /* modem ctrl? */
                  && (old & DTR) && !(muxc_ota[ln] & DTR)) {    /* DTR drop? */
                    tprintf (muxc_dev, TRACE_CSRW, "Channel %d disconnected by DTR drop\n",
                             ln);

                    tmxr_linemsg (&mux_ldsc[ln], "\r\nDisconnected from the ");
                    tmxr_linemsg (&mux_ldsc[ln], sim_name);
                    tmxr_linemsg (&mux_ldsc[ln], " simulator\r\n\n");

                    tmxr_reset_ln (&mux_ldsc[ln]);              /* reset line */
                    muxc_lia[ln] = 0;                           /* dataset off */

                    tprintf (muxc_dev, TRACE_XFER, "Channel %d disconnect dropped DCD and DSR\n",
                             ln);
                    }
                }                                               /* end update */

            if ((muxu_unit.flags & UNIT_DIAG) && (!muxc.flag))  /* loopback and flag clear? */
                mux_ctrl_int ();                                /* status chg may interrupt */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            muxc.flag = muxc.flagbuf = SET;             /* set flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            muxc.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            muxc.control = SET;
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (muxc);                           /* set standard PRL signal */
            setstdIRQ (muxc);                           /* set standard IRQ signal */
            setstdSRQ (muxc);                           /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            muxc.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service - receive side

   Poll for new connections
   Poll all active lines for input
*/

t_stat muxi_svc (UNIT *uptr)
{
int32 ln, c;
t_bool loopback;

tprintf (muxu_dev, TRACE_PSERV, "Poll delay %d service entered\n",
         uptr->wait);

loopback = ((muxu_unit.flags & UNIT_DIAG) != 0);        /* diagnostic mode? */

if (!loopback) {                                        /* terminal mode? */
    if (uptr->wait == POLL_FIRST)                       /* first poll? */
        uptr->wait = sync_poll (INITIAL);               /* initial synchronization */
    else                                                /* not first */
        uptr->wait = sync_poll (SERVICE);               /* continue synchronization */

    sim_activate (uptr, uptr->wait);                    /* continue polling */

    ln = tmxr_poll_conn (&mux_desc);                    /* look for connect */

    if (ln >= 0) {                                      /* got one? */
        mux_ldsc[ln].rcve = 1;                          /* rcv enabled */
        muxc_lia[ln] = muxc_lia[ln] | DSR;              /* set dsr */

        if ((muxl_unit[ln].flags & UNIT_MDM) &&         /* modem ctrl? */
            (muxc_ota[ln] & DTR))                       /* DTR? */
            muxc_lia[ln] = muxc_lia[ln] | DCD;          /* set DCD */

        tprintf (muxc_dev, TRACE_XFER, "Channel %d connected\n",
                 ln);
        }

    tmxr_poll_rx (&mux_desc);                           /* poll for input */
    }

for (ln = 0; ln < SEND_CHAN_COUNT; ln++) {              /* loop thru lines */
    if (mux_ldsc[ln].conn) {                            /* connected? */
        if (loopback) {                                 /* diagnostic mode? */
            c = mux_xbuf[ln ^ 1] & OTL_CHAR;            /* get char from xmit line */
            if (c == 0)                                 /* all char bits = 0? */
                c = c | SCPE_BREAK;                     /* set break flag */
            mux_ldsc[ln].conn = 0;                      /* clear connection */
            }

        else if (mux_defer[ln])                         /* break deferred? */
            c = SCPE_BREAK;                             /* supply it now */

        else
            c = tmxr_getc_ln (&mux_ldsc[ln]);           /* get char from line */

        if (c)                                          /* valid char? */
            mux_receive (ln, c, loopback);              /* process it */
        }

    else                                                /* not connected */
        if (!loopback)                                  /* terminal mode? */
            muxc_lia[ln] = 0;                           /* line disconnected */
    }

if (!muxl.flag) mux_data_int ();                        /* scan for data int */
if (!muxc.flag) mux_ctrl_int ();                        /* scan modem */
return SCPE_OK;
}


/* Unit service - transmit side */

t_stat muxo_svc (UNIT *uptr)
{
const int32 ln = uptr - muxl_unit;                      /* line # */
const int32 altln = ln ^ 1;                             /* alt. line for diag mode */
int32 c, fc;
t_bool loopback;
t_stat result = SCPE_OK;

tprintf (muxl_dev, TRACE_SERV, "Channel %d service entered\n",
         ln);

fc = mux_xbuf[ln] & OTL_CHAR;                           /* full character data */
c = fc & 0377;                                          /* line character data */

loopback = ((muxu_unit.flags & UNIT_DIAG) != 0);        /* diagnostic mode? */

if (mux_ldsc[ln].xmte) {                                /* xmt enabled? */
    if (loopback)                                       /* diagnostic mode? */
        mux_ldsc[ln].conn = 0;                          /* clear connection */

    else if (mux_defer[ln])                             /* break deferred? */
        mux_receive (ln, SCPE_BREAK, loopback);         /* process it now */

    if ((mux_xbuf[ln] & OTL_SYNC) == 0) {               /* start bit 0? */
        TMLN *lp = &mux_ldsc[ln];                       /* get line */
        c = sim_tt_outcvt (c, TT_GET_MODE (muxl_unit[ln].flags));

        if (mux_xpar[ln] & OTL_DIAG)                    /* xmt diagnose? */
            mux_diag (fc);                              /* before munge */

        if (loopback) {                                 /* diagnostic mode? */
            mux_ldsc[altln].conn = 1;                   /* set recv connection */
            sim_activate (&muxu_unit, 1);               /* schedule receive */
            }

        else {                                          /* no loopback */
            if (c >= 0)                                 /* valid? */
                result = tmxr_putc_ln (lp, c);          /* output char */
            tmxr_poll_tx (&mux_desc);                   /* poll xmt */
            }
        }

    else if (mux_ldsc [ln].conn == 0)                   /* sync character isn't seen by receiver */
        result = SCPE_LOST;                             /*   so report transfer success if connected */

    mux_xdon[ln] = 1;                                   /* set for xmit irq */

    if (loopback || c >= 0)
        if (result == SCPE_LOST)
            tprintf (muxl_dev, TRACE_XFER, "Channel %d character %s discarded by connection loss\n",
                     ln, fmt_char ((uint8) (loopback ? fc : c)));
        else
            tprintf (muxl_dev, TRACE_XFER, "Channel %d character %s sent\n",
                     ln, fmt_char ((uint8) (loopback ? fc : c)));
    }

else {                                              /* buf full */
    tmxr_poll_tx (&mux_desc);                       /* poll xmt */
    sim_activate (uptr, muxl_unit[ln].wait);        /* wait */

    tprintf (muxl_dev, TRACE_SERV, "Channel %d delay %d service rescheduled\n",
             ln, muxl_unit [ln].wait);

    return SCPE_OK;
    }

if (!muxl.flag) mux_data_int ();                        /* scan for int */
return SCPE_OK;
}


/* Process a character received from a multiplexer port */

void mux_receive (int32 ln, int32 c, t_bool diag)
{
if (c & SCPE_BREAK) {                                   /* break? */
    if (mux_defer[ln] || diag) {                        /* break deferred or diagnostic mode? */
        mux_defer[ln] = 0;                              /* process now */
        mux_rbuf[ln] = 0;                               /* break returns NUL */
        mux_sta[ln] = mux_sta[ln] | LIU_BRK;            /* set break status */

        if (diag)
            tprintf (muxl_dev, TRACE_XFER, "Channel %d break detected\n", ln);
        else
            tprintf (muxl_dev, TRACE_XFER, "Channel %d deferred break processed\n", ln);
        }

    else {
        mux_defer[ln] = 1;                              /* defer break */

        tprintf (muxl_dev, TRACE_XFER, "Channel %d break detected and deferred\n", ln);

        return;
        }
    }
else {                                                  /* normal */
    if (mux_rchp[ln])                                   /* char already pending? */
        mux_sta[ln] = mux_sta[ln] | LIU_LOST;

    if (!diag) {                                        /* terminal mode? */
        c = sim_tt_inpcvt (c, TT_GET_MODE (muxl_unit[ln].flags));
        if (mux_rpar[ln] & OTL_ECHO) {                  /* echo? */
            TMLN *lp = &mux_ldsc[ln];                   /* get line */
            tmxr_putc_ln (lp, c);                       /* output char */
            tmxr_poll_tx (&mux_desc);                   /* poll xmt */
            }
        }
    mux_rbuf[ln] = (uint16) c;                          /* save char */
    }

mux_rchp[ln] = 1;                                       /* char pending */

tprintf (muxl_dev, TRACE_XFER, "Channel %d character %s received\n",
         ln, fmt_char ((uint8) c));

if (mux_rpar[ln] & OTL_DIAG)                            /* diagnose this line? */
    mux_diag (c);                                       /* do diagnosis */

return;
}


/* Look for data interrupt */

void mux_data_int (void)
{
int32 i;

for (i = FIRST_TERM; i <= LAST_TERM; i++) {             /* rcv lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {       /* enabled, char? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i];           /* hi buf = stat */
        mux_rchp[i] = 0;                                /* clr char, stat */
        mux_sta[i] = 0;

        tprintf (muxl_dev, TRACE_CSRW, "Channel %d receive interrupt requested\n",
                 i);

        muxlio (&muxl_dib, ioENF, 0);                   /* interrupt */
        return;
        }
    }
for (i = FIRST_TERM; i <= LAST_TERM; i++) {             /* xmt lines */
    if ((mux_xpar[i] & OTL_ENB) && mux_xdon[i]) {       /* enabled, done? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = last rcv char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i] | LIU_TR;  /* hi buf = stat */
        mux_xdon[i] = 0;                                /* clr done, stat */
        mux_sta[i] = 0;

        tprintf (muxl_dev, TRACE_CSRW, "Channel %d send interrupt requested\n",
                 i);

        muxlio (&muxl_dib, ioENF, 0);                   /* interrupt */
        return;
        }
    }
for (i = FIRST_AUX; i <= LAST_AUX; i++) {               /* diag lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {       /* enabled, char? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i] | LIU_DG;  /* hi buf = stat */
        mux_rchp[i] = 0;                                /* clr char, stat */
        mux_sta[i] = 0;

        tprintf (muxl_dev, TRACE_CSRW, "Channel %d receive interrupt requested\n",
                 i);

        muxlio (&muxl_dib, ioENF, 0);                       /* interrupt */
        return;
        }
    }
return;
}


/* Look for control interrupt

   If either of the incoming status bits does not match the stored status, and
   the corresponding mismatch is enabled, a control interrupt request is
   generated.  Depending on the scan flag, we check either all 16 lines or just
   the current line.  If an interrupt is requested, the channel counter
   indicates the interrupting channel.
*/

void mux_ctrl_int (void)
{
int32 i, line_count;

line_count = (muxc_scan ? TERM_COUNT : 1);              /* check one or all lines */

for (i = 0; i < line_count; i++) {
    if (muxc_scan)                                      /* scanning? */
        muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;       /* step channel */

    if (LIC_TSTI (muxc_chan)) {                         /* status change? */
        tprintf (muxc_dev, TRACE_CSRW, "Channel %u interrupt requested\n",
                 muxc_chan);

        muxcio (&muxc_dib, ioENF, 0);                   /* set flag */
        break;
        }
    }
return;
}


/* Set diagnostic lines for given character */

void mux_diag (int32 c)
{
int32 i;

for (i = FIRST_AUX; i <= LAST_AUX; i++) {             /* diag lines */
    if (c & SCPE_BREAK) {                               /* break? */
        mux_sta[i] = mux_sta[i] | LIU_BRK;
        mux_rbuf[i] = 0;                                /* no char */
        }
    else {
        if (mux_rchp[i]) mux_sta[i] = mux_sta[i] | LIU_LOST;
        mux_rchp[i] = 1;
        mux_rbuf[i] = (uint16) c;
        }
    }
return;
}


/* Reset an individual line */

static void mux_reset_ln (int32 i)
{
mux_rbuf[i] = mux_xbuf[i] = 0;                          /* clear state */
mux_rpar[i] = mux_xpar[i] = 0;
mux_rchp[i] = mux_xdon[i] = 0;
mux_sta[i] = mux_defer[i] = 0;
muxc_ota[i] = muxc_lia[i] = 0;                          /* clear modem */

if (mux_ldsc [i].conn                                   /* connected? */
  && (muxu_unit.flags & UNIT_DIAG) == 0)                /* term mode? */
    muxc_lia[i] = muxc_lia[i] | DSR                     /* DCD, dsr */
      | (muxl_unit[i].flags & UNIT_MDM ? DCD : 0);

sim_cancel (&muxl_unit[i]);
return;
}


/* Reset routine for lower data, upper data, and control cards */

t_stat muxc_reset (DEVICE *dptr)
{
int32 i;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */

if (sim_switches & SWMASK ('P')                         /* initialization reset? */
  && muxc_dev.lname == NULL)                            /* logical name unassigned? */
    muxc_dev.lname = strdup ("MUXC");                   /* allocate and initialize the name */

if (dptr == &muxl_dev)                                  /* make all consistent */
    hp_enbdis_pair (dptr, &muxu_dev);

else if (dptr == &muxu_dev)
    hp_enbdis_pair (dptr, &muxl_dev);

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

muxc_chan = muxc_scan = 0;                              /* init modem scan */

if (muxu_unit.flags & UNIT_ATT) {                       /* master att? */
    muxu_unit.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&muxu_unit, muxu_unit.wait);          /* start poll immediately */
    }
else
    sim_cancel (&muxu_unit);                            /* else stop */

for (i = FIRST_TERM; i <= LAST_TERM; i++)
    mux_reset_ln (i);                                   /* reset lines 0-15 */

for (i = FIRST_AUX; i <= LAST_AUX; i++)                 /* reset lines 16-20 */
    mux_rbuf[i] = mux_rpar[i] = mux_sta[i] = mux_rchp[i] = 0;

return SCPE_OK;
}


/* Attach master unit */

t_stat mux_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status = SCPE_OK;

if (muxu_unit.flags & UNIT_DIAG)                        /* diag mode? */
    return SCPE_NOFNC;                                  /* command not allowed */

status = tmxr_attach (&mux_desc, uptr, cptr);           /* attach */

if (status == SCPE_OK) {
    muxu_unit.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&muxu_unit, muxu_unit.wait);          /* start poll immediately */
    }

return status;
}


/* Detach master unit */

t_stat mux_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&mux_desc, uptr);                      /* detach */

for (i = 0; i < TERM_COUNT; i++)                        /* disable rcv */
    mux_ldsc[i].rcve = 0;

sim_cancel (uptr);                                      /* stop poll */
return r;
}


/* Diagnostic/normal mode routine,

   Diagnostic testing wants to exercise as much of the regular simulation code
   as possible to ensure good test coverage.  Normally, input polling and output
   transmission only occurs on connected lines.  In diagnostic mode, line
   connection flags are set selectively to enable processing on the lines under
   test.  The alternative to this would require duplicating the send/receive
   code; the diagnostic would then test the copy but not the actual code used
   for normal character transfers, which is undesirable.

   Therefore, to enable diagnostic mode, we must force a disconnect of the
   master socket and any connected Telnet lines, which clears the connection
   flags on all lines.  Then we set the "transmission enabled" flags on all
   lines to enable output character processing for the diagnostic.  (Normally,
   all of the flags are set when the multiplexer is first attached.  Until then,
   the enable flags default to "not enabled," so we enable them explicitly
   here.)
*/

t_stat mux_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 ln;

if (val) {                                              /* set diag? */
    mux_detach (uptr);                                  /* detach Telnet lines */
    for (ln = 0; ln < TERM_COUNT; ln++)                 /* enable transmission */
        mux_ldsc[ln].xmte = 1;                          /* on all lines */
    }
else {                                                  /* set term */
    for (ln = 0; ln < TERM_COUNT; ln++)                 /* clear connections */
        mux_ldsc[ln].conn = 0;                          /* on all lines */
    }
return SCPE_OK;
}
