/* hp2100_mpx.c: HP 12792C eight-channel asynchronous multiplexer simulator

   Copyright (c) 2008-2016, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   MPX          12792C 8-channel multiplexer card

   02-Aug-16    JDB     Burst-fill only the first receive buffer in fast mode
   28-Jul-16    JDB     Fixed buffer ready check at read completion
                        Fixed terminate on character counts > 254
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Jan-13    MP      Added DEV_MUX and additional DEVICE field values
   28-Dec-12    JDB     Allow direct attach to the poll unit only when restoring
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Removed DEV_NET to allow restoration of listening port
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   25-Nov-08    JDB     Revised for new multiplexer library SHOW routines
   14-Nov-08    JDB     Cleaned up VC++ size mismatch warnings for zero assignments
   03-Oct-08    JDB     Fixed logic for ENQ/XOFF transmit wait
   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   10-Aug-08    JDB     Added REG_FIT to register variables < 32-bit size
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   26-May-08    JDB     Created MPX device

   References:
   - HP 12792B 8-Channel Asynchronous Multiplexer Subsystem Installation and
        Reference Manual (12792-90020, Jul-1984)
   - HP 12792B/C 8-Channel Asynchronous Multiplexer Subsystem User's Manual
        (5955-8867, Jun-1993)
   - HP 12792B/C 8-Channel Asynchronous Multiplexer Subsystem Configuration Guide
        (5955-8868, Jun-1993)
   - HP 1000 series 8-channel Multiplexer Firmware External Reference Specification
        (October 19, 1982)
   - HP 12792/12040 Multiplexer Firmware Source (24999-18312, revision C)
   - Zilog Components Data Book (00-2034-04, 1985)


   The 12792A/B/C/D was an eight-line asynchronous serial multiplexer that
   connected terminals, modems, serial line printers, and "black box" devices
   that used the RS-232 standard to the CPU.  It used an on-board microprocessor
   and provided input and output buffering to support block-mode reads from HP
   264x and 262x terminals at speeds up to 19.2K baud.  The card handled
   character editing, echoing, ENQ/ACK handshaking, and read terminator
   detection, substantially reducing the load on the CPU over the earlier 12920
   multiplexer.  It was supported by HP under RTE-MIII, RTE-IVB, and RTE-6/VM.
   Under simulation, it connects with HP terminal emulators via Telnet to a
   user-specified port.

   The single interface card contained a Z80 CPU, DMA controller, CTC, four
   two-channel SIO UARTs, 16K of RAM, 8K of ROM, and I/O backplane latches and
   control circuitry.  The card executed a high-level command set, and data
   transfer to and from the CPU was via the on-board DMA controller and the DCPC
   in the CPU.

   The 12792 for the M/E/F series and the 12040 multiplexer for the A/L series
   differed only in backplane design.  Early ROMs were card-specific, but later
   ones were interchangeable; the code would determine whether it was executing
   on an MEF card or an AL card.

   Four major firmware revisions were made.  These were labelled "A", "B", "C",
   and "D".  The A, B, and C revisions were interchangeable from the perspective
   of the OS driver; the D was different and required an updated driver.
   Specifically:

     Op. Sys.  Driver  Part Number           Rev
     --------  ------  --------------------  ---
     RTE-MIII  DVM00   12792-16002 Rev.2032   A
     RTE-IVB   DVM00   12792-16002 Rev.5000  ABC

     RTE-6/VM  DVM00   12792-16002 Rev.5000  ABC
     RTE-6/VM  DV800   92084-15068 Rev.6000   D

     RTE-A     IDM00   92077-16754 Rev.5020  ABC
     RTE-A     ID800   92077-16887 Rev.6200   D

   Revisions A-C have an upward-compatible command set that partitions each OS
   request into several sub-commands.  Each command is initiated by setting the
   control flip-flop on the card, which causes a non-maskable interrupt (NMI) on
   the card's Z80 processor.

   The D-revision firmware uses a completely different command set.  The
   commands are slightly modified versions of the original EXEC calls (read,
   write, and control) and are generally passed to the card directly for action.

   This simulation supports the C revision.  D-revision support may be added
   later.

   Twelve programmable baud rates are supported by the multiplexer.  These
   "realistic" rates are simulated by scheduling I/O service based on the
   appropriate number of 1000 E-Series instructions for the rate selected.

   The simulation provides both the "realistic timing" described above, as well
   as an optimized "fast timing" option.  Optimization makes three improvements:

     1. Buffered characters are transferred in blocks.

     2. ENQ/ACK handshaking is done locally without involving the client.

     3. BS and DEL respond visually more like prior RTE terminal drivers.

   HP did not offer a functional diagnostic for the 12792.  Instead, a Z80
   program that tested the operation of the hardware was downloaded to the card,
   and a "go/no-go" status was returned to indicate the hardware condition.
   Because this is a functional simulation of the multiplexer and not a Z80
   emulation, the diagnostic cannot be used to test the implementation.


   Implementation notes:

    1. The 12792 had two baud-rate generators that were assigned to lines by the
       wiring configuration in the I/O cable connector hood.  Two of the four
       CTC counters were used to implement the BRGs for all eight lines.  Only
       subsets of the configurable rates were allowed for lines connected to the
       same BRG, and assigning mutually incompatible rates caused corruption of
       the rates on lines assigned earlier.  Under simulation, any baud rate may
       be assigned to any line without interaction, and assignments of lines to
       BRGs is not implemented.

    2. Revisions B and C added support for the 37214A Systems Modem subsystem
       and the RTE-A Virtual Control Panel (VCP).  Under simulation, the modem
       commands return status codes indicating that no modems are present, and
       the VCP commands are not implemented.
*/


#include <ctype.h>

#include "hp2100_defs.h"
#include "sim_tmxr.h"


/* Bitfield constructor.

   Given a bitfield starting bit number and width in bits, declare two
   constants: one for the starting bit number, and one for the positioned field
   mask.  That is, given a definition such as:

     BITFIELD(SMALLFIELD,5,2)

   ...this macro produces:

     static const uint32 SMALLFIELD_V = 5;
     static const uint32 SMALLFIELD = ((1 << (2)) - 1) << (5);

   The latter reduces to 3 << 5, or 0x00000060.

   Note: C requires constant expressions in initializers for objects with static
   storage duration, so initializing a static object with a BITFIELD value is
   illegal (a "static const" object is not a constant!).
*/

#define BITFIELD(NAME,STARTBIT,BITWIDTH) \
    static const uint32 NAME ## _V = STARTBIT; \
    static const uint32 NAME = ((1 << (BITWIDTH)) - 1) << (STARTBIT);


/* Program constants */

#define MPX_DATE_CODE   2416                            /* date code for C firmware */

#define RD_BUF_SIZE     514                             /* read buffer size */
#define WR_BUF_SIZE     514                             /* write buffer size */

#define RD_BUF_LIMIT    254                             /* read buffer limit */
#define WR_BUF_LIMIT    254                             /* write buffer limit */

#define KEY_DEFAULT     255                             /* default port key */


/* Service times:

     DATA_DELAY  = 1.25 us (Z80 DMA data word transfer time)
     PARAM_DELAY =  25 us (STC to STF for first word of two-word command)
     CMD_DELAY   = 400 us (STC to STF for one or two-word command execution)
*/

#define DATA_DELAY        2                             /* data transfer time */
#define PARAM_DELAY      40                             /* parameter request time */
#define CMD_DELAY       630                             /* command completion time */


/* Unit references */

#define MPX_PORTS       8                               /* number of visible units */
#define MPX_CNTLS       2                               /* number of control units */

#define mpx_cntl        (mpx_unit [MPX_PORTS + 0])      /* controller unit */
#define mpx_poll        (mpx_unit [MPX_PORTS + 1])      /* Telnet polling unit */


/* Character constants */

#define EOT             '\004'
#define ENQ             '\005'
#define ACK             '\006'
#define BS              '\010'
#define LF              '\012'
#define CR              '\015'
#define DC1             '\021'
#define DC2             '\022'
#define DC3             '\023'
#define ESC             '\033'
#define RS              '\036'
#define DEL             '\177'

#define XON             DC1
#define XOFF            DC3


/* Device flags */

#define DEV_V_REV_D     (DEV_V_UF + 0)                  /* firmware revision D (not implemented) */

#define DEV_REV_D       (1 << DEV_V_REV_D)


/* Unit flags */

#define UNIT_V_FASTTIME (UNIT_V_UF + 0)                 /* fast timing mode */
#define UNIT_V_CAPSLOCK (UNIT_V_UF + 1)                 /* caps lock mode */

#define UNIT_FASTTIME   (1 << UNIT_V_FASTTIME)
#define UNIT_CAPSLOCK   (1 << UNIT_V_CAPSLOCK)


/* Debug flags */

#define DEB_CMDS        (1 << 0)                        /* commands and status */
#define DEB_CPU         (1 << 1)                        /* CPU I/O */
#define DEB_BUF         (1 << 2)                        /* buffer gets and puts */
#define DEB_XFER        (1 << 3)                        /* character reads and writes */


/* Multiplexer commands for revisions A/B/C.

   Commands are either one or two words in length.  The one-word format is:

     +-------------------------------+-------------------------------+
     | 0 . 1 |    command opcode     |       command parameter       |
     +-------------------------------+-------------------------------+
                  15 - 8                           7 - 0

   The two-word format is:

     +-------------------------------+-------------------------------+
     | 1 . 1 |    command opcode     |        command value          |
     +-------------------------------+-------------------------------+
     |                       command parameter                       |
     +---------------------------------------------------------------+
                  15 - 8                           7 - 0

   Commands implemented by firmware revision:

     Rev  Cmd  Value  Operation                        Status Value(s) Returned
     ---  ---  -----  -------------------------------  -------------------------------
     ABC  100    -    No operation                     000000
     ABC  101    -    Reset to power-on defaults       100000
     ABC  102    -    Enable unsolicited input         None, unless UI pending
     ABC  103    1    Disable unsolicited interrupts   000000
     ABC  103    2    Abort DMA transfer               000000
     ABC  104    -    Acknowledge                      Second word of UI status
     ABC  105   key   Cancel first receive buffer      000000
     ABC  106   key   Cancel all received buffers      000000
     ABC  107    -    Fast binary read                 (none)

     -BC  140   chr   VCP put byte                     000000
     -BC  141    -    VCP put buffer                   000000
     -BC  142    -    VCP get byte                     Character from port 0
     -BC  143    -    VCP get buffer                   000120
     -BC  144    -    Exit VCP mode                    000000
     -BC  157    -    Enter VCP mode                   000000

     ABC  300    -    No operation                     000000
     ABC  301   key   Request write buffer             000000 or 000376
     ABC  302   key   Write data to buffer             (none)
     ABC  303   key   Set port key                     000000 or date code of firmware
     ABC  304   key   Set receive type                 000000
     ABC  305   key   Set character count              000000
     ABC  306   key   Set flow control                 000000
     ABC  307   key   Read data from buffer            (none)
     ABC  310    -    Download executable              (none)

     -BC  311   key   Connect line                     000000 or 140000 if no modem
     -BC  312   key   Disconnect line                  000000 or 140000 if no modem
     -BC  315   key   Get modem/port status            modem status or 000200 if no modem
     -BC  316   key   Enable/disable modem loopback    000000 or 140000 if no modem
     -BC  320   key   Terminate active receive buffer  000000
*/


/* One-word command codes */

#define CMD_NOP         0100                            /* No operation */
#define CMD_RESET       0101                            /* Reset firmware to power-on defaults */
#define CMD_ENABLE_UI   0102                            /* Enable unsolicited input */
#define CMD_DISABLE     0103                            /* Disable interrupts / Abort DMA Transfer */
#define CMD_ACK         0104                            /* Acknowledge */
#define CMD_CANCEL      0105                            /* Cancel first receive buffer */
#define CMD_CANCEL_ALL  0106                            /* Cancel all received buffers */
#define CMD_BINARY_READ 0107                            /* Fast binary read */

#define CMD_VCP_PUT     0140                            /* VCP put byte */
#define CMD_VCP_PUT_BUF 0141                            /* VCP put buffer */
#define CMD_VCP_GET     0142                            /* VCP get byte */
#define CMD_VCP_GET_BUF 0143                            /* VCP get buffer */
#define CMD_VCP_EXIT    0144                            /* Exit VCP mode */
#define CMD_VCP_ENTER   0157                            /* Enter VCP mode */


/* Two-word command codes */

#define CMD_REQ_WRITE   0301                            /* Request write buffer */
#define CMD_WRITE       0302                            /* Write data to buffer */
#define CMD_SET_KEY     0303                            /* Set port key */
#define CMD_SET_RCV     0304                            /* Set receive type */
#define CMD_SET_COUNT   0305                            /* Set character count */
#define CMD_SET_FLOW    0306                            /* Set flow control */
#define CMD_READ        0307                            /* Read data from buffer */
#define CMD_DL_EXEC     0310                            /* Download executable */

#define CMD_CN_LINE     0311                            /* Connect line */
#define CMD_DC_LINE     0312                            /* Disconnect line */
#define CMD_GET_STATUS  0315                            /* Get modem/port status */
#define CMD_LOOPBACK    0316                            /* Enable/disable modem loopback */
#define CMD_TERM_BUF    0320                            /* Terminate active receive buffer */


/* Sub-command codes */

#define SUBCMD_UI       1                               /* Disable unsolicited interrupts */
#define SUBCMD_DMA      2                               /* Abort DMA transfer */

#define CMD_TWO_WORDS   0200                            /* two-word command flag */


/* Unsolicited interrupt reasons */

#define UI_REASON_V     8                                   /* interrupt reason */
#define UI_REASON       (((1 << 8) - 1) << (UI_REASON_V))   /* (UI_REASON_V must be a constant!) */

BITFIELD (UI_PORT, 0, 3)                                /* interrupt port number */

#define UI_WRBUF_AVAIL  (1 << UI_REASON_V)              /* Write buffer available */
#define UI_LINE_CONN    (2 << UI_REASON_V)              /* Modem line connected */
#define UI_LINE_DISC    (3 << UI_REASON_V)              /* Modem line disconnected */
#define UI_BRK_RECD     (4 << UI_REASON_V)              /* Break received */
#define UI_RDBUF_AVAIL  (5 << UI_REASON_V)              /* Read buffer available */


/* Return status to CPU */

#define ST_OK           0000000                         /* Command OK */
#define ST_DIAG_OK      0000015                         /* Diagnostic passes */
#define ST_VCP_SIZE     0000120                         /* VCP buffer size = 80 chars */
#define ST_NO_SYSMDM    0000200                         /* No systems modem card */
#define ST_TEST_OK      0100000                         /* Self test OK */
#define ST_NO_MODEM     0140000                         /* No modem card on port */
#define ST_BAD_KEY      0135320                         /* Bad port key = 0xBAD0 */


/* Bit flags */

#define RS_OVERFLOW     0040000                         /* Receive status: buffer overflow occurred */
#define RS_PARTIAL      0020000                         /* Receive status: buffer is partial */
#define RS_ETC_RS       0014000                         /* Receive status: terminated by RS */
#define RS_ETC_DC2      0010000                         /* Receive status: terminated by DC2 */
#define RS_ETC_CR       0004000                         /* Receive status: terminated by CR */
#define RS_ETC_EOT      0000000                         /* Receive status: terminated by EOT */
#define RS_CHAR_COUNT   0003777                         /* Receive status: character count */

#define WR_NO_ENQACK    0020000                         /* Write: no ENQ/ACK this xfer */
#define WR_ADD_CRLF     0010000                         /* Write: add CR/LF if not '_' */
#define WR_PARTIAL      0004000                         /* Write: write is partial */
#define WR_LENGTH       0003777                         /* Write: write length in bytes */

#define RT_END_ON_CR    0000200                         /* Receive type: end xfer on CR */
#define RT_END_ON_RS    0000100                         /* Receive type: end xfer on RS */
#define RT_END_ON_EOT   0000040                         /* Receive type: end xfer on EOT */
#define RT_END_ON_DC2   0000020                         /* Receive type: end xfer on DC2 */
#define RT_END_ON_CNT   0000010                         /* Receive type: end xfer on count */
#define RT_END_ON_CHAR  0000004                         /* Receive type: end xfer on character */
#define RT_ENAB_EDIT    0000002                         /* Receive type: enable input editing */
#define RT_ENAB_ECHO    0000001                         /* Receive type: enable input echoing */

#define FC_FORCE_XON    0000002                         /* Flow control: force XON */
#define FC_XONXOFF      0000001                         /* Flow control: enable XON/XOFF */

#define CL_GUARD        0000040                         /* Connect line: guard tone off or on */
#define CL_STANDARD     0000020                         /* Connect line: standard 212 or V.22 */
#define CL_BITS         0000010                         /* Connect line: bits 10 or 9 */
#define CL_MODE         0000004                         /* Connect line: mode originate or answer */
#define CL_DIAL         0000002                         /* Connect line: dial manual or automatic */
#define CL_SPEED        0000001                         /* Connect line: speed low or high */

#define DL_AUTO_ANSWER  0000001                         /* Disconnect line: auto-answer enable or disable */

#define LB_SPEED        0000004                         /* Loopback test: speed low or high */
#define LB_MODE         0000002                         /* Loopback test: mode analog or digital */
#define LB_TEST         0000001                         /* Loopback test: test disable or enable */

#define GS_NO_SYSMDM    0000200                         /* Get status: systems modem present or absent */
#define GS_SYSMDM_TO    0000100                         /* Get status: systems modem OK or timed out */
#define GS_NO_MODEM     0000040                         /* Get status: modem present or absent */
#define GS_SPEED        0000020                         /* Get status: speed low or high */
#define GS_LINE         0000001                         /* Get status: line disconnected or connected */


/* Bit fields (name, starting bit, bit width) */

BITFIELD (CMD_OPCODE,    8,  8)                         /* Command: opcode */
BITFIELD (CMD_KEY,       0,  8)                         /* Command: key */

BITFIELD (SK_BPC,       14,  2)                         /* Set key: bits per character */
BITFIELD (SK_MODEM,     13,  1)                         /* Set key: hardwired or modem */
BITFIELD (SK_BRG,       12,  1)                         /* Set key: baud rate generator 0/1 */
BITFIELD (SK_STOPBITS,  10,  2)                         /* Set key: stop bits */
BITFIELD (SK_PARITY,     8,  2)                         /* Set key: parity select */
BITFIELD (SK_ENQACK,     7,  1)                         /* Set key: disable or enable ENQ/ACK */
BITFIELD (SK_BAUDRATE,   3,  4)                         /* Set key: port baud rate */
BITFIELD (SK_PORT,       0,  3)                         /* Set key: port number */

BITFIELD (FL_ALERT,     11,  1)                         /* Port flags: alert for terminate recv buffer */
BITFIELD (FL_XOFF,      10,  1)                         /* Port flags: XOFF stopped transmission */
BITFIELD (FL_BREAK,      9,  1)                         /* Port flags: UI / break detected */
BITFIELD (FL_HAVEBUF,    8,  1)                         /* Port flags: UI / read buffer available */
BITFIELD (FL_WANTBUF,    7,  1)                         /* Port flags: UI / write buffer available */
BITFIELD (FL_RDOVFLOW,   6,  1)                         /* Port flags: read buffers overflowed */
BITFIELD (FL_RDFILL,     5,  1)                         /* Port flags: read buffer is filling */
BITFIELD (FL_RDEMPT,     4,  1)                         /* Port flags: read buffer is emptying */
BITFIELD (FL_WRFILL,     3,  1)                         /* Port flags: write buffer is filling */
BITFIELD (FL_WREMPT,     2,  1)                         /* Port flags: write buffer is emptying */
BITFIELD (FL_WAITACK,    1,  1)                         /* Port flags: ENQ sent, waiting for ACK */
BITFIELD (FL_DO_ENQACK,  0,  1)                         /* Port flags: do ENQ/ACK handshake */

#define SK_BRG_1        SK_BRG
#define SK_BRG_0        0

#define FL_RDFLAGS      (FL_RDEMPT | FL_RDFILL | FL_RDOVFLOW)
#define FL_WRFLAGS      (FL_WREMPT | FL_WRFILL)
#define FL_UI_PENDING   (FL_WANTBUF | FL_HAVEBUF | FL_BREAK)

#define ACK_LIMIT       1000                            /* poll timeout for ACK response */
#define ENQ_LIMIT         80                            /* output chars before ENQ */


/* Packed field values */

#define SK_BPC_5        (0 << SK_BPC_V)
#define SK_BPC_6        (1 << SK_BPC_V)
#define SK_BPC_7        (2 << SK_BPC_V)
#define SK_BPC_8        (3 << SK_BPC_V)

#define SK_STOP_1       (1 << SK_STOPBITS_V)
#define SK_STOP_15      (2 << SK_STOPBITS_V)
#define SK_STOP_2       (3 << SK_STOPBITS_V)

#define SK_BAUD_NOCHG   (0 << SK_BAUDRATE_V)
#define SK_BAUD_50      (1 << SK_BAUDRATE_V)
#define SK_BAUD_75      (2 << SK_BAUDRATE_V)
#define SK_BAUD_110     (3 << SK_BAUDRATE_V)
#define SK_BAUD_1345    (4 << SK_BAUDRATE_V)
#define SK_BAUD_150     (5 << SK_BAUDRATE_V)
#define SK_BAUD_300     (6 << SK_BAUDRATE_V)
#define SK_BAUD_1200    (7 << SK_BAUDRATE_V)
#define SK_BAUD_1800    (8 << SK_BAUDRATE_V)
#define SK_BAUD_2400    (9 << SK_BAUDRATE_V)
#define SK_BAUD_4800    (10 << SK_BAUDRATE_V)
#define SK_BAUD_9600    (11 << SK_BAUDRATE_V)
#define SK_BAUD_19200   (12 << SK_BAUDRATE_V)


/* Default values */

#define SK_PWRUP_0      (SK_BPC_8 | SK_BRG_0 | SK_STOP_1 | SK_BAUD_9600)
#define SK_PWRUP_1      (SK_BPC_8 | SK_BRG_1 | SK_STOP_1 | SK_BAUD_9600)

#define RT_PWRUP        (RT_END_ON_CR | RT_END_ON_CHAR | RT_ENAB_EDIT | RT_ENAB_ECHO)


/* Command helpers */

#define GET_OPCODE(w)   (((w) & CMD_OPCODE)  >> CMD_OPCODE_V)
#define GET_KEY(w)      (((w) & CMD_KEY)     >> CMD_KEY_V)
#define GET_BPC(w)      (((w) & SK_BPC)      >> SK_BPC_V)
#define GET_BAUDRATE(w) (((w) & SK_BAUDRATE) >> SK_BAUDRATE_V)
#define GET_PORT(w)     (((w) & SK_PORT)     >> SK_PORT_V)
#define GET_UIREASON(w) (((w) & UI_REASON)   >> UI_REASON_V)
#define GET_UIPORT(w)   (((w) & UI_PORT)     >> UI_PORT_V)


/* Multiplexer controller state variables */

typedef enum {                                          /* execution state */
    idle,
    cmd,
    param,
    exec
    } STATE;

STATE  mpx_state = idle;                                /* controller state */

uint16 mpx_ibuf = 0;                                    /* status/data in */
uint16 mpx_obuf = 0;                                    /* command/data out */

uint32 mpx_cmd = 0;                                     /* current command */
uint32 mpx_param = 0;                                   /* current parameter */
uint32 mpx_port = 0;                                    /* current port number for R/W */
uint32 mpx_portkey = 0;                                 /* current port's key */
 int32 mpx_iolen = 0;                                   /* length of current I/O xfer */

t_bool mpx_uien = FALSE;                                /* unsolicited interrupts enabled */
uint32 mpx_uicode = 0;                                  /* unsolicited interrupt reason and port */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } mpx = { CLEAR, CLEAR, CLEAR };

/* Multiplexer per-line state variables */

uint8  mpx_key      [MPX_PORTS];                        /* port keys */
uint16 mpx_config   [MPX_PORTS];                        /* port configuration */
uint16 mpx_rcvtype  [MPX_PORTS];                        /* receive type */
uint16 mpx_charcnt  [MPX_PORTS];                        /* current character count */
uint16 mpx_termcnt  [MPX_PORTS];                        /* termination character count */
uint16 mpx_flowcntl [MPX_PORTS];                        /* flow control */
uint8  mpx_enq_cntr [MPX_PORTS];                        /* ENQ character counter */
uint16 mpx_ack_wait [MPX_PORTS];                        /* ACK wait timer */
uint16 mpx_flags    [MPX_PORTS];                        /* line state flags */

/* Multiplexer buffer selectors */

typedef enum { ioread, iowrite } IO_OPER;               /* I/O operation */
typedef enum { get, put } BUF_SELECT;                   /* buffer selector */

static const char *const io_op [] = { "read",           /* operation names */
                                      "write" };

static const uint16 buf_size [] = { RD_BUF_SIZE,        /* buffer sizes */
                                    WR_BUF_SIZE };

static uint32 emptying_flags [2];                       /* buffer emptying flags [IO_OPER] */
static uint32 filling_flags  [2];                       /* buffer filling  flags [IO_OPER] */


/* Multiplexer per-line buffer variables */

typedef uint16 BUF_INDEX [MPX_PORTS] [2];               /* buffer index (read and write) */

BUF_INDEX mpx_put;                                      /* read/write buffer add index */
BUF_INDEX mpx_sep;                                      /* read/write buffer separator index */
BUF_INDEX mpx_get;                                      /* read/write buffer remove index */

uint8 mpx_rbuf [MPX_PORTS] [RD_BUF_SIZE];               /* read buffer */
uint8 mpx_wbuf [MPX_PORTS] [WR_BUF_SIZE];               /* write buffer */


/* Multiplexer local routines */

static t_bool exec_command     (void);
static void   poll_connection  (void);
static void   controller_reset (void);
static uint32 service_time     (uint16 control_word);
static int32  key_to_port      (uint32 key);

static void   buf_init   (IO_OPER rw, uint32 port);
static uint8  buf_get    (IO_OPER rw, uint32 port);
static void   buf_put    (IO_OPER rw, uint32 port, uint8 ch);
static void   buf_remove (IO_OPER rw, uint32 port);
static void   buf_term   (IO_OPER rw, uint32 port, uint8 header);
static void   buf_free   (IO_OPER rw, uint32 port);
static void   buf_cancel (IO_OPER rw, uint32 port, BUF_SELECT which);
static uint16 buf_len    (IO_OPER rw, uint32 port, BUF_SELECT which);
static uint32 buf_avail  (IO_OPER rw, uint32 port);


/* Multiplexer global routines */

IOHANDLER mpx_io;

t_stat mpx_line_svc  (UNIT   *uptr);
t_stat mpx_cntl_svc  (UNIT   *uptr);
t_stat mpx_poll_svc  (UNIT   *uptr);
t_stat mpx_reset     (DEVICE *dptr);
t_stat mpx_attach    (UNIT   *uptr, CONST char *cptr);
t_stat mpx_detach    (UNIT   *uptr);
t_stat mpx_status    (FILE   *st,   UNIT  *uptr, int32 val,        CONST void *desc);
t_stat mpx_set_frev  (UNIT   *uptr, int32  val,  CONST char *cptr, void *desc);
t_stat mpx_show_frev (FILE   *st,   UNIT  *uptr, int32 val,        CONST void *desc);


/* MPX data structures.

   mpx_order    MPX line connection order table
   mpx_ldsc     MPX terminal multiplexer line descriptors
   mpx_desc     MPX terminal multiplexer device descriptor
   mpx_dib      MPX device information block
   mpx_unit     MPX unit list
   mpx_reg      MPX register list
   mpx_mod      MPX modifier list
   mpx_deb      MPX debug list
   mpx_dev      MPX device descriptor

   The first eight units correspond to the eight multiplexer line ports.  These
   handle character I/O via the Telnet library.  A ninth unit acts as the card
   controller, executing commands and transferring data to and from the I/O
   buffers.  A tenth unit is responsible for polling for connections and socket
   I/O.  It also holds the master socket.

   The character I/O service routines run only when there are characters to read
   or write.  They operate at the approximate baud rates of the terminals (in
   CPU instructions per second) in order to be compatible with the OS drivers.
   The controller service routine runs only when a command is executing or a
   data transfer to or from the CPU is in progress.  The Telnet poll must run
   continuously, but it may operate much more slowly, as the only requirement is
   that it must not present a perceptible lag to human input.  To be compatible
   with CPU idling, it is co-scheduled with the master poll timer, which uses a
   ten millisecond period.

   The controller and poll units are hidden by disabling them, so as to present
   a logical picture of the multiplexer to the user.
*/

DEVICE mpx_dev;

int32 mpx_order [MPX_PORTS] = { -1 };                       /* connection order */
TMLN  mpx_ldsc [MPX_PORTS] = { { 0 } };                     /* line descriptors */
TMXR  mpx_desc = { MPX_PORTS, 0, 0, mpx_ldsc, mpx_order };  /* device descriptor */

DIB mpx_dib = { &mpx_io, MPX };

UNIT mpx_unit [] = {
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 0 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 1 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 2 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 3 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 4 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 5 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 6 */
    { UDATA (&mpx_line_svc, UNIT_FASTTIME, 0) },                    /* terminal I/O line 7 */
    { UDATA (&mpx_cntl_svc, UNIT_DIS, 0) },                         /* controller unit */
    { UDATA (&mpx_poll_svc, UNIT_ATTABLE | UNIT_DIS, POLL_FIRST) }  /* Telnet poll unit */
    };

REG mpx_reg [] = {
    { DRDATA (STATE,   mpx_state,    3) },
    { ORDATA (IBUF,    mpx_ibuf,    16), REG_FIT },
    { ORDATA (OBUF,    mpx_obuf,    16), REG_FIT },

    { ORDATA (CMD,     mpx_cmd,      8) },
    { ORDATA (PARAM,   mpx_param,   16) },

    { DRDATA (PORT,    mpx_port,     8), PV_LEFT },
    { DRDATA (PORTKEY, mpx_portkey,  8), PV_LEFT },
    { DRDATA (IOLEN,   mpx_iolen,   16), PV_LEFT },

    { FLDATA (UIEN,    mpx_uien,     0) },
    { GRDATA (UIPORT,  mpx_uicode,  10, 3, 0)  },
    { GRDATA (UICODE,  mpx_uicode,  10, 3, UI_REASON_V) },

    { BRDATA (KEYS,     mpx_key,      10,  8, MPX_PORTS) },
    { BRDATA (PCONFIG,  mpx_config,    8, 16, MPX_PORTS) },
    { BRDATA (RCVTYPE,  mpx_rcvtype,   8, 16, MPX_PORTS) },
    { BRDATA (CHARCNT,  mpx_charcnt,   8, 16, MPX_PORTS) },
    { BRDATA (TERMCNT,  mpx_termcnt,   8, 16, MPX_PORTS) },
    { BRDATA (FLOWCNTL, mpx_flowcntl,  8, 16, MPX_PORTS) },

    { BRDATA (ENQCNTR, mpx_enq_cntr, 10,  7, MPX_PORTS) },
    { BRDATA (ACKWAIT, mpx_ack_wait, 10, 10, MPX_PORTS) },
    { BRDATA (PFLAGS,  mpx_flags,     2, 12, MPX_PORTS) },

    { BRDATA (RBUF, mpx_rbuf,  8,  8, MPX_PORTS * RD_BUF_SIZE) },
    { BRDATA (WBUF, mpx_wbuf,  8,  8, MPX_PORTS * WR_BUF_SIZE) },

    { BRDATA (GET, mpx_get,   10, 10, MPX_PORTS * 2) },
    { BRDATA (SEP, mpx_sep,   10, 10, MPX_PORTS * 2) },
    { BRDATA (PUT, mpx_put,   10, 10, MPX_PORTS * 2) },

    { FLDATA (CTL,   mpx.control,         0)  },
    { FLDATA (FLG,   mpx.flag,            0)  },
    { FLDATA (FBF,   mpx.flagbuf,         0)  },
    { ORDATA (SC,    mpx_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, mpx_dib.select_code, 6), REG_HRO },

    { BRDATA (CONNORD, mpx_order, 10, 32, MPX_PORTS), REG_HRO },
    { NULL }
    };

MTAB mpx_mod [] = {
    { UNIT_FASTTIME, UNIT_FASTTIME, "fast timing",      "FASTTIME",   NULL, NULL, NULL },
    { UNIT_FASTTIME,             0, "realistic timing", "REALTIME",   NULL, NULL, NULL },

    { UNIT_CAPSLOCK, UNIT_CAPSLOCK, "CAPS LOCK down",   "CAPSLOCK",   NULL, NULL, NULL },
    { UNIT_CAPSLOCK,             0, "CAPS LOCK up",     "NOCAPSLOCK", NULL, NULL, NULL },

    { MTAB_XTD | MTAB_VDV,            0, "REV",       NULL,        &mpx_set_frev,     &mpx_show_frev,     NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "LINEORDER", "LINEORDER", &tmxr_set_lnorder, &tmxr_show_lnorder, &mpx_desc },

    { MTAB_XTD | MTAB_VUN | MTAB_NC,  0, "LOG", "LOG",   &tmxr_set_log,   &tmxr_show_log, &mpx_desc },
    { MTAB_XTD | MTAB_VUN | MTAB_NC,  0, NULL,  "NOLOG", &tmxr_set_nolog, NULL,           &mpx_desc },

    { MTAB_XTD | MTAB_VDV,            0, "",            NULL,         NULL,        &mpx_status,      &mpx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,         NULL,        &tmxr_show_cstat, &mpx_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS",  NULL,         NULL,        &tmxr_show_cstat, &mpx_desc },
    { MTAB_XTD | MTAB_VDV,            1, NULL,          "DISCONNECT", &tmxr_dscln, NULL,             &mpx_desc },
    { MTAB_XTD | MTAB_VDV,            0, "SC",          "SC",         &hp_setsc,   &hp_showsc,       &mpx_dev  },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO",       "DEVNO",      &hp_setdev,  &hp_showdev,      &mpx_dev  },

    { 0 }
    };

DEBTAB mpx_deb [] = {
    { "CMDS", DEB_CMDS },
    { "CPU",  DEB_CPU },
    { "BUF",  DEB_BUF },
    { "XFER", DEB_XFER },
    { NULL,   0 }
    };

DEVICE mpx_dev = {
    "MPX",                                  /* device name */
    mpx_unit,                               /* unit array */
    mpx_reg,                                /* register array */
    mpx_mod,                                /* modifier array */
    MPX_PORTS + MPX_CNTLS,                  /* number of units */
    10,                                     /* address radix */
    31,                                     /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    8,                                      /* data width */
    &tmxr_ex,                               /* examine routine */
    &tmxr_dep,                              /* deposit routine */
    &mpx_reset,                             /* reset routine */
    NULL,                                   /* boot routine */
    &mpx_attach,                            /* attach routine */
    &mpx_detach,                            /* detach routine */
    &mpx_dib,                               /* device information block */
    DEV_DEBUG | DEV_DISABLE | DEV_MUX,      /* device flags */
    0,                                      /* debug control flags */
    mpx_deb,                                /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL,                                   /* logical device name */
    NULL,                                   /* help routine */
    NULL,                                   /* help attach routine*/
    (void *) &mpx_desc                      /* help context */
    };


/* I/O signal handler.

   Commands are sent to the card via an OTA/B.  Issuing an STC SC,C causes the
   mux to accept the word (STC causes a NMI on the card).  If the command uses
   one word, command execution will commence, and the flag will set on
   completion.  If the command uses two words, the flag will be set, indicating
   that the second word should be output via an OTA/B.  Command execution will
   commence upon receipt, and the flag will set on completion.

   When the flag sets for command completion, status or data may be read from
   the card via an LIA/B.  If additional status or data words are expected, the
   flag will set when they are available.

   A command consists of an opcode in the high byte, and a port key or command
   parameter in the low byte.  Undefined commands are treated as NOPs.

   The card firmware executes commands as part of a twelve-event round-robin
   scheduling poll.  The card NMI service routine simply sets a flag that is
   interrogated during polling.  The poll sequence is advanced after each
   command.  This implies that successive commands incur a delay of at least one
   poll-loop's execution time.  On an otherwise quiescent card, this delay is
   approximately 460 Z80 instructions, or about 950 usec.  The average command
   initiation time is half of that, or roughly 425 usec.

   If a detected command requires a second word, the card sits in a tight loop,
   waiting for the OTx that indicates that the parameter is available.  Command
   initiation from parameter receipt is about 25 usec.

   For reads and writes to card buffers, the on-board DMA controller is used.
   The CPU uses DCPC to handle the transfer, but the data transfer time is
   limited by the Z80 DMA, which can process a word in about 1.25 usec.

   For most cards, the hardware POPIO signal sets the flag buffer and flag
   flip-flops, while CRS clears the control flip-flop.  For this card, the
   control and flags are cleared together by CRS, and POPIO is not used.

   Implementation notes:

    1. "Enable unsolicited input" is the only command that does not set the
       device flag upon completion.  Therefore, the CPU has no way of knowing
       when the command has completed.  Because the command in the input latch
       is recorded in the NMI handler, but actual execution only begins when the
       scheduler polls for the command indication, it is possible for another
       command to be sent to the card before the "Enable unsolicited input"
       command is recognized.  In this case, the second command overwrites the
       first and is executed by the scheduler poll.  Under simulation, this
       condition occurs when the OTx and STC processors are entered with
       mpx_state = cmd.

    2. The "Fast binary read" command inhibits all other commands until the card
       is reset.
*/

uint32 mpx_io (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
static const char *output_state [] = { "Command", "Command override", "Parameter", "Data" };
static const char *input_state  [] = { "Status",  "Invalid status",   "Parameter", "Data" };
const char *hold_or_clear = (signal_set & ioCLF ? ",C" : "");
int32 delay;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            mpx.flag = mpx.flagbuf = CLEAR;             /* clear flag and flag buffer */

            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                fputs (">>MPX cmds: [CLF] Flag cleared\n", sim_deb);
            break;


        case ioSTF:                                     /* set flag flip-flop */
            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                fputs (">>MPX cmds: [STF] Flag set\n", sim_deb);
                                                        /* fall into ENF */

        case ioENF:                                     /* enable flag */
            mpx.flag = mpx.flagbuf = SET;               /* set flag and flag buffer */
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (mpx);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (mpx);
            break;


        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, mpx_ibuf);   /* return info */

            if (DEBUG_PRI (mpx_dev, DEB_CPU))
                fprintf (sim_deb, ">>MPX cpu:  [LIx%s] %s = %06o\n",
                                  hold_or_clear, input_state [mpx_state], mpx_ibuf);

            if (mpx_state == exec)                      /* if this is input data word */
                sim_activate (&mpx_cntl, DATA_DELAY);   /*   continue transmission */
            break;


        case ioIOO:                                     /* I/O data output */
            mpx_obuf = IODATA (stat_data);              /* save word */

            if (DEBUG_PRI (mpx_dev, DEB_CPU))
                fprintf (sim_deb, ">>MPX cpu:  [OTx%s] %s = %06o\n",
                                  hold_or_clear, output_state [mpx_state], mpx_obuf);

            if (mpx_state == param) {                   /* if this is parameter word */
                sim_activate (&mpx_cntl, CMD_DELAY);    /*   do command now */

                if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                    fprintf (sim_deb, ">>MPX cmds: [OTx%s] Command %03o parameter %06o scheduled, "
                                      "time = %d\n", hold_or_clear, mpx_cmd, mpx_obuf, CMD_DELAY);
                }

            else if (mpx_state == exec)                 /* else if this is output data word */
                sim_activate (&mpx_cntl, DATA_DELAY);   /*   then do transmission */
            break;


        case ioCRS:                                     /* control reset */
            controller_reset ();                        /* reset firmware to power-on defaults */
            mpx_obuf = 0;                               /* clear output buffer */

            mpx.control = CLEAR;                        /* clear control */
            mpx.flagbuf = CLEAR;                        /* clear flag buffer */
            mpx.flag    = CLEAR;                        /* clear flag */

            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                fputs (">>MPX cmds: [CRS] Controller reset\n", sim_deb);
            break;


        case ioCLC:                                     /* clear control flip-flop */
            mpx.control = CLEAR;                        /* clear control */

            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                fprintf (sim_deb, ">>MPX cmds: [CLC%s] Control cleared\n", hold_or_clear);
            break;


        case ioSTC:                                     /* set control flip-flop */
            mpx.control = SET;                          /* set control */

            if (mpx_cmd == CMD_BINARY_READ)             /* executing fast binary read? */
                break;                                  /* further command execution inhibited */

            mpx_cmd = GET_OPCODE (mpx_obuf);            /* get command opcode */
            mpx_portkey = GET_KEY (mpx_obuf);           /* get port key */

            if (mpx_state == cmd)                       /* already scheduled? */
                sim_cancel (&mpx_cntl);                 /* cancel to get full delay */

            mpx_state = cmd;                            /* set command state */

            if (mpx_cmd & CMD_TWO_WORDS)                /* two-word command? */
                delay = PARAM_DELAY;                    /* specify parameter wait */
            else                                        /* one-word command */
                delay = CMD_DELAY;                      /* specify command wait */

            sim_activate (&mpx_cntl, delay);            /* schedule command */

            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                fprintf (sim_deb, ">>MPX cmds: [STC%s] Command %03o key %d scheduled, "
                                  "time = %d\n", hold_or_clear, mpx_cmd, mpx_portkey, delay);
            break;


        case ioEDT:                                     /* end data transfer */
            if (DEBUG_PRI (mpx_dev, DEB_CPU))
                fputs (">>MPX cpu:  [EDT] DCPC transfer ended\n", sim_deb);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (mpx);                            /* set standard PRL signal */
            setstdIRQ (mpx);                            /* set standard IRQ signal */
            setstdSRQ (mpx);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            mpx.flagbuf = CLEAR;                        /* clear flag buffer */
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Command executor.

   We are called by the controller service routine to process one- and two-word
   commands.  For two-word commands, the parameter word is present in mpx_param.
   The return value indicates whether the card flag should be set upon
   completion.

   Most commands execute and complete directly.  The read and write commands,
   however, transition to the execution state to simulate the DMA transfer, and
   the "Download executable" command does the same to receive the download from
   the CPU.

   Several commands were added for the B firmware revision, and the various
   revisions of the RTE drivers sent some commands that were never implemented
   in the mux firmware.  The command protocol treated unknown commands as NOPs,
   meaning that the command (and parameter, if it was a two-word command) was
   absorbed and the card flag was set as though the command completed normally.
   This allowed interoperability between firmware and driver revisions.

   Commands that refer to ports do so indirectly by passing a port key, rather
   than a port number.  The key-to-port translation is established by the "Set
   port key" command.  If a key is not found in the table, the command is not
   executed, and the status return is ST_BAD_KEY, which in hex is "BAD0".

   Implementation notes:

    1. The "Reset to power-on defaults" command causes the firmware to disable
       interrupts and jump to the power-on initialization routine, exactly as
       though the Z80 had received a hardware reset.

    2. The "Abort DMA transfer" command works because STC causes NMI, so the
       command is executed even in the middle of a DMA transfer.  The OTx of the
       command will be sent to the buffer if a "Write data to buffer" command is
       in progress, but the STC will cause this routine to be called, which will
       cancel the buffer and return the controller to the idle state.  Note that
       this command might be sent with no transfer in progress, in which case
       nothing is done.

    3. In response to an "Enable unsolicited interrupts" command, the controller
       service is scheduled to check for a pending UI.  If one is found, the
       first UI status word is placed in the input buffer, and an interrupt is
       generated by setting the flag.  This causes entry to the driver, which
       issues an "Acknowledge" command to obtain the second status word.

       It is possible, however, for the interrupt to be ignored.  For example,
       the driver may be waiting for a "write buffer available" UI when it is
       called to begin a write to a different port.  If the flag is set by
       the UI after RTE has been entered, the interrupt will be held off, and
       the STC sc,C instruction that begins the command sequence will clear the
       flag, removing the interrupt entirely.  In this case, the controller will
       reissue the UI when the next "Enable unsolicited interrupts" command is
       sent.

       Note that the firmware reissues the same UI, rather than recomputing UIs
       and potentially selecting a different one of higher priority.

    4. The "Fast binary read" command apparently was intended to facilitate
       booting from a 264x tape drive, although no boot loader ROM for the
       multiplexer was ever released.  It sends the fast binary read escape
       sequence (ESC e) to the terminal and then packs each pair of characters
       received into a word and sends it to the CPU, accompanied by the device
       flag.

       The multiplexer firmware disables interrupts and then manipulates the SIO
       for port 0 directly.  Significantly, it does no interpretation of the
       incoming data and sits in an endless I/O loop, so the only way to exit
       the command is to reset the card with a CRS (front panel PRESET or CLC 0
       instruction execution).  Sending a command will not work; although the
       NMI will interrupt the fast binary read, the NMI handler simply sets a
       flag that is tested by the scheduler poll.  Because the processor is in
       an endless loop, control never returns to the scheduler, so the command
       is never seen.

    5. The "Terminate active receive buffer" behavior is a bit tricky.  If the
       read buffer has characters, the buffer is terminated as though a
       "terminate on count" condition occurred.  If the buffer is empty,
       however, a "terminate on count = 1" condition is established.  When a
       character is received, the buffer is terminated, and the buffer
       termination count is reset to 254.
*/

static t_bool exec_command (void)
{
int32 port;
uint32 svc_time;
t_bool set_flag = TRUE;                                 /* flag is normally set on completion */
STATE next_state = idle;                                /* command normally executes to completion */

mpx_ibuf = ST_OK;                                       /* return status is normally OK */

switch (mpx_cmd) {

    case CMD_NOP:                                       /* no operation */
        break;                                          /* just ignore */


    case CMD_RESET:                                     /* reset firmware */
        controller_reset ();                            /* reset program variables */
        mpx_ibuf = ST_TEST_OK;                          /* return self-test OK code */
        break;


    case CMD_ENABLE_UI:
        mpx_uien = TRUE;                                /* enable unsolicited interrupts */
        sim_activate (&mpx_cntl, CMD_DELAY);            /*   and schedule controller for UI check */

        if (DEBUG_PRI (mpx_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MPX cmds: Controller status check scheduled, "
                              "time = %d\n", CMD_DELAY);

        set_flag = FALSE;                               /* do not set the flag at completion */
        break;


    case CMD_DISABLE:
        switch (mpx_portkey) {
            case SUBCMD_UI:
                mpx_uien = FALSE;                           /* disable unsolicited interrupts */
                break;

            case SUBCMD_DMA:
                if (mpx_flags [mpx_port] & FL_WRFILL)       /* write buffer xfer in progress? */
                    buf_cancel (iowrite, mpx_port, put);    /* cancel it */
                else if (mpx_flags [mpx_port] & FL_RDEMPT)  /* read buffer xfer in progress? */
                    buf_cancel (ioread, mpx_port, get);     /* cancel it */
                break;
            }
        break;


    case CMD_ACK:                                               /* acknowledge unsolicited interrupt */
        switch (mpx_uicode & UI_REASON) {

            case UI_WRBUF_AVAIL:                                /* write buffer notification */
                mpx_flags [mpx_port] &= ~FL_WANTBUF;            /* clear flag */
                mpx_ibuf = WR_BUF_LIMIT;                        /* report write buffer available */
                break;

            case UI_RDBUF_AVAIL:                                /* read buffer notification */
                mpx_flags [mpx_port] &= ~FL_HAVEBUF;            /* clear flag */

                mpx_ibuf = (uint16) (buf_get (ioread, mpx_port) << 8 |  /* get header value and position */
                                     buf_len (ioread, mpx_port, get));  /*   and include buffer length */

                if (mpx_flags [mpx_port] & FL_RDOVFLOW) {       /* did a buffer overflow? */
                    mpx_ibuf = mpx_ibuf | RS_OVERFLOW;          /* report it */
                    mpx_flags [mpx_port] &= ~FL_RDOVFLOW;       /* clear overflow flag */
                    }
                break;

            case UI_BRK_RECD:                                   /* break received */
                mpx_flags [mpx_port] &= ~FL_BREAK;              /* clear flag */
                mpx_ibuf = 0;                                   /* 2nd word is zero */
                break;
            }

        mpx_uicode = 0;                                         /* clear notification code */
        break;


    case CMD_CANCEL:                                    /* cancel first read buffer */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0)                                  /* port defined? */
            buf_cancel (ioread, port, get);             /* cancel get buffer */

            if (buf_avail (ioread, port) == 2)          /* if all buffers are now clear */
                mpx_charcnt [port] = 0;                 /*   then clear the current character count */

            else if (!(mpx_flags [port] & FL_RDFILL))   /* otherwise if the other buffer is not filling */
                mpx_flags [port] |= FL_HAVEBUF;         /*   then indicate buffer availability */
        break;


    case CMD_CANCEL_ALL:                                /* cancel all read buffers */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0) {                                /* port defined? */
            buf_init (ioread, port);                    /* reinitialize read buffers */
            mpx_charcnt [port] = 0;                     /*   and clear the current character count */
            }
        break;


    case CMD_BINARY_READ:                               /* fast binary read */
        for (port = 0; port < MPX_PORTS; port++)
            sim_cancel (&mpx_unit [port]);              /* cancel I/O on all lines */

        mpx_flags [0] = 0;                              /* clear port 0 state flags */
        mpx_enq_cntr [0] = 0;                           /* clear port 0 ENQ counter */
        mpx_ack_wait [0] = 0;                           /* clear port 0 ACK wait timer */

        tmxr_putc_ln (&mpx_ldsc [0], ESC);              /* send fast binary read */
        tmxr_putc_ln (&mpx_ldsc [0], 'e');              /*   escape sequence to port 0 */
        tmxr_poll_tx (&mpx_desc);                       /* flush output */

        next_state = exec;                              /* set execution state */
        break;


    case CMD_REQ_WRITE:                                 /* request write buffer */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0)                                  /* port defined? */
            if (buf_avail (iowrite, port) > 0)          /* is a buffer available? */
                mpx_ibuf = WR_BUF_LIMIT;                /* report write buffer limit */

            else {
                mpx_ibuf = 0;                           /* report none available */
                mpx_flags [port] |= FL_WANTBUF;         /* set buffer request */
                }
        break;


    case CMD_WRITE:                                     /* write to buffer */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0) {                                /* port defined? */
            mpx_port = port;                            /* save port number */
            mpx_iolen = mpx_param & WR_LENGTH;          /* save request length */
            next_state = exec;                          /* set execution state */
            }
        break;


    case CMD_SET_KEY:                                   /* set port key and configuration */
        port = GET_PORT (mpx_param);                    /* get target port number */
        mpx_key [port] = (uint8) mpx_portkey;           /* set port key */
        mpx_config [port] = (uint16) mpx_param;         /* set port configuration word */

        svc_time = service_time (mpx_config [port]);    /* get service time for baud rate */

        if (svc_time)                                   /* want to change? */
            mpx_unit [port].wait = svc_time;            /* set service time */

        mpx_ibuf = MPX_DATE_CODE;                       /* return firmware date code */
        break;


    case CMD_SET_RCV:                                   /* set receive type */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0)                                  /* port defined? */
            mpx_rcvtype [port] = (uint16) mpx_param;    /* save port receive type */
        break;


    case CMD_SET_COUNT:                                 /* set character count */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0) {                                /* port defined? */
            mpx_termcnt [port] = (uint16) mpx_param;    /* save port termination character count */
            mpx_charcnt [port] = 0;                     /*   and clear the current character count */
            }
        break;


    case CMD_SET_FLOW:                                      /* set flow control */
        port = key_to_port (mpx_portkey);                   /* get port */

        if (port >= 0)                                      /* port defined? */
            mpx_flowcntl [port] = mpx_param & FC_XONXOFF;   /* save port flow control */

            if (mpx_param & FC_FORCE_XON)                   /* force XON? */
                mpx_flags [port] &= ~FL_XOFF;               /* resume transmission if suspended */
        break;


    case CMD_READ:                                      /* read from buffer */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0) {                                /* port defined? */
            mpx_port = port;                            /* save port number */
            mpx_iolen = mpx_param;                      /* save request length */

            sim_activate (&mpx_cntl, DATA_DELAY);       /* schedule the transfer */
            next_state = exec;                          /* set execution state */
            set_flag = FALSE;                           /* no flag until word ready */
            }
        break;


    case CMD_DL_EXEC:                                   /* Download executable */
        mpx_iolen = mpx_param;                          /* save request length */
        next_state = exec;                              /* set execution state */
        break;


    case CMD_CN_LINE:                                   /* connect modem line */
    case CMD_DC_LINE:                                   /* disconnect modem line */
    case CMD_LOOPBACK:                                  /* enable/disable modem loopback */
        mpx_ibuf = ST_NO_MODEM;                         /* report "no modem installed" */
        break;


    case CMD_GET_STATUS:                                /* get modem status */
        mpx_ibuf = ST_NO_SYSMDM;                        /* report "no systems modem card" */
        break;


    case CMD_TERM_BUF:                                  /* terminate active receive buffer */
        port = key_to_port (mpx_portkey);               /* get port */

        if (port >= 0)                                  /* port defined? */
            if (buf_len (ioread, port, put) > 0) {      /* any chars in buffer? */
                buf_term (ioread, port, 0);             /* terminate buffer and set header */
                mpx_charcnt [port] = 0;                 /*   then clear the current character count */

                if (buf_avail (ioread, port) == 1)      /* first read buffer? */
                    mpx_flags [port] |= FL_HAVEBUF;     /* indicate availability */
                }

            else {                                      /* buffer is empty */
                mpx_termcnt [port] = 1;                 /* set to terminate on one char */
                mpx_flags [port] |= FL_ALERT;           /* set alert flag */
                }
        break;


    case CMD_VCP_PUT:                                   /* VCP put byte */
    case CMD_VCP_PUT_BUF:                               /* VCP put buffer */
    case CMD_VCP_GET:                                   /* VCP get byte */
    case CMD_VCP_GET_BUF:                               /* VCP get buffer */
    case CMD_VCP_EXIT:                                  /* Exit VCP mode */
    case CMD_VCP_ENTER:                                 /* Enter VCP mode */

    default:                                            /* unknown command */
        if (DEBUG_PRI (mpx_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MPX cmds: Unknown command %03o ignored\n", mpx_cmd);
    }

mpx_state = next_state;
return set_flag;
}


/* Multiplexer controller service.

   The controller service handles commands and data transfers to and from the
   CPU.  The delay in scheduling the controller service represents the firmware
   command or data execution time.  The controller may be in one of four states
   upon entry: idle, first word of command received (cmd), command parameter
   received (param), or data transfer (exec).

   Entry in the command state causes execution of one-word commands and
   solicitation of command parameters for two-word commands, which are executed
   when entering in the parameter state.

   Entry in the data transfer state moves one word between the CPU and a read or
   write buffer.  For writes, the write buffer is filled with words from the
   CPU.  Once the indicated number of words have been transferred, the
   appropriate line service is scheduled to send the characters.  For reads,
   characters are unloaded from the read buffer to the CPU; an odd-length
   transfer is padded with a blank.  A read of fewer characters than are present
   in the buffer will return the remaining characters when the next read is
   performed.

   Each read or write is terminated by the CPU sending one additional word (the
   RTE drivers send -1).  The command completes when this word is acknowledged
   by the card setting the device flag.  For zero-length writes, this additional
   word will be the only word sent.

   Data transfer is also used by the "Download executable" command to absorb the
   downloaded program.  The firmware jumps to location 5100 hex in the
   downloaded program upon completion of reception.  It is the responsibility of
   the program to return to the multiplexer firmware and to return to the CPU
   whatever status is appropriate when it is done.  Under simulation, we simply
   "sink" the program and return status compatible with the multiplexer
   diagnostic program to simulate a passing test.

   Entry in the idle state checks for unsolicited interrupts.  UIs are sent to
   the host when the controller is idle, UIs have been enabled, and a UI
   condition exists.  If a UI is not acknowledged, it will remain pending and
   will be reissued the next time the controller is idle and UIs have been
   enabled.

   UI conditions are kept in the per-port flags.  The UI conditions are write
   buffer available, read buffer available, break received, modem line
   connected, and modem line disconnected.  The latter two conditions are not
   implemented in this simulation.  If a break condition occurs at the same time
   as a read buffer completion, the break has priority; the buffer UI will occur
   after the break UI is acknowledged.

   The firmware checks for UI condition flags as part of the scheduler polling
   loop.  Under simulation, though, UIs can occur only in two places: the point
   of origin (e.g., termination of a read buffer), or the "Enable unsolicited
   input" command executor.  UIs will be generated at the point of origin only
   if the simulator is idle.  If the simulator is not idle, it is assumed that
   UIs have been disabled to execute the current command and will be reenabled
   when the command sequence is complete.

   When the multiplexer is reset, and before the port keys are set, all ports
   enter "echoplex" mode.  In this mode, characters received are echoed back as
   a functional test.  Each port terminates buffers on CR reception.  We detect
   this condition, cancel the buffer, and discard the buffer termination UI.

   Implementation notes:

    1. The firmware transfers the full amount requested by the CPU, even if the
       transfer is longer than the buffer.  Also, zero-length transfers program
       the card DMA chip to transfer 0 bytes; this results in a transfer of 217
       bytes, per the Zilog databook.  Under simulation, writes beyond the
       buffer are accepted from the CPU but discarded, and reads beyond the
       buffer return blanks.

    2. We should never return from this routine in the "cmd" state, so debugging
       will report "internal error!" if we do.
*/

t_stat mpx_cntl_svc (UNIT *uptr)
{
uint8 ch;
uint32 i;
t_bool add_crlf;
t_bool set_flag = TRUE;
STATE last_state = mpx_state;

static const char *cmd_state [] = { "complete", "internal error!", "waiting for parameter", "executing" };


switch (mpx_state) {                                                /* dispatch on current state */

    case idle:                                                      /* controller idle */
        set_flag = FALSE;                                           /* assume no UI */

        if (mpx_uicode) {                                                   /* unacknowledged UI? */
            if (mpx_uien == TRUE) {                                         /* interrupts enabled? */
                mpx_port = GET_UIPORT (mpx_uicode);                         /* get port number */
                mpx_portkey = mpx_key [mpx_port];                           /* get port key */
                mpx_ibuf = (uint16) (mpx_uicode & UI_REASON | mpx_portkey); /* report UI reason and port key */
                set_flag = TRUE;                                            /* reissue host interrupt */
                mpx_uien = FALSE;                                           /* disable UI */

                if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                    fprintf (sim_deb, ">>MPX cmds: Port %d key %d unsolicited interrupt reissued, "
                                      "reason = %d\n", mpx_port, mpx_portkey, GET_UIREASON (mpx_uicode));
                }
            }

        else {                                                      /* no unacknowledged UI */
            for (i = 0; i < MPX_PORTS; i++) {                       /* check all ports for UIs */
                if (mpx_flags [i] & FL_UI_PENDING) {                /* pending UI? */
                    mpx_portkey = mpx_key [i];                      /* get port key */

                    if (mpx_portkey == KEY_DEFAULT) {               /* key defined? */
                        if (mpx_flags [i] & FL_HAVEBUF)             /* no, is this read buffer avail? */
                            buf_cancel (ioread, i, get);            /* cancel buffer */

                        mpx_flags [i] &= ~FL_UI_PENDING;            /* cancel pending UI */
                        }

                    else if (mpx_uien == TRUE) {                    /* interrupts enabled? */
                        if ((mpx_flags [i] & FL_WANTBUF) &&         /* port wants a write buffer? */
                            (buf_avail (iowrite, i) > 0))           /*   and one is available? */
                            mpx_uicode = UI_WRBUF_AVAIL;            /* set UI reason */

                        else if (mpx_flags [i] & FL_BREAK)          /* received a line BREAK? */
                            mpx_uicode = UI_BRK_RECD;               /* set UI reason */

                        else if (mpx_flags [i] & FL_HAVEBUF)        /* have a read buffer ready? */
                            mpx_uicode = UI_RDBUF_AVAIL;            /* set UI reason */

                        if (mpx_uicode) {                                   /* UI to send? */
                            mpx_port = i;                                   /* set port number for Acknowledge */
                            mpx_ibuf = (uint16) (mpx_uicode | mpx_portkey); /* merge UI reason and port key */
                            mpx_uicode = mpx_uicode | mpx_port;             /* save UI reason and port */
                            set_flag = TRUE;                                /* interrupt host */
                            mpx_uien = FALSE;                               /* disable UI */

                            if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                                fprintf (sim_deb, ">>MPX cmds: Port %d key %d unsolicited interrupt generated, "
                                                  "reason = %d\n", i, mpx_portkey, GET_UIREASON (mpx_uicode));

                            break;                                  /* quit after first UI */
                            }
                        }
                    }
                }
            }
        break;


    case cmd:                                           /* command state */
        if (mpx_cmd & CMD_TWO_WORDS)                    /* two-word command? */
            mpx_state = param;                          /* look for parameter before executing */
        else
            set_flag = exec_command ();                 /* execute one-word command */
        break;


    case param:                                         /* parameter get state */
        mpx_param = mpx_obuf;                           /* save parameter */
        set_flag = exec_command ();                     /* execute two-word command */
        break;


    case exec:                                          /* execution state */
        switch (mpx_cmd) {

            case CMD_BINARY_READ:                       /* fast binary read */
                mpx_flags [0] &= ~FL_HAVEBUF;           /* data word was picked up by CPU */
                set_flag = FALSE;                       /* suppress device flag */
                break;


            case CMD_WRITE:                                 /* transfer data to buffer */
                if (mpx_iolen <= 0) {                       /* last (or only) entry? */
                    mpx_state = idle;                       /* idle controller */

                    if (mpx_iolen < 0)                      /* tie-off for buffer complete? */
                        break;                              /* we're done */
                    }

                add_crlf = ((mpx_param &                    /* CRLF should be added */
                             (WR_ADD_CRLF | WR_PARTIAL)) == WR_ADD_CRLF);

                for (i = 0; i < 2; i++)                     /* output one or two chars */
                    if (mpx_iolen > 0) {                    /* more to do? */
                        if (i)                              /* high or low byte? */
                            ch = (uint8) (mpx_obuf & 0377); /* low byte */
                        else
                            ch = mpx_obuf >> 8;             /* high byte */

                        if ((mpx_iolen == 1) &&             /* final char? */
                            (ch == '_') && add_crlf) {      /* underscore and asking for CRLF? */

                            add_crlf = FALSE;               /* suppress CRLF */

                            if (DEBUG_PRI (mpx_dev, DEB_BUF))
                                fprintf (sim_deb, ">>MPX buf:  Port %d character '_' "
                                                  "suppressed CR/LF\n", mpx_port);
                            }

                        else if (buf_len (iowrite, mpx_port, put) < WR_BUF_LIMIT)
                            buf_put (iowrite, mpx_port, ch);    /* add char to buffer if space avail */

                        mpx_iolen = mpx_iolen - 1;              /* drop remaining count */
                        }

                if (mpx_iolen == 0)  {                              /* buffer done? */
                    if (add_crlf) {                                 /* want CRLF? */
                        buf_put (iowrite, mpx_port, CR);            /* add CR to buffer */
                        buf_put (iowrite, mpx_port, LF);            /* add LF to buffer */
                        }

                    buf_term (iowrite, mpx_port, (uint8) (mpx_param  >> 8));    /* terminate buffer */
                    mpx_iolen = -1;                                             /* mark as done */
                    }

                if (DEBUG_PRI (mpx_dev, DEB_CMDS) &&
                    (sim_is_active (&mpx_unit [mpx_port]) == 0))
                    fprintf (sim_deb, ">>MPX cmds: Port %d service scheduled, "
                                      "time = %d\n", mpx_port, mpx_unit [mpx_port].wait);

                sim_activate (&mpx_unit [mpx_port],                 /* start line service */
                              mpx_unit [mpx_port].wait);
                break;


            case CMD_READ:                                          /* transfer data from buffer */
                if (mpx_iolen < 0) {                                /* input complete? */
                    if (mpx_obuf == 0177777) {                      /* "tie-off" word received? */
                        if (buf_len (ioread, mpx_port, get) == 0) { /* buffer now empty? */
                            buf_free (ioread, mpx_port);            /* free buffer */

                            if ((buf_avail (ioread, mpx_port) == 1) &&  /* one buffer remaining? */
                                !(mpx_flags [mpx_port] & FL_RDFILL))    /*   and not filling it? */
                                mpx_flags [mpx_port] |= FL_HAVEBUF;     /* indicate buffer availability */
                            }

                        mpx_state = idle;                           /* idle controller */
                        }

                    else
                        set_flag = FALSE;                           /* ignore word */

                    break;
                    }

                for (i = 0; i < 2; i++)                             /* input one or two chars */
                    if (mpx_iolen > 0) {                            /* more to transfer? */
                        if (buf_len (ioread, mpx_port, get) > 0)    /* more chars available? */
                            ch = buf_get (ioread, mpx_port);        /* get char from buffer */
                        else                                        /* buffer exhausted */
                            ch = ' ';                               /* pad with blank */

                        if (i)                                      /* high or low byte? */
                            mpx_ibuf = mpx_ibuf | ch;               /* low byte */
                        else
                            mpx_ibuf = (uint16) (ch << 8);          /* high byte */

                        mpx_iolen = mpx_iolen - 1;                  /* drop count */
                        }

                    else                                            /* odd number of chars */
                        mpx_ibuf = mpx_ibuf | ' ';                  /* pad last with blank */

                if (mpx_iolen == 0)                                 /* end of host xfer? */
                    mpx_iolen = -1;                                 /* mark as done */

                break;


            case CMD_DL_EXEC:                                   /* sink data from host */
                if (mpx_iolen <= 0) {                           /* final entry? */
                    mpx_state = idle;                           /* idle controller */
                    mpx_ibuf = ST_DIAG_OK;                      /* return diag passed status */
                    }

                else {
                    if (mpx_iolen > 0)                          /* more from host? */
                        mpx_iolen = mpx_iolen - 2;              /* sink two bytes */

                    if (mpx_iolen <= 0)                         /* finished download? */
                        sim_activate (&mpx_cntl, CMD_DELAY);    /* schedule completion */

                    if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                        fprintf (sim_deb, ">>MPX cmds: Download completion scheduled, "
                                          "time = %d\n", CMD_DELAY);
                    }

                break;


            default:                                            /* no other entries allowed */
                return SCPE_IERR;                               /* simulator error! */
            }
        break;
    }


if (DEBUG_PRI (mpx_dev, DEB_CMDS) &&                    /* debug print? */
    (last_state != mpx_state)) {                        /*   and state change? */
    fprintf (sim_deb, ">>MPX cmds: Command %03o ", mpx_cmd);

    if ((mpx_cmd & CMD_TWO_WORDS) && (mpx_state != param))
        fprintf (sim_deb, "parameter %06o ", mpx_param);

    fputs (cmd_state [mpx_state], sim_deb);
    fputc ('\n', sim_deb);
    }

if (set_flag) {
    mpx_io (&mpx_dib, ioENF, 0);                        /* set device flag */

    if (DEBUG_PRI (mpx_dev, DEB_CMDS))
        fputs (">>MPX cmds: Flag set\n", sim_deb);
    }

return SCPE_OK;
}


/* Multiplexer line service.

   The line service routine is used to transmit and receive characters.  It is
   started when a buffer is ready for output or when the Telnet poll routine
   determines that there are characters ready for input, and it is stopped when
   there are no more characters to output or input.  When a line is quiescent,
   this routine does not run.  Service times are selected to approximate the
   baud rate setting of the multiplexer port.

   "Fast timing" mode enables three optimizations.  First, buffered characters
   are transferred via Telnet in blocks, rather than a character at a time; this
   reduces network traffic and decreases simulator overhead (there is only one
   service routine entry per block, rather than one per character).  Second,
   ENQ/ACK handshaking is done locally, without involving the Telnet client.
   Third, when editing and echo is enabled, entering BS echoes a backspace, a
   space, and a backspace, and entering DEL echoes a backslash, a carriage
   return, and a line feed, providing better compatibility with prior RTE
   terminal drivers.

   Each read and write buffer begins with a reserved header byte that stores
   per-buffer information, such as whether handshaking should be suppressed
   during output, or the specific cause of termination for input.  Buffer
   termination sets the header byte with the appropriate flags.

   For output, a character counter is maintained and is incremented if ENQ/ACK
   handshaking is enabled for the current port and request.  If the counter
   limit is reached, an ENQ is sent, and a flag is set to suspend transmission
   until an ACK is received.  If the last character of the buffer is sent, the
   write buffer is freed, and a UI check is made if the controller is idle, in
   case a write buffer request is pending.

   For input, the character is retrieved from the Telnet buffer.  If a BREAK was
   received, break status is set, and the character is discarded (the current
   multiplexer library implementation always returns a NUL with a BREAK
   indication).  If the character is an XOFF, and XON/XOFF pacing is enabled, a
   flag is set, and transmission is suspended until a corresponding XON is
   received.  If the character is an ACK and is in response to a previously sent
   ENQ, it is discarded, and transmission is reenabled.

   If editing is enabled, a BS will delete the last character in the read
   buffer, and a DEL will delete the entire buffer.  Otherwise, buffer
   termination conditions are checked (end on character, end on count, or
   buffer full), and if observed, the read buffer is terminated, and a read
   buffer available UI condition is signalled.


   Implementation notes:

    1. The firmware echoes an entered BS before checking the buffer count to see
       if there are any characters to delete.  Under simulation, we only echo if
       the buffer is not empty.

    2. The "Fast binary read" command inhibits the normal transmit and receive
       processing.  Instead, a pair of characters are sought on line 0 to fill
       the input buffer.  When they are received, the device flag is set.  The
       CPU will do a LIx sc,C to retrieve the data and reset the flag.

    3. In fast timing mode, burst transfers are used only to fill the first of
       the two receive buffers; the second is filled with one character per
       service entry.  This allows the CPU time to unload the first buffer
       before the second fills up.  Once the first buffer is freed, the routine
       shifts back to burst mode to fill the remainder of the second buffer.
*/

t_stat mpx_line_svc (UNIT *uptr)
{
const  int32 port = uptr - mpx_unit;                            /* port number */
const uint16 rt = mpx_rcvtype [port];                           /* receive type for port */
const uint32 data_bits = 5 + GET_BPC (mpx_config [port]);       /* number of data bits */
const uint32 data_mask = (1 << data_bits) - 1;                  /* mask for data bits */
const t_bool fast_timing = (uptr->flags & UNIT_FASTTIME) != 0;  /* port is set for fast timing */
const t_bool fast_binary_read = (mpx_cmd == CMD_BINARY_READ);   /* fast binary read in progress */
uint8 ch;
int32 chx;
uint32 buffer_count, write_count;
t_stat status = SCPE_OK;
t_bool recv_loop = !fast_binary_read;                           /* bypass if fast binary read */
t_bool xmit_loop = !(fast_binary_read ||                        /* bypass if fast read or output suspended */
                     (mpx_flags [port] & (FL_WAITACK | FL_XOFF)));


if (DEBUG_PRI (mpx_dev, DEB_CMDS))
    fprintf (sim_deb, ">>MPX cmds: Port %d service entered\n", port);

/* Transmission service */

write_count = buf_len (iowrite, port, get);             /* get the output buffer length */

while (xmit_loop && write_count > 0) {                  /* character available to output? */
    if ((mpx_flags [port] & FL_WREMPT) == 0) {          /* if the buffer has not started emptying */
        chx = buf_get (iowrite, port) << 8;             /*   then get the header value and position it */

        if (fast_timing || (chx & WR_NO_ENQACK) ||      /* do we want handshake? */
            !(mpx_config [port] & SK_ENQACK))           /*   and configured for handshake? */
            mpx_flags [port] &= ~FL_DO_ENQACK;          /* no, so clear flag */
        else
            mpx_flags [port] |= FL_DO_ENQACK;           /* yes, so set flag */

        continue;                                       /* continue with the first output character */
        }

    if (mpx_flags [port] & FL_DO_ENQACK)                /* do handshake for this buffer? */
        mpx_enq_cntr [port] = mpx_enq_cntr [port] + 1;  /* bump character counter */

    if (mpx_enq_cntr [port] > ENQ_LIMIT) {              /* ready for ENQ? */
        mpx_enq_cntr [port] = 0;                        /* clear ENQ counter */
        mpx_ack_wait [port] = 0;                        /* clear ACK wait timer */

        mpx_flags [port] |= FL_WAITACK;                 /* set wait for ACK */
        ch = ENQ;
        status = tmxr_putc_ln (&mpx_ldsc [port], ch);   /* transmit ENQ */
        xmit_loop = FALSE;                              /* stop further transmission */
        }

    else {                                              /* not ready for ENQ */
        ch = buf_get (iowrite, port) & data_mask;       /* get char and mask to bit width */
        status = tmxr_putc_ln (&mpx_ldsc [port], ch);   /* transmit the character */

        write_count = write_count - 1;                  /* count the character */
        xmit_loop = (status == SCPE_OK) && fast_timing; /*   and continue transmission if enabled */
        }

    if (status != SCPE_OK)                              /* if the transmission failed */
        xmit_loop = FALSE;                              /*   then exit the loop */

    else if (DEBUG_PRI (mpx_dev, DEB_XFER))
        fprintf (sim_deb, ">>MPX xfer: Port %d character %s transmitted\n",
                          port, fmt_char (ch));

    if (write_count == 0) {                             /* buffer complete? */
        buf_free (iowrite, port);                       /* free buffer */

        write_count = buf_len (iowrite, port, get);     /* get the next output buffer length */

        if (mpx_state == idle)                          /* controller idle? */
            mpx_cntl_svc (&mpx_cntl);                   /* check for UI */
        }
    }


/* Reception service */

buffer_count = buf_avail (ioread, port);                /* get the number of available read buffers */

if (mpx_flags [port] & FL_RDFILL)                       /* if filling the current buffer */
    buffer_count = buffer_count + 1;                    /*   then include it in the count */

while (recv_loop) {                                     /* OK to process? */
    chx = tmxr_getc_ln (&mpx_ldsc [port]);              /* get a new character */

    if (chx == 0)                                       /* if there are no more characters available */
        break;                                          /*   then quit the reception loop */

    if (chx & SCPE_BREAK) {                             /* break detected? */
        mpx_flags [port] |= FL_BREAK;                   /* set break status */

        if (DEBUG_PRI (mpx_dev, DEB_XFER))
            fputs (">>MPX xfer: Break detected\n", sim_deb);

        if (mpx_state == idle)                          /* controller idle? */
            mpx_cntl_svc (&mpx_cntl);                   /* check for UI */

        continue;                                       /* discard NUL that accompanied BREAK */
        }

    ch = (uint8) (chx & data_mask);                     /* mask to bits per char */

    if ((ch == XOFF) &&                                 /* XOFF? */
        (mpx_flowcntl [port] & FC_XONXOFF)) {           /*   and handshaking enabled? */
        mpx_flags [port] |= FL_XOFF;                    /* suspend transmission */

        if (DEBUG_PRI (mpx_dev, DEB_XFER))
            fprintf (sim_deb, ">>MPX xfer: Port %d character XOFF "
                              "suspends transmission\n", port);

        recv_loop = fast_timing;                        /* set to loop if fast mode */
        continue;
        }

    else if ((ch == XON) &&                             /* XON? */
             (mpx_flags [port] & FL_XOFF)) {            /*   and currently suspended? */
        mpx_flags [port] &= ~FL_XOFF;                   /* resume transmission */

        if (DEBUG_PRI (mpx_dev, DEB_XFER))
            fprintf (sim_deb, ">>MPX xfer: Port %d character XON "
                              "resumes transmission\n", port);

        recv_loop = fast_timing;                        /* set to loop if fast mode */
        continue;
        }

    if (DEBUG_PRI (mpx_dev, DEB_XFER))
        fprintf (sim_deb, ">>MPX xfer: Port %d character %s received\n",
                          port, fmt_char (ch));

    if ((ch == ACK) && (mpx_flags [port] & FL_WAITACK)) {   /* ACK and waiting for it? */
        mpx_flags [port] = mpx_flags [port] & ~FL_WAITACK;  /* clear wait flag */
        recv_loop = FALSE;                                  /* absorb character */
        }

    else if (buffer_count == 0 &&                           /* no free buffer available for char? */
             !(mpx_flags [port] & FL_RDFILL)) {             /*   and not filling last buffer? */
        mpx_flags [port] |= FL_RDOVFLOW;                    /* set buffer overflow flag */
        recv_loop = fast_timing;                            /* continue loop if fast mode */
        }

    else {                                                      /* buffer is available */
        if (rt & RT_ENAB_EDIT)                                  /* editing enabled? */
            if (ch == BS) {                                     /* backspace? */
                if (buf_len (ioread, port, put) > 0)            /* at least one character in buffer? */
                    buf_remove (ioread, port);                  /* remove last char */

                if (rt & RT_ENAB_ECHO) {                        /* echo enabled? */
                    tmxr_putc_ln (&mpx_ldsc [port], BS);        /* echo BS */

                    if (fast_timing) {                          /* fast timing mode? */
                        tmxr_putc_ln (&mpx_ldsc [port], ' ');   /* echo space */
                        tmxr_putc_ln (&mpx_ldsc [port], BS);    /* echo BS */
                        }
                    }

                continue;
                }

            else if (ch == DEL) {                               /* delete line? */
                buf_cancel (ioread, port, put);                 /* cancel put buffer */

                if (rt & RT_ENAB_ECHO) {                        /* echo enabled? */
                    if (fast_timing)                            /* fast timing mode? */
                        tmxr_putc_ln (&mpx_ldsc [port], '\\');  /* echo backslash */

                    tmxr_putc_ln (&mpx_ldsc [port], CR);        /* echo CR */
                    tmxr_putc_ln (&mpx_ldsc [port], LF);        /*   and LF */
                    }

                continue;
                }

        if (uptr->flags & UNIT_CAPSLOCK)                    /* caps lock mode? */
            ch = (uint8) toupper (ch);                      /* convert to upper case if lower */

        if (rt & RT_ENAB_ECHO)                              /* echo enabled? */
            tmxr_putc_ln (&mpx_ldsc [port], ch);            /* echo the char */

        if (rt & RT_END_ON_CHAR) {                          /* end on character? */
            recv_loop = FALSE;                              /* assume termination */

            if ((ch == CR) && (rt & RT_END_ON_CR)) {
                if (rt & RT_ENAB_ECHO)                      /* echo enabled? */
                    tmxr_putc_ln (&mpx_ldsc [port], LF);    /* send LF */
                mpx_param = RS_ETC_CR;                      /* set termination condition */
                }

            else if ((ch == RS)  && (rt & RT_END_ON_RS))
                mpx_param = RS_ETC_RS;                      /* set termination condition */

            else if ((ch == EOT) && (rt & RT_END_ON_EOT))
                mpx_param = RS_ETC_EOT;                     /* set termination condition */

            else if ((ch == DC2) && (rt & RT_END_ON_DC2))
                mpx_param = RS_ETC_DC2;                     /* set termination condition */

            else
                recv_loop = TRUE;                           /* no termination */
            }

        if (recv_loop) {                                    /* no termination condition? */
            buf_put (ioread, port, ch);                     /* put character in buffer */
            mpx_charcnt [port]++;                           /*   and count it */
            }

        if ((rt & RT_END_ON_CNT) &&                         /* end on count */
            (mpx_charcnt [port] == mpx_termcnt [port])) {   /*   and termination count reached? */
            recv_loop = FALSE;                              /* set termination */
            mpx_param = 0;                                  /* no extra termination info */
            mpx_charcnt [port] = 0;                         /* clear the current character count */

            if (mpx_flags [port] & FL_ALERT) {              /* was this alert for term rcv buffer? */
                mpx_flags [port] &= ~FL_ALERT;              /* clear alert flag */
                mpx_termcnt [port] = RD_BUF_LIMIT;          /* reset termination character count */
                }
            }

        else if (buf_len (ioread, port, put) == RD_BUF_LIMIT) { /* buffer now full? */
            recv_loop = FALSE;                                  /* set termination */
            mpx_param = mpx_param | RS_PARTIAL;                 /*   and partial buffer flag */
            }

        if (recv_loop)                                      /* if there is no termination condition */
            if (buffer_count == 2)                          /*   then if we're filling the first buffer */
                recv_loop = fast_timing;                    /*     then set to loop if in fast mode */
            else                                            /*   otherwise we're filling the second */
                recv_loop = FALSE;                          /*     so give the CPU a chance to read the first */

        else {                                              /* otherwise a termination condition exists */
            if (DEBUG_PRI (mpx_dev, DEB_XFER)) {
                fprintf (sim_deb, ">>MPX xfer: Port %d read terminated on ", port);

                if (mpx_param & RS_PARTIAL)
                    fputs ("buffer full\n", sim_deb);
                else if (rt & RT_END_ON_CHAR)
                    fprintf (sim_deb, "character %s\n", fmt_char (ch));
                else
                    fprintf (sim_deb, "count = %d\n", mpx_termcnt [port]);
                }

            if (buf_len (ioread, port, put) == 0) {         /* zero-length read? */
                buf_put (ioread, port, 0);                  /* dummy put to reserve header */
                buf_remove (ioread, port);                  /* back out dummy char leaving header */
                }

            buf_term (ioread, port, (uint8) (mpx_param >> 8));  /* terminate buffer and set header */

            if (buf_avail (ioread, port) == 1)              /* first read buffer? */
                mpx_flags [port] |= FL_HAVEBUF;             /* indicate availability */

            if (mpx_state == idle)                          /* controller idle? */
                mpx_cntl_svc (&mpx_cntl);                   /* check for UI */
            }
        }
    }


/* Housekeeping */

if (fast_binary_read) {                                     /* fast binary read in progress? */
    if (port == 0) {                                        /* on port 0? */
        chx = tmxr_getc_ln (&mpx_ldsc [0]);                 /* see if a character is ready */

        if (chx && !(mpx_flags [0] & FL_HAVEBUF)) {         /* character ready and buffer empty? */
            if (mpx_flags [0] & FL_WANTBUF) {               /* second character? */
                mpx_ibuf = mpx_ibuf | (chx & DMASK8);       /* merge it into word */
                mpx_flags [0] |= FL_HAVEBUF;                /* mark buffer as ready */

                mpx_io (&mpx_dib, ioENF, 0);                /* set device flag */

                if (DEBUG_PRI (mpx_dev, DEB_CMDS))
                    fputs (">>MPX cmds: Flag and SRQ set\n", sim_deb);
                }

            else                                            /* first character */
                mpx_ibuf = (uint16) ((chx & DMASK8) << 8);  /* put in top half of word */

            mpx_flags [0] ^= FL_WANTBUF;                    /* toggle byte flag */
            }

        sim_activate (uptr, uptr->wait);                    /* reschedule service for fast response */
        }
    }

else {                                                      /* normal service */
    tmxr_poll_tx (&mpx_desc);                               /* output any accumulated characters */

    if (write_count > 0                                     /* if there are more characters to transmit */
      && !(mpx_flags [port] & (FL_WAITACK | FL_XOFF))       /*   and transmission is not suspended */
      || tmxr_rqln (&mpx_ldsc [port])) {                    /*   or there are more characters to receive */
        sim_activate (uptr, uptr->wait);                    /*     then reschedule the service */

        if (DEBUG_PRI (mpx_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MPX cmds: Port %d delay %d service rescheduled\n", port, uptr->wait);
        }

    else
        if (DEBUG_PRI (mpx_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MPX cmds: Port %d service stopped\n", port);
    }

return SCPE_OK;
}


/* Telnet poll service.

   This service routine is used to poll for Telnet connections and incoming
   characters.  It starts when the socket is attached and stops when the socket
   is detached.

   Each line is then checked for a pending ENQ/ACK handshake.  If one is
   pending, the ACK counter is incremented, and if it times out, another ENQ is
   sent to avoid stalls.  Lines are also checked for available characters, and
   the corresponding line I/O service routine is scheduled if needed.
*/

t_stat mpx_poll_svc (UNIT *uptr)
{
uint32 i;
t_stat status = SCPE_OK;

poll_connection ();                                         /* check for new connection */

tmxr_poll_rx (&mpx_desc);                                   /* poll for input */

for (i = 0; i < MPX_PORTS; i++) {                           /* check lines */
    if (mpx_flags [i] & FL_WAITACK) {                       /* waiting for ACK? */
        mpx_ack_wait [i] = mpx_ack_wait [i] + 1;            /* increment ACK wait timer */

        if (mpx_ack_wait [i] > ACK_LIMIT) {                 /* has wait timed out? */
            mpx_ack_wait [i] = 0;                           /* reset counter */
            status = tmxr_putc_ln (&mpx_ldsc [i], ENQ);     /* send ENQ again */
            tmxr_poll_tx (&mpx_desc);                       /* transmit it */

            if ((status == SCPE_OK) &&                      /* transmitted OK? */
                DEBUG_PRI (mpx_dev, DEB_XFER))
                fprintf (sim_deb, ">>MPX xfer: Port %d character ENQ retransmitted\n", i);
            }
        }

    if (tmxr_rqln (&mpx_ldsc [i]))                          /* chars available? */
        sim_activate (&mpx_unit [i], mpx_unit [i].wait);    /* activate I/O service */
    }

if (uptr->wait == POLL_FIRST)                               /* first poll? */
    uptr->wait = sync_poll (INITIAL);                       /* initial synchronization */
else                                                        /* not first */
    uptr->wait = sync_poll (SERVICE);                       /* continue synchronization */

sim_activate (uptr, uptr->wait);                            /* continue polling */

return SCPE_OK;
}


/* Simulator reset routine.

   The hardware CRS signal generates a reset signal to the Z80 and its
   peripherals.  This causes execution of the power up initialization code.

   The CRS signal also has these hardware effects:
    - clears control
    - clears flag
    - clears flag buffer
    - clears backplane ready
    - clears the output buffer register

   Implementation notes:

    1. Under simulation, we also clear the input buffer register, even though
       the hardware doesn't.

    2. We set up the first poll for Telnet connections to occur "immediately"
       upon execution, so that clients will be connected before execution
       begins.  Otherwise, a fast program may access the multiplexer before the
       poll service routine activates.

    3. We must set the "emptying_flags" and "filling_flags" values here, because
       they cannot be initialized statically, even though the values are
       constant.
*/

t_stat mpx_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* power-on reset? */
    emptying_flags [ioread]  = FL_RDEMPT;               /* initialize buffer flags constants */
    emptying_flags [iowrite] = FL_WREMPT;
    filling_flags  [ioread]  = FL_RDFILL;
    filling_flags  [iowrite] = FL_WRFILL;
    }

IOPRESET (&mpx_dib);                                    /* PRESET device (does not use PON) */

mpx_ibuf = 0;                                           /* clear input buffer */

if (mpx_poll.flags & UNIT_ATT) {                        /* network attached? */
    mpx_poll.wait = POLL_FIRST;                         /* set up poll */
    sim_activate (&mpx_poll, mpx_poll.wait);            /* start Telnet poll immediately */
    }
else
    sim_cancel (&mpx_poll);                             /* else stop Telnet poll */

return SCPE_OK;
}


/* Attach the multiplexer to a Telnet port.

   We are called by the ATTACH MPX <port> command to attach the multiplexer to
   the listening port indicated by <port>.  Logically, it is the multiplexer
   device that is attached; however, SIMH only allows units to be attached.
   This makes sense for devices such as tape drives, where the attached media is
   a property of a specific drive.  In our case, though, the listening port is a
   property of the multiplexer card, not of any given serial line.  As ATTACH
   MPX is equivalent to ATTACH MPX0, the port would, by default, be attached to
   the first serial line and be reported there in a SHOW MPX command.

   To preserve the logical picture, we attach the port to the Telnet poll unit,
   which is normally disabled to inhibit its display.  Attaching to a disabled
   unit is not allowed, so we first enable the unit, then attach it, then
   disable it again.  Attachment is reported by the "mpx_status" routine below.

   A direct attach to the poll unit is only allowed when restoring a previously
   saved session.

   The Telnet poll service routine is synchronized with the other input polling
   devices in the simulator to facilitate idling.
*/

t_stat mpx_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status = SCPE_OK;

if (uptr != mpx_unit                                        /* not unit 0? */
  && (uptr != &mpx_poll || !(sim_switches & SIM_SW_REST)))  /*   and not restoring the poll unit? */
    return SCPE_NOATT;                                      /* can't attach */

mpx_poll.flags = mpx_poll.flags & ~UNIT_DIS;                /* enable unit */
status = tmxr_attach (&mpx_desc, &mpx_poll, cptr);          /* attach to socket */
mpx_poll.flags = mpx_poll.flags | UNIT_DIS;                 /* disable unit */

if (status == SCPE_OK) {
    mpx_poll.wait = POLL_FIRST;                             /* set up poll */
    sim_activate (&mpx_poll, mpx_poll.wait);                /* start poll immediately */
    }
return status;
}


/* Detach the multiplexer.

   Normally, we are called by the DETACH MPX command, which is equivalent to
   DETACH MPX0.  However, we may be called with other units in two cases.

   A DETACH ALL command will call us for unit 9 (the poll unit) if it is
   attached.  Also, during simulator shutdown, we will be called for units 0-8
   (detach_all in scp.c calls the detach routines of all units that do NOT have
   UNIT_ATTABLE), as well as for unit 9 if it is attached.  In both cases, it is
   imperative that we return SCPE_OK, otherwise any remaining device detaches
   will not be performed.
*/

t_stat mpx_detach (UNIT *uptr)
{
t_stat status = SCPE_OK;
int32 i;

if ((uptr == mpx_unit) || (uptr == &mpx_poll)) {        /* base unit or poll unit? */
    status = tmxr_detach (&mpx_desc, &mpx_poll);        /* detach socket */

    for (i = 0; i < MPX_PORTS; i++) {
        mpx_ldsc [i].rcve = 0;                          /* disable line reception */
        sim_cancel (&mpx_unit [i]);                     /* cancel any scheduled I/O */
        }

    sim_cancel (&mpx_poll);                             /* stop Telnet poll */
    }

return status;
}


/* Show multiplexer status */

t_stat mpx_status (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (mpx_poll.flags & UNIT_ATT)                          /* attached to socket? */
    fprintf (st, "attached to port %s, ", mpx_poll.filename);
else
    fprintf (st, "not attached, ");

tmxr_show_summ (st, uptr, val, desc);                   /* report connection count */
return SCPE_OK;
}


/* Set firmware revision.

   Currently, we support only revision C, so the MTAB entry does not have an
   "mstring" entry.  When we add revision D support, an "mstring" entry of "REV"
   will enable changing the firmware revision.
*/

t_stat mpx_set_frev (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((cptr == NULL) ||                                   /* no parameter? */
    (*cptr < 'C') || (*cptr > 'D') ||                   /*   or not C or D? */
    (*(cptr + 1) != '\0'))                              /*   or not just one character? */
    return SCPE_ARG;                                    /* bad argument */

else {
    if (*cptr == 'C')                                   /* setting revision C? */
        mpx_dev.flags = mpx_dev.flags & ~DEV_REV_D;     /* clear 'D' flag */
    else if (*cptr == 'D')                              /* setting revision D? */
        mpx_dev.flags = mpx_dev.flags | DEV_REV_D;      /* set 'D' flag */

    return SCPE_OK;
    }
}


/* Show firmware revision */

t_stat mpx_show_frev (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (mpx_dev.flags & DEV_REV_D)
    fputs ("12792D", st);
else
    fputs ("12792C", st);
return SCPE_OK;
}


/* Local routines */


/* Poll for new Telnet connections */

static void poll_connection (void)
{
int32 new_line;

new_line = tmxr_poll_conn (&mpx_desc);                      /* check for new connection */

if (new_line >= 0)                                          /* new connection established? */
    mpx_ldsc [new_line].rcve = 1;                           /* enable line to receive */

return;
}


/* Controller reset.

   This is the card microprocessor reset, not the simulator reset routine.  It
   simulates a power-on restart of the Z80 firmware.  When it is called from the
   simulator reset routine, that routine will take care of setting the card
   flip-flops appropriately.
*/

static void controller_reset (void)
{
uint32 i;

mpx_state = idle;                                       /* idle state */

mpx_cmd = 0;                                            /* clear command */
mpx_param = 0;                                          /* clear parameter */
mpx_uien = FALSE;                                       /* disable interrupts */

for (i = 0; i < MPX_PORTS; i++) {                       /* clear per-line variables */
    buf_init (iowrite, i);                              /* initialize write buffers */
    buf_init (ioread, i);                               /* initialize read buffers */

    mpx_key [i] = KEY_DEFAULT;                          /* clear port key to default */

    if (i == 0)                                         /* default port configurations */
        mpx_config [0] = SK_PWRUP_0;                    /* port 0 is separate from 1-7 */
    else
        mpx_config [i] = (uint16) (SK_PWRUP_1 | i);

    mpx_rcvtype [i] = RT_PWRUP;                         /* power on config for echoplex */
    mpx_charcnt [i] = 0;                                /* clear character count */
    mpx_termcnt [i] = 0;                                /* default termination character count */
    mpx_flowcntl [i] = 0;                               /* default flow control */
    mpx_flags [i] = 0;                                  /* clear state flags */
    mpx_enq_cntr [i] = 0;                               /* clear ENQ counter */
    mpx_ack_wait [i] = 0;                               /* clear ACK wait timer */
    mpx_unit [i].wait = service_time (mpx_config [i]);  /* set terminal I/O time */

    sim_cancel (&mpx_unit [i]);                         /* cancel line I/O */
    }

sim_cancel (&mpx_cntl);                                 /* cancel controller */

return;
}


/* Calculate service time from baud rate.

   Service times are based on 1580 instructions per millisecond, which is the
   1000 E-Series execution speed.  Baud rate 0 means "don't change" and is
   handled by the "Set port key" command executor.

   Baud rate settings of 13-15 are marked as "reserved" in the user manual, but
   the firmware defines these as 38400, 9600, and 9600 baud, respectively.
*/

static uint32 service_time (uint16 control_word)
{
/*           Baud Rates 0- 7 :    --,     50,     75,    110,  134.5,    150,   300,  1200, */
/*           Baud Rates 8-15 :  1800,   2400,   4800,   9600,  19200,  38400,  9600,  9600  */
static const int32 ticks [] = {    0, 316000, 210667, 143636, 117472, 105333, 52667, 13167,
                                8778,   6583,   3292,   1646,    823,    411,  1646,  1646 };

return ticks [GET_BAUDRATE (control_word)];             /* return service time for indicated rate */
}


/* Translate port key to port number.

   Port keys are scanned in reverse port order, so if more than one port has the
   same port key, commands specifying that key will affect the highest numbered
   port.

   If a port key is the reserved value 255, then the port key has not been set.
   In this case, set the input buffer to 0xBAD0 and return -1 to indicate
   failure.
*/

static int32 key_to_port (uint32 key)
{
int32 i;

for (i = MPX_PORTS - 1; i >= 0; i--)                    /* scan in reverse order */
    if (mpx_key [i] == key)                             /* key found? */
        return i;                                       /* return port number */

mpx_ibuf = ST_BAD_KEY;                                  /* key not found: set status */
return -1;                                              /* return failure code */
}


/* Buffer manipulation routines.

   The 12792 hardware provides 16K bytes of RAM to the microprocessor.  From
   this pool, the firmware allocates per-port read/write buffers and state
   variables, global variables, and the system stack.  Allocations are static
   and differ between firmware revisions.

   The A/B/C revisions allocate two 254-byte read buffers and two 254-byte write
   buffers per port.  Assuming an idle condition, the first write to a port
   transfers characters to the first write buffer.  When the transfer completes,
   the SIO begins transmitting.  During transmission, a second write can be
   initiated, which transfers characters to the second write buffer.  If a third
   write is attempted before the first buffer has been released, it will be
   denied until the SIO completes transmission; then, if enabled, an unsolicited
   interrupt will occur to announce buffer availability.  The "active" (filling)
   buffer alternates between the two.

   At idle, characters received will fill the first read buffer.  When the read
   completes according to the previously set termination criteria, an
   unsolicited interrupt will occur (if enabled) to announce buffer
   availability.  If more characters are received before the first buffer has
   been transferred to the CPU, they will fill the second buffer.  If that read
   also completes, additional characters will be discarded until the first
   buffer has been emptied.  The "active" (emptying) buffer alternates between
   the two.

   With this configuration, two one-character writes or reads will allocate both
   available buffers, even though each was essentially empty.

   The D revision allocates one 1024-byte FIFO read buffer and one 892-byte
   write buffer per port.  As with the A/B/C revisions, the first write to a
   port transfers characters to the write buffer, and serial transmission begins
   when the write completes.  However, the write buffer is not a FIFO, so the
   host is not permitted another write request until the entire buffer has been
   transmitted.

   The read buffer is a FIFO.  Characters received are placed into the FIFO as a
   stream.  Unlike the A/B/C revisions, character editing and termination
   conditions are not evaluated until the buffer is read.  Therefore, a full
   1024 characters may be received before additional characters would be
   discarded.

   When the first character is received, an unsolicited interrupt occurs (if
   enabled) to announce data reception.  A host read may then be initiated.  The
   write buffer is used temporarily to process characters from the read buffer.
   Characters are copied from the read to the write buffer while editing as
   directed by the configuration accompanying the read request (e.g., deleting
   the character preceding a BS, stripping CR/LF, etc.).  When the termination
   condition is found, the read command completes.  Incoming characters may be
   added to the FIFO while this is occurring.

   In summary, the revision differences in buffer handling are:

     Revisions A/B/C:
      - two 254-byte receive buffers
      - a buffer is "full" when the terminator character or count is received
      - termination type must be established before the corresponding read
      - data is echoed as it is received

     Revision D:
      - one 1024-byte receive buffer
      - buffer is "full" only when 1024 characters are received
      - the concept of a buffer terminator does not apply, as the data is not
        examined until a read is requested and characters are retrieved from the
        FIFO.
      - data is not echoed until it is read

   To implement the C revision behavior, while preserving the option of reusing
   the buffer handlers for future D revision support, the dual 254-byte buffers
   are implemented as a single 514-byte circular FIFO with capacity limited to
   254 bytes per buffer.  This reserves space for a CR and LF and for a header
   byte in each buffer.  The header byte preserves per-buffer state information.

   In this implementation, the buffer "put" index points at the next free
   location, and the buffer "get" index points at the next character to
   retrieve.  In addition to "put" and "get" indexes, a third "separator" index
   is maintained to divide the FIFO into two areas corresponding to the two
   buffers, and a "buffer filling" flag is maintained for each FIFO that is set
   by the fill (put) routine and cleared by the terminate buffer routine.

   Graphically, the implementation is as follows for buffer "B[]", get "G", put
   "P", and separator "S" indexes:

     1. Initialize:                               2. Fill first buffer:
        G = S = P = 0                                B[P] = char; Incr (P)

        |------------------------------|             |---------|--------------------|
        G                                            G         P -->
        S                                            S
        P

     3. Terminate first buffer:                   4. Fill second buffer:
        if S == G then S = P else nop                B[P] = char; Incr (P)

        |------------|-----------------|             |------------|------|----------|
        G      /---> S                               G            S      P -->
        * ----/      P

     5. Terminate second buffer:                  6. Empty first buffer:
        if S == G then S = P else nop                char = B[G]; Incr (G)

        |------------|------------|----|             |----|-------|------------|----|
        G            S            P                       G -->   S            P

     7. First buffer is empty:                    8. Free first buffer:
        G == S                                       if !filling then S = P else nop

        |------------|------------|----|             |------------|------------|----|
                     G            P                               G      /---> S
                     S                                            * ----/      P

     9. Empty second buffer:                     10. Second buffer empty:
        char = B[G]; Incr (G)                        G == S

        |----------------|--------|----|             |-------------------------|----|
                         G -->    S                                            G
                                  P                                            S
                                                                               P
    11. Free second buffer:
        if !filling then S = P else nop

        |-------------------------|----|
                                  G
                                  S
                                  P

   We also provide the following utility routines:

    - Remove Character: Decr (P)

    - Cancel Buffer: if S == G then P = G else G = S

    - Buffer Length: if S < G then return S + BUFSIZE - G else return S - G

    - Buffers Available: if G == P then return 2 else if G != S != P then return
      0 else return 1

   The "buffer filling" flag is necessary for the "free" routine to decide
   whether to advance the separator index.  If the first buffer is to be freed,
   then G == S and S != P.  If the second buffer is already filled, then S = P.
   However, if the buffer is still filling, then S must remain at G.  This
   cannot be determined from G, S, and P alone.

   A "buffer emptying" flag is also employed to record whether the per-buffer
   header has been obtained.  This allows the buffer length to exclude the
   header and reflect only the characters present.
*/


/* Increment a buffer index with wraparound */

static uint16 buf_incr (BUF_INDEX index, uint32 port, IO_OPER rw, int increment)
{
index [port] [rw] =
  (index [port] [rw] + buf_size [rw] + increment) % buf_size [rw];

return index [port] [rw];
}


/* Initialize the buffer.

   Initialization sets the three indexes to zero and clears the buffer state
   flags.
*/

static void buf_init (IO_OPER rw, uint32 port)
{
mpx_get [port] [rw] = 0;                                /* clear indexes */
mpx_sep [port] [rw] = 0;
mpx_put [port] [rw] = 0;

if (rw == ioread)
    mpx_flags [mpx_port] &= ~(FL_RDFLAGS);              /* clear read buffer flags */
else
    mpx_flags [mpx_port] &= ~(FL_WRFLAGS);              /* clear write buffer flags */
return;
}


/* Get a character from the buffer.

   The character indicated by the "get" index is retrieved from the buffer, and
   the index is incremented with wraparound.  If the buffer is now empty, the
   "buffer emptying" flag is cleared.  Otherwise, it is set to indicate that
   characters have been removed from the buffer.
*/

static uint8 buf_get (IO_OPER rw, uint32 port)
{
uint8 ch;
uint32 index = mpx_get [port] [rw];                     /* current get index */

if (rw == ioread)
    ch = mpx_rbuf [port] [index];                       /* get char from read buffer */
else
    ch = mpx_wbuf [port] [index];                       /* get char from write buffer */

buf_incr (mpx_get, port, rw, +1);                       /* increment circular get index */

if (DEBUG_PRI (mpx_dev, DEB_BUF))
    if (mpx_flags [port] & emptying_flags [rw])
        fprintf (sim_deb, ">>MPX buf:  Port %d character %s get from %s buffer "
                          "[%d]\n", port, fmt_char (ch), io_op [rw], index);
    else
        fprintf (sim_deb, ">>MPX buf:  Port %d header %03o get from %s buffer "
                          "[%d]\n", port, ch, io_op [rw], index);

if (mpx_get [port] [rw] == mpx_sep [port] [rw])         /* buffer now empty? */
    mpx_flags [port] &= ~emptying_flags [rw];           /* clear "buffer emptying" flag */
else
    mpx_flags [port] |= emptying_flags [rw];            /* set "buffer emptying" flag */

return ch;
}


/* Put a character to the buffer.

   The character is written to the buffer in the slot indicated by the "put"
   index, and the index is incremented with wraparound.  The first character put
   to a new buffer reserves space for the header and sets the "buffer filling"
   flag.
*/

static void buf_put (IO_OPER rw, uint32 port, uint8 ch)
{
uint32 index;

if ((mpx_flags [port] & filling_flags [rw]) == 0) {     /* first put to this buffer? */
    mpx_flags [port] |= filling_flags [rw];             /* set buffer filling flag */
    index = mpx_put [port] [rw];                        /* get current put index */
    buf_incr (mpx_put, port, rw, +1);                   /* reserve space for header */

    if (DEBUG_PRI (mpx_dev, DEB_BUF))
        fprintf (sim_deb, ">>MPX buf:  Port %d reserved header "
                          "for %s buffer [%d]\n", port, io_op [rw], index);
    }

index = mpx_put [port] [rw];                            /* get current put index */

if (rw == ioread)
    mpx_rbuf [port] [index] = ch;                       /* put char in read buffer */
else
    mpx_wbuf [port] [index] = ch;                       /* put char in write buffer */

buf_incr (mpx_put, port, rw, +1);                       /* increment circular put index */

if (DEBUG_PRI (mpx_dev, DEB_BUF))
    fprintf (sim_deb, ">>MPX buf:  Port %d character %s put to %s buffer "
                      "[%d]\n", port, fmt_char (ch), io_op [rw], index);
return;
}


/* Remove the last character put to the buffer.

   The most-recent character put to the buffer is removed by decrementing the
   "put" index with wraparound.
*/

static void buf_remove (IO_OPER rw, uint32 port)
{
uint8 ch;
uint32 index;

index = buf_incr (mpx_put, port, rw, -1);               /* decrement circular put index */

if (DEBUG_PRI (mpx_dev, DEB_BUF)) {
    if (rw == ioread)
        ch = mpx_rbuf [port] [index];                   /* pick up char from read buffer */
    else
        ch = mpx_wbuf [port] [index];                   /* pick up char from write buffer */

    fprintf (sim_deb, ">>MPX buf:  Port %d character %s removed from %s buffer "
                      "[%d]\n", port, fmt_char (ch), io_op [rw], index);
    }
return;
}


/* Terminate the buffer.

   The buffer is marked to indicate that filling is complete and that the next
   "put" operation should begin a new buffer.  The header value is stored in
   first byte of buffer, which is reserved, and the "buffer filling" flag is
   cleared.
*/

static void buf_term (IO_OPER rw, uint32 port, uint8 header)
{
uint32 index = mpx_sep [port] [rw];                         /* separator index */

if (rw == ioread)
    mpx_rbuf [port] [index] = header;                       /* put header in read buffer */
else
    mpx_wbuf [port] [index] = header;                       /* put header in write buffer */

mpx_flags [port] = mpx_flags [port] & ~filling_flags [rw];  /* clear filling flag */

if (mpx_get [port] [rw] == index)                           /* reached separator? */
    mpx_sep [port] [rw] = mpx_put [port] [rw];              /* move sep to end of next buffer */

if (DEBUG_PRI (mpx_dev, DEB_BUF))
    fprintf (sim_deb, ">>MPX buf:  Port %d header %03o terminated %s buffer\n",
                      port, header, io_op [rw]);
return;
}


/* Free the buffer.

   The buffer is marked to indicate that it is available for reuse, and the
   "buffer emptying" flag is reset.
*/

static void buf_free (IO_OPER rw, uint32 port)
{
if ((mpx_flags [port] & filling_flags [rw]) == 0)           /* not filling next buffer? */
    mpx_sep [port] [rw] = mpx_put [port] [rw];              /* move separator to end of next buffer */
                                                            /* else it will be moved when terminated */
mpx_flags [port] = mpx_flags [port] & ~emptying_flags [rw]; /* clear emptying flag */

if (DEBUG_PRI (mpx_dev, DEB_BUF))
    fprintf (sim_deb, ">>MPX buf:  Port %d released %s buffer\n", port, io_op [rw]);
return;
}


/* Cancel the selected buffer.

   The selected buffer is marked to indicate that it is empty.  Either the "put"
   buffer or the "get" buffer may be selected.
*/

static void buf_cancel (IO_OPER rw, uint32 port, BUF_SELECT which)
{
if (which == put) {                                     /* cancel put buffer? */
    mpx_put [port] [rw] = mpx_sep [port] [rw];          /* move put back to separator */
    mpx_flags [port] &= ~filling_flags [rw];            /* clear filling flag */
    }

else {                                                  /* cancel get buffer */
    if (mpx_sep [port] [rw] == mpx_get [port] [rw]) {   /* filling first buffer? */
        mpx_put [port] [rw] = mpx_get [port] [rw];      /* cancel first buffer */
        mpx_flags [port] &= ~filling_flags [rw];        /* clear filling flag */
        }

    else {                                                  /* not filling first buffer */
        mpx_get [port] [rw] = mpx_sep [port] [rw];          /* cancel first buffer */

        if ((mpx_flags [port] & filling_flags [rw]) == 0)   /* not filling second buffer? */
            mpx_sep [port] [rw] = mpx_put [port] [rw];      /* move separator to end of next buffer */
        }

    mpx_flags [port] &= ~emptying_flags [rw];           /* clear emptying flag */
    }

if (DEBUG_PRI (mpx_dev, DEB_BUF))
    fprintf (sim_deb, ">>MPX buf:  Port %d cancelled %s buffer\n", port, io_op [rw]);
return;
}


/* Get the buffer length.

   The current length of the selected buffer (put or get) is returned.  For ease
   of use, the returned length does NOT include the header byte, i.e., it
   reflects only the characters contained in the buffer.

   If the put buffer is selected, and the buffer is filling, or the get buffer
   is selected, and the buffer is not emptying, then subtract one from the
   length for the allocated header.
*/

static uint16 buf_len (IO_OPER rw, uint32 port, BUF_SELECT which)
{
int16 length;

if (which == put)
    length = mpx_put [port] [rw] - mpx_sep [port] [rw] -        /* calculate length */
             ((mpx_flags [port] & filling_flags [rw]) != 0);    /* account for allocated header */

else {
    length = mpx_sep [port] [rw] - mpx_get [port] [rw];         /* calculate length */

    if (length && !(mpx_flags [port] & emptying_flags [rw]))    /* not empty and not yet emptying? */
        length = length - 1;                                    /* account for allocated header */
    }

if (length < 0)                                                 /* is length negative? */
    return length + buf_size [rw];                              /* account for wraparound */
else
    return length;
}


/* Return the number of free buffers available.

   Either 0, 1, or 2 free buffers will be available.  A buffer is available if
   it contains no characters (including the header byte).
*/

static uint32 buf_avail (IO_OPER rw, uint32 port)
{
if (mpx_get [port] [rw] == mpx_put [port] [rw])             /* get and put indexes equal? */
    return 2;                                               /* all buffers are free */

else if ((mpx_get [port] [rw] != mpx_sep [port] [rw]) &&    /* get, separator, and put */
         (mpx_sep [port] [rw] != mpx_put [port] [rw]))      /*   all different? */
    return 0;                                               /* no buffers are free */

else
    return 1;                                               /* one buffer free */
}
