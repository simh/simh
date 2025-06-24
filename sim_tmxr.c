/* sim_tmxr.c: Telnet terminal multiplexer library

   Copyright (c) 2001-2011, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Based on the original DZ11 simulator by Thord Nilson, as updated by
   Arthur Krewat.

   12-Oct-12    MP      Revised serial port support to not require changes to
                        any code in TMXR library using code.  Added support
                        for per line listener ports and outgoing tcp connections.
   02-Jun-11    MP      Fixed telnet option negotiation loop with some clients
                        Added Option Negotiation and Debugging Support
   17-Jan-11    MP      Added Buffered line capabilities
   16-Jan-11    MP      Made option negotiation more reliable
   20-Nov-08    RMS     Added three new standardized SHOW routines
   05-Nov-08    JDB     Moved logging call after connection check in tmxr_putc_ln
   03-Nov-08    JDB     Added TMXR null check to tmxr_find_ldsc
   07-Oct-08    JDB     Added initial serial port support
   30-Sep-08    JDB     Reverted tmxr_find_ldsc to original implementation
   27-May-08    JDB     Added line connection order to tmxr_poll_conn,
                        added tmxr_set_lnorder and tmxr_show_lnorder
   14-May-08    JDB     Print device and line to which connection was made
   11-Apr-07    JDB     Worked around Telnet negotiation problem with QCTerm
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   29-Jun-05    RMS     Extended tmxr_dscln to support unit array devices
                        Fixed bug in SET LOG/NOLOG
   04-Jan-04    RMS     Changed TMXR ldsc to be pointer to linedesc array
                        Added tmxr_linemsg, circular output pointers, logging
                        (from Mark Pizzolato)
   29-Dec-03    RMS     Added output stall support
   01-Nov-03    RMS     Cleaned up attach routine
   09-Mar-03    RMS     Fixed bug in SHOW CONN
   22-Dec-02    RMS     Fixed bugs in IAC+IAC receive and transmit sequences
                        Added support for received break (all from by Mark Pizzolato)
                        Fixed bug in attach
   31-Oct-02    RMS     Fixed bug in 8b (binary) support
   22-Aug-02    RMS     Added tmxr_open_master, tmxr_close_master
   30-Dec-01    RMS     Added tmxr_fstats, tmxr_dscln, renamed tmxr_fstatus
   03-Dec-01    RMS     Changed tmxr_fconns for extended SET/SHOW
   20-Oct-01    RMS     Fixed bugs in read logic (found by Thord Nilson).
                        Added tmxr_rqln, tmxr_tqln

   This library includes:

   tmxr_poll_conn -                     poll for connection
   tmxr_reset_ln -                      reset line (drops Telnet/tcp and serial connections)
   tmxr_detach_ln -                     reset line and close per line listener and outgoing destination
   tmxr_getc_ln -                       get character for line
   tmxr_get_packet_ln -                 get packet from line
   tmxr_get_packet_ln_ex -              get packet from line with separator byte
   tmxr_poll_rx -                       poll receive
   tmxr_putc_ln -                       put character for line
   tmxr_put_packet_ln -                 put packet on line
   tmxr_put_packet_ln_ex -              put packet on line with separator byte
   tmxr_poll_tx -                       poll transmit
   tmxr_send_buffered_data -            transmit buffered data
   tmxr_set_modem_control_passthru -    enable modem control on a multiplexer
   tmxr_clear_modem_control_passthru -  disable modem control on a multiplexer
   tmxr_set_port_speed_control -        Declare that tmxr_set_config_line is used
   tmxr_clear_port_speed_control -      Declare that tmxr_set_config_line is not used
   tmxr_set_backlog -                   set listen backlog
   tmxr_set_line_port_speed_control -   Declare that tmxr_set_config_line is used for line
   tmxr_clear_line_port_speed_control - Declare that tmxr_set_config_line is not used for line
   tmxr_set_get_modem_bits -            set and/or get a line modem bits
   tmxr_set_line_loopback -             enable or disable loopback mode on a line
   tmxr_get_line_loopback -             returns the current loopback status of a line
   tmxr_set_line_halfduplex -           enable or disable halfduplex mode on a line
   tmxr_get_line_halfduplex -           returns the current halfduplex status of a line
   tmxr_set_config_line -               set port speed, character size, parity and stop bits
   tmxr_open_master -                   open master connection
   tmxr_close_master -                  close master connection
   tmxr_attach  -                       attach terminal multiplexor to listening port
   tmxr_detach  -                       detach terminal multiplexor to listening port
   tmxr_attach_help  -                  help routine for attaching multiplexer devices
   tmxr_set_line_unit -                 set the unit which polls for input for a given line
   tmxr_ex      -                       (null) examine
   tmxr_dep     -                       (null) deposit
   tmxr_msg     -                       send message to socket
   tmxr_linemsg -                       send message to line
   tmxr_linemsgf -                      send formatted message to line
   tmxr_fconns  -                       output connection status
   tmxr_fstats  -                       output connection statistics
   tmxr_set_log -                       enable logging for line
   tmxr_set_nolog -                     disable logging for line
   tmxr_show_log -                      show logging status for line
   tmxr_dscln   -                       disconnect line (SET routine)
   tmxr_rqln    -                       number of available characters for line
   tmxr_tqln    -                       number of buffered characters for line
   tmxr_tpqln    -                      number of buffered packet characters for line
   tmxr_tpbusyln -                      transmit packet busy status for line
   tmxr_set_lnorder -                   set line connection order
   tmxr_show_lnorder -                  show line connection order
   tmxr_show_summ -                     show connection summary
   tmxr_show_cstat -                    show line connections or status
   tmxr_show_lines -                    show number of lines
   tmxr_show_open_devices -             show info about all open tmxr devices

   All routines are OS-independent.


    This library supports the simulation of multiple-line terminal multiplexers.
    It may also be used to create single-line "multiplexers" to provide
    additional terminals beyond the simulation console.  It may also be used to
    create single-line or multi-line simulated synchronous (BiSync) devices.
    Multiplexer lines may be connected to terminal emulators supporting the
    Telnet protocol via sockets, or to hardware terminals via host serial
    ports.  Concurrent Telnet and serial connections may be mixed on a given
    multiplexer.

    When connecting via sockets, the simulated multiplexer is attached to a
    listening port on the host system:

      sim> attach MUX 23
      Listening on port 23

    Once attached, the listening port must be polled for incoming connections.
    When a connection attempt is received, it will be associated with the next
    multiplexer line in the user-specified line order, or with the next line in
    sequence if no order has been specified.  Individual lines may be connected
    to serial ports or remote systems via TCP (telnet or not as desired), OR
    they may have separate listening TCP ports.

    Logging of Multiplexer Line output:

    The traffic going out multiplexer lines can be logged to files.  A single
    line multiplexer can log it's traffic with the following command:

        sim> atta MUX 23,Log=LogFileName
        sim> atta MUX Connect=ser0,Log=LogFileName

    Specifying a Log value for a multi-line multiplexer is specifying a
    template filename.  The actual file name used for each line will be
    the indicated filename with _n appended (n being the line number).

    Buffered Multiplexer Line:

    A Multiplexer Line Buffering has been implemented.  A Buffered Line will
    have a copy of the last 'buffer size' bytes of output retained in a line
    specific buffer.  The contents of this buffer will be transmitted out any
    new connection on that line when a new telnet session is established.

    This capability is most useful for the Console Telnet session.  When a
    Console Telnet session is Buffered, a simulator will start (via BOOT CPU
    or whatever is appropriate for a particular simulator) without needing to
    have an active telnet connection.  When a Telnet connection comes along
    for the telnet port, the contents of the saved buffer (which wraps on
    overflow) are presented on the telnet session as output before session
    traffic.  This allows the connecting telnet client to see what happened
    before he connected since the likely reason he might be connecting to the
    console of a background simulator is to troubleshoot unusual behavior,
    the details of which may have already been sent to the console.

    Serial Port support:

    Serial ports may be specified as an operating system specific device names
    or using simh generic serial names.  simh generic names are of the form
    serN, where N is from 0 thru one less than the maximum number of serial
    ports on the local system.  The mapping of simh generic port names to OS
    specific names can be displayed using the following command:

        sim> show serial
        Serial devices:
         ser0   COM1 (\Device\Serial0)
         ser1   COM3 (Winachcf0)

        sim> attach MUX Line=2,Connect=ser0

    or equivalently

        sim> attach MUX Line=2,Connect=COM1

    An optional configuration string may be present after the port name.  If
    present, it must be separated from the port name with a semicolon and has
    this form:

       <rate>-<charsize><parity><stopbits>

    where:

      rate     = communication rate in bits per second
      charsize = character size in bits (5-8, including optional parity)
      parity   = parity designator (N/E/O/M/S for no/even/odd/mark/space parity)
      stopbits = number of stop bits (1, 1.5, or 2)

     As an example:

        9600-8n1

    The supported rates, sizes, and parity options are host-specific.  If
    a configuration string is not supplied, then the default of 9600-8N1
    is used.

    An attachment to a serial port with the '-V' switch will cause a
    connection message to be output to the connected serial port.
    This will help to confirm the correct port has been connected and
    that the port settings are reasonable for the connected device.
    This would be done as:

        sim> attach -V MUX Connect=SerN


    Line specific tcp listening ports are supported.  These are configured
    using commands of the form:

        sim> attach MUX Line=2,port{;notelnet}|{;nomessage}

    Direct computer to computer connections (Virtual Null Modem cables) may
    be established using the telnet protocol or via raw tcp sockets.

        sim> attach MUX Line=2,Connect=host:port{;notelnet}

    Computer to computer virtual connections can be one way (as illustrated
    above) or symmetric.  A symmetric connection is configured by combining
    a one way connection with a tcp listening port on the same line:

        sim> attach MUX Line=2,Connect=host:port,listenport

    When symmetric virtual connections are configured, incoming connections
    on the specified listening port are checked to assure that they actually
    come from the specified connection destination host system.



     The command syntax for a single line device (MX) is:

        sim> attach MX port{;backlog=n}{;notelnet}|{;nomessage}
        sim> attach MX Connect=serN{;config}
        sim> attach MX Connect=COM9{;config}
        sim> attach MX Connect=host:port{;notelnet}|{;nomessage}

     The command syntax for ANY multi-line device is:

        sim> attach MX port{;backlog=n}{;notelnet}|{;nomessage} ; Defines the master listening port for the mux and optionally allows non-telnet (i.e. raw socket) operation for all lines.
        sim> attach MX Line=n,port{;backlog=n}{;notelnet}|{;nomessage}  ; Defines a line specific listen port for a particular line. Each line can have a separate listen port and the mux can have its own as well.  Optionally disable telnet wire protocol (i.e. raw socket)
        sim> attach MX Line=n,Connect=serN{;config}        ; Connects line n to simh generic serial port N (port list visible with the sim> SHOW SERIAL command), the optional ";config" data specifies the speed, parity and stop bits for the connection
                                                           ; DTR (and RTS) will be raised at attach time and will drop at detach/disconnect time
        sim> attach MX Line=n,Connect=host:port{;notelnet} ; Causes a connection to be established to the designated host:port.  The actual connection will happen in a non-blocking fashion and will be completed and/or re-established by the normal tmxr_poll_conn activities

     All connections configured for any multiplexer device are unconfigured by:

        sim> detach MX                              ; detaches ALL connections/ports/sessions on the MUX.

    Console serial connections are achieved by:

        sim> set console serial=serN{;config}
    or
        sim> set console serial=COM2{;config}

    A line specific listening port (12366) can be specified by the following:

        sim> attach MUX Line=2,12366

    A line specific remote telnet (or raw tcp) destination can be specified
    by the following:

        sim> attach MUX Line=2,Connect=remotehost:port

    If a connection to a remotehost:port wants a raw binary data channel
    (instead of a telnet session) the following would be used:

        sim> attach MUX Line=2,Connect=remotehost:port;notelnet

    A single line multiplexor can indicate any of the above line options
    without specifying a line number:

        sim> attach MUX Connect=ser0;9600-8N1
        sim> attach MUX 12366{;notelnet}|{;nomessage}
        sim> attach MUX Connect=remotehost:port
        sim> attach MUX Connect=remotehost:port;notelnet

    A multiplexor can disconnect all (telnet, serial and outgoing) previous
    attachments with:

        sim> detach MUX

   A device emulation may choose to implement a command interface to
   disconnect specific individual lines.  This would usually be done via
   a Unit Modifier table entry (MTAB) which dispatches the command
   "SET dev DISCONNECT[=line]" to tmxr_dscln.  This will cause a telnet
   connection to be closed, but a serial port will normally have DTR
   dropped for 500ms and raised again (thus hanging up a modem on that
   serial port).

     sim> set MUX disconnect=2

    Full Modem Control serial port support.

    This library supports devices which wish to emulate full modem
    control/signalling for serial ports.  Any device emulation which wishes
    to support this functionality for attached serial ports must call
    "tmxr_set_modem_control_passthru" before any call to tmxr_attach.
    This disables automatic DTR (&RTS) manipulation by this library.
    Responsibility for manipulating DTR falls on the simulated operating
    system.  Calling tmxr_set_modem_control_passthru would usually be in
    a device reset routine.  It may also be called by a device attach
    routine based on user specified options.
    Once support for full modem control has been declared by a device
    emulation for a particular TMXR device, this library will make no
    direct effort to manipulate modem bits while connected to serial ports.
    The "tmxr_set_get_modem_bits" API exists to allow the device emulation
    layer to query and control modem signals.  The "tmxr_set_config_line"
    API exists to allow the device emulation layer to change port settings
    (baud rate, parity and stop bits).  A modem_control enabled line
    merely passes the VM's port status bits, data and settings through to
    and from the serial port.

    The "tmxr_set_get_modem_bits" and "tmxr_set_config_line" APIs will
    ONLY work on a modem control enabled TMXR device.

*/

#define NOT_MUX_USING_CODE /* sim_tmxr library define */

#include "sim_defs.h"
#include "sim_scp_private.h"
#include "sim_serial.h"
#include "sim_sock.h"
#include "sim_timer.h"
#include "sim_tmxr.h"
#include "sim_ether.h"
#include "scp.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/* Telnet protocol constants - negatives are for init'ing signed char data */

/* Commands */
#define TN_IAC          0xFFu /* -1 */                  /* protocol delim */
#define TN_DONT         0xFEu /* -2 */                  /* dont */
#define TN_DO           0xFDu /* -3 */                  /* do */
#define TN_WONT         0xFCu /* -4 */                  /* wont */
#define TN_WILL         0xFBu /* -5 */                  /* will */
#define TN_SB           0xFAu /* -6 */                  /* sub-option negotiation */
#define TN_GA           0xF9u /* -7 */                  /* go ahead */
#define TN_EL           0xF8u /* -8 */                  /* erase line */
#define TN_EC           0xF7u /* -9 */                  /* erase character */
#define TN_AYT          0xF6u /* -10 */                 /* are you there */
#define TN_AO           0xF5u /* -11 */                 /* abort output */
#define TN_IP           0xF4u /* -12 */                 /* interrupt process */
#define TN_BRK          0xF3u /* -13 */                 /* break */
#define TN_DATAMK       0xF2u /* -14 */                 /* data mark */
#define TN_NOP          0xF1u /* -15 */                 /* no operation */
#define TN_SE           0xF0u /* -16 */                 /* end sub-option negot */

/* Options */

#define TN_BIN            0                             /* bin */
#define TN_ECHO           1                             /* echo */
#define TN_SGA            3                             /* sga */
#define TN_STATUS         5                             /* option status query */
#define TN_TIMING         6                             /* Timing Mark */
#define TN_NAOCRD        10                             /* Output Carriage-Return Disposition */
#define TN_NAOHTS        11                             /* Output Horizontal Tab Stops */
#define TN_NAOHTD        12                             /* Output Horizontal Tab Stop Disposition */
#define TN_NAOFFD        13                             /* Output Formfeed Disposition */
#define TN_NAOVTS        14                             /* Output Vertical Tab Stop */
#define TN_NAOVTD        15                             /* Output Vertical Tab Stop Disposition */
#define TN_NAOLFD        16                             /* Output Linefeed Disposition */
#define TN_EXTEND        17                             /* Extended Ascii */
#define TN_LOGOUT        18                             /* Logout */
#define TN_BM            19                             /* Byte Macro */
#define TN_DET           20                             /* Data Entry Terminal */
#define TN_SENDLO        23                             /* Send Location */
#define TN_TERMTY        24                             /* Terminal Type */
#define TN_ENDREC        25                             /* Terminal Type */
#define TN_TUID          26                             /* TACACS User Identification */
#define TN_OUTMRK        27                             /* Output Marking */
#define TN_TTYLOC        28                             /* Terminal Location Number */
#define TN_3270          29                             /* 3270 Regime */
#define TN_X3PAD         30                             /* X.3 PAD */
#define TN_NAWS          31                             /* Negotiate About Window Size */
#define TN_TERMSP        32                             /* Terminal Speed */
#define TN_TOGFLO        33                             /* Remote Flow Control */
#define TN_LINE          34                             /* line mode */
#define TN_XDISPL        35                             /* X Display Location */
#define TN_ENVIRO        36                             /* Environment */
#define TN_AUTH          37                             /* Authentication */
#define TN_ENCRYP        38                             /* Data Encryption */
#define TN_NEWENV        39                             /* New Environment */
#define TN_TN3270        40                             /* TN3270 Enhancements */
#define TN_CHARST        42                             /* CHARSET */
#define TN_COMPRT        44                             /* Com Port Control */
#define TN_KERMIT        47                             /* KERMIT */

#define TN_CR           015                             /* carriage return */
#define TN_LF           012                             /* line feed */
#define TN_NUL          000                             /* null */

/* Telnet line states */

#define TNS_NORM        000                             /* normal */
#define TNS_IAC         001                             /* IAC seen */
#define TNS_WILL        002                             /* WILL seen */
#define TNS_WONT        003                             /* WONT seen */
#define TNS_SKIP        004                             /* skip next cmd */
#define TNS_CRPAD       005                             /* CR padding */
#define TNS_DO          006                             /* DO request pending rejection */

/* Telnet Option Sent Flags */

#define TNOS_DONT       001                             /* Don't has been sent */
#define TNOS_WONT       002                             /* Won't has been sent */

/* Lifted from ddcmp.c in the framer firmware */
struct status_msg_t
{
    uint8 dc1;
    uint8 on;                     /* "on" flags */
    uint16 mflags;
    uint32 speed;
    uint32 txspeed;
    uint32 rxframes;
    uint32 rxbytes;
    uint32 txframes;
    uint32 txbytes;
    uint32 hcrc_err;
    uint32 crc_err;
    uint32 len_err;
    uint32 nobuf_err;
    uint32 last_cmd_sts;          /* Response code from last command */
    uint32 freq;                  /* Measured frequency */
    char version[64];
};
#define ON_ACT 1
#define ON_SYN 2
#define ON_CLKOK 4

/* This struct is internal to sim_tmxr, used when a line is attached
 * to a DDCMP synchronous framer device (a USB peripheral that looks
 * like an Ethernet interface).
 */
typedef struct framer_data {
    ETH_DEV *eth;                   /* Ethernet device pointer if framer */
    uint16 fmode;                   /* Framer mode from attach command */
    uint32 fspeed;                  /* Framer link speed from attach command */
    struct status_msg_t status;     /* Last received status message */
    int status_cnt;                 /* Count of status messages seen */
    t_bool connect_pending;         /* True if connected not yet reported */
} FRAMER;

static BITFIELD tmxr_modem_bits[] = {
  BIT(DTR),                                 /* Data Terminal Ready */
  BIT(RTS),                                 /* Request To Send     */
  BIT(DCD),                                 /* Data Carrier Detect */
  BIT(RNG),                                 /* Ring Indicator      */
  BIT(CTS),                                 /* Clear To Send       */
  BIT(DSR),                                 /* Data Set Ready      */
  ENDBITS
};

static u_char mantra[] = {                  /* Telnet Option Negotiation Mantra */
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

#define TMXR_GUARD  ((int32)(lp->serport ? 1 : sizeof(mantra)))/* buffer guard */

#define TMXR_LINE_DISABLED (-1)

/* Local routines */
static void tmxr_setup_framer(TMLN *line, ETH_PACK *packet, int len);
static int  tmxr_framer_read (TMLN *line, char *buf, int nbytes);
static int  tmxr_framer_write (TMLN *line, const char *buf, int32 length);

static void tmxr_add_to_open_list (TMXR* mux);

/* Initialize the line state.

   Reset the line state to represent an idle line.  Note that we do not clear
   all of the line structure members, so a connected line remains connected
   after this call.

   Because a line break is represented by a flag in the "receive break status"
   array, we must zero that array in order to clear any pending break
   indications.
*/

static void tmxr_init_line (TMLN *lp)
{
lp->tsta = 0;                                           /* init telnet state */
lp->xmte = 1;                                           /* enable transmit */
lp->dstb = 0;                                           /* default bin mode */
lp->rxbpr = lp->rxbpi = lp->rxcnt = lp->rxpcnt = 0;     /* init receive indexes */
if (!lp->txbfd || lp->notelnet)                         /* if not buffered telnet */
    lp->txbpr = lp->txbpi = lp->txcnt = lp->txpcnt = 0; /*   init transmit indexes */
lp->txdrp = lp->txstall = 0;
tmxr_set_get_modem_bits (lp, 0, 0, NULL);
if (lp->mp && (!lp->mp->buffered) && (!lp->txbfd)) {
    lp->txbfd = 0;
    lp->txbsz = TMXR_MAXBUF;
    lp->txb = (char *)realloc (lp->txb, lp->txbsz);
    lp->rxbsz = TMXR_MAXBUF;
    lp->rxb = (char *)realloc(lp->rxb, lp->rxbsz);
    lp->rbr = (char *)realloc(lp->rbr, lp->rxbsz);
    }
if (lp->loopback) {
    lp->lpbsz = lp->rxbsz;
    lp->lpb = (char *)realloc(lp->lpb, lp->lpbsz);
    lp->lpbcnt = lp->lpbpi = lp->lpbpr = 0;
    }
if (lp->rxpb) {
    lp->rxpboffset = lp->rxpbsize = 0;
    free (lp->rxpb);
    lp->rxpb = NULL;
    }
if (lp->txpb) {
    lp->txpbsize = lp->txppsize = lp->txppoffset = 0;
    free (lp->txpb);
    lp->txpb = NULL;
    }
memset (lp->rbr, 0, lp->rxbsz);                         /* clear break status array */
}


/* Report a connection to a line.

   If the indicated line (lp) is speaking the telnet wire protocol, a
   notification of the form:

      Connected to the <sim> simulator <dev> device, line <n>

   is sent to the newly connected line.  If the device has only one line, the
   "line <n>" part is omitted.  If the device has not been defined, the "<dev>
   device" part is omitted.

*/

static void tmxr_report_connection (TMXR *mp, TMLN *lp)
{
int32 unwritten, psave;
char cmsg[160];
char dmsg[80] = "";
char lmsg[80] = "";
char msgbuf[512] = "";

if (((!lp->notelnet) && (!lp->nomessage)) || (sim_switches & SWMASK ('V'))) {
    sprintf (cmsg, "\n\r\nConnected to the %s simulator ", sim_name);

    if (mp->dptr) {                                     /* device defined? */
        sprintf (dmsg, "%s device",                     /* report device name */
                       sim_dname (mp->dptr));

        if (mp->lines > 1)                              /* more than one line? */
            sprintf (lmsg, ", line %d", (int)(lp-mp->ldsc));/* report the line number */
        }

    sprintf (msgbuf, "%s%s%s\r\n\n", cmsg, dmsg, lmsg);
    }

if (!mp->buffered) {
    lp->txbpi = 0;                                      /* init buf pointers */
    lp->txbpr = (int32)(lp->txbsz - strlen (msgbuf));
    lp->rxcnt = lp->txcnt = lp->txdrp = lp->txstall = 0;/* init counters */
    lp->rxpcnt = lp->txpcnt = 0;
    }
else
    if (lp->txcnt > lp->txbsz)
        lp->txbpr = (lp->txbpi + 1) % lp->txbsz;
    else
        lp->txbpr = (int32)(lp->txbsz - strlen (msgbuf));

psave = lp->txbpi;                                      /* save insertion pointer */
lp->txbpi = lp->txbpr;                                  /* insert connection message */
if ((lp->serport) && (!sim_is_running)) {
    sim_os_ms_sleep (TMXR_DTR_DROP_TIME);               /* Wait for DTR to be noticed */
    lp->ser_connect_pending = FALSE;                    /* Mark line as ready for action */
    lp->conn = TRUE;
    }
tmxr_linemsg (lp, msgbuf);                              /* beginning of buffer */
lp->txbpi = psave;                                      /* restore insertion pointer */

unwritten = tmxr_send_buffered_data (lp);               /* send the message */

if ((lp->serport) && (!sim_is_running)) {
    lp->ser_connect_pending = TRUE;                     /* Mark line as not yet ready for action */
    lp->conn = FALSE;
    }
if (unwritten == 0)                                     /* buffer now empty? */
    lp->xmte = 1;                                       /* reenable transmission if paused */

lp->txcnt -= (int32)strlen (msgbuf);                    /* adjust statistics */
}


/* Report a disconnection to a line.

   A notification of the form:

      Disconnected from the <sim> simulator

   is sent to the line about to be disconnected.  We do not flush the buffer
   here, because the disconnect routines will do that just after calling us.
*/

static void tmxr_report_disconnection (TMLN *lp)
{
if (lp->notelnet || lp->nomessage)
    return;
tmxr_linemsgf (lp, "\r\nDisconnected from the %s simulator\r\n\n", sim_name);/* report disconnection */
}

static int32 loop_write_ex (TMLN *lp, char *buf, int32 length, t_bool prefix_datagram)
{
int32 written = 0;
int32 loopfree = lp->lpbsz - lp->lpbcnt;

if (lp->datagram && prefix_datagram) {
    if ((size_t)loopfree < (size_t)(length + sizeof(length)))
        return written;
    loop_write_ex (lp, (char *)&length, sizeof(length), FALSE);
    }
while (length) {
    int32 chunksize;

    loopfree = lp->lpbsz - lp->lpbcnt;
    if (loopfree == 0)
        break;
    if (loopfree < length)
        length = loopfree;
    if (lp->lpbpi >= lp->lpbpr)
        chunksize = lp->lpbsz - lp->lpbpi;
    else
        chunksize = lp->lpbpr - lp->lpbpi;
    if (chunksize > length)
        chunksize = length;
    memcpy (&lp->lpb[lp->lpbpi], buf, chunksize);
    buf += chunksize;
    length -= chunksize;
    written += chunksize;
    lp->lpbpi = (lp->lpbpi + chunksize) % lp->lpbsz;
    }
lp->lpbcnt += written;
return written;
}

static int32 loop_write (TMLN *lp, char *buf, int32 length)
{
return loop_write_ex (lp, buf, length, TRUE);
}

static int32 loop_read_ex (TMLN *lp, char *buf, int32 bufsize)
{
int32 bytesread = 0;

while (bufsize > 0) {
    int32 chunksize;
    int32 loopused = lp->lpbcnt;

    if (loopused < bufsize)
        bufsize = loopused;
    if (loopused == 0)
        break;
    if (lp->lpbpi > lp->lpbpr)
        chunksize = lp->lpbpi - lp->lpbpr;
    else
        chunksize = lp->lpbsz - lp->lpbpr;
    if (chunksize > bufsize)
        chunksize = bufsize;
    memcpy (buf, &lp->lpb[lp->lpbpr], chunksize);
    buf += chunksize;
    bufsize -= chunksize;
    bytesread += chunksize;
    lp->lpbpr = (lp->lpbpr + chunksize) % lp->lpbsz;
    }
lp->lpbcnt -= bytesread;
return bytesread;
}

static int32 loop_read (TMLN *lp, char *buf, int32 bufsize)
{
if (lp->datagram) {
    int32 pktsize;

    if (lp->lpbcnt < (int32)sizeof(pktsize))
        return 0;
    if ((sizeof(pktsize) != loop_read_ex (lp, (char *)&pktsize, sizeof(pktsize))) ||
        (pktsize > bufsize))
        return -1;
    bufsize = pktsize;
    }
return loop_read_ex (lp, buf, bufsize);
}

/* Read from a line.

   Up to "length" characters are read into the character buffer associated with
   line "lp".  The actual number of characters read is returned.  If no
   characters are available, 0 is returned.  If an error occurred while reading,
   -1 is returned.

   If a line break was detected on serial input, the associated receive break
   status flag will be set.  Line break indication for Telnet connections is
   embedded in the Telnet protocol and must be determined externally.
*/

static int32 tmxr_read (TMLN *lp, int32 length)
{
int32 i = lp->rxbpi;

if (lp->loopback)
    return loop_read (lp, &(lp->rxb[i]), length);
if (lp->serport)                                        /* serial port connection? */
    return sim_read_serial (lp->serport, &(lp->rxb[i]), length, &(lp->rbr[i]));
else {
    if (lp->framer)
        return tmxr_framer_read (lp,  &(lp->rxb[i]), length);
    else                                                    /* Telnet connection */
        return sim_read_sock (lp->sock, &(lp->rxb[i]), length);
    }
}


/* Write to a line.

   Up to "length" characters are written from the character buffer associated
   with "lp".  The actual number of characters written is returned.  If an error
   occurred while writing, -1 is returned.
*/

static int32 tmxr_write (TMLN *lp, int32 length)
{
int32 written = 0;
int32 i = lp->txbpr;

if ((lp->txbps) && (sim_gtime () < lp->txnexttime) && (sim_is_running))
    return 0;

if (lp->loopback)
    return loop_write (lp, &(lp->txb[i]), length);

if (lp->serport) {                                      /* serial port connection? */
    written = sim_write_serial (lp->serport, &(lp->txb[i]), length);
    }
else {
    if (lp->framer)
        written = tmxr_framer_write (lp,  &(lp->txb[i]), length);
    else {
        if (lp->sock) {                                     /* Telnet connection */
            written = sim_write_sock (lp->sock, &(lp->txb[i]), length);

            if (written == SOCKET_ERROR) {                  /* did an error occur? */
                lp->txdone = TRUE;
                if (lp->datagram)
                    return written;                         /* ignore errors on datagram sockets */
                else
                    return -1;                              /* return error indication */
                }
            }
        else {
            if (lp->console)
                written = (SCPE_OK == _sim_os_putchar (lp->txb[i])) ? 1 : 0; /* write to the sim> session */
            else {
                if ((lp->conn == TMXR_LINE_DISABLED) ||
                    ((lp->conn == 0) && lp->txbfd)){
                    written = length;                           /* Count here output timing is correct */
                    if (lp->conn == TMXR_LINE_DISABLED)
                        lp->txdrp += length;                    /* Record as having been dropped on the floor */
                    }
                }
            }
        }
    }
if (written > 0) {
    lp->txdone = FALSE;
    if ((lp->txbps) && (sim_is_running))
        lp->txnexttime = floor (sim_gtime () + ((written * lp->txdeltausecs * sim_timer_inst_per_sec ()) / USECS_PER_SECOND));
    }
return written;
}


/* Remove a character from the read buffer.

   The character at position "p" in the read buffer associated with line "lp" is
   removed by moving all of the following received characters down one position.
   The receive break status array is adjusted accordingly.
*/

static void tmxr_rmvrc (TMLN *lp, int32 p)
{
for ( ; p < lp->rxbpi; p++) {                           /* work from "p" through end of buffer */
    lp->rxb[p] = lp->rxb[p + 1];                        /* slide following character down */
    lp->rbr[p] = lp->rbr[p + 1];                        /* adjust break status too */
    }

lp->rbr[p] = 0;                                         /* clear potential break from vacated slot */
lp->rxbpi = lp->rxbpi - 1;                              /* drop buffer insert index */
}


/* Find a line descriptor indicated by unit or number.

   If "uptr" is NULL, then the line descriptor is determined by the line number
   passed in "val".  If "uptr" is not NULL, then it must point to a unit
   associated with a line, and the line descriptor is determined by the unit
   number, which is derived by the position of the unit in the device's unit
   array.

   Note: This routine may be called with a UNIT that does not belong to the
   device indicated in the TMXR structure.  That is, the multiplexer lines may
   belong to a device other than the one attached to the socket (the HP 2100 MUX
   device is one example).  Therefore, we must look up the device from the unit
   at each call, rather than depending on the DEVICE pointer stored in the TMXR.
*/

static TMLN *tmxr_find_ldsc (UNIT *uptr, int32 val, const TMXR *mp)
{
if (mp == NULL)                                         /* invalid multiplexer descriptor? */
    return NULL;                                        /* programming error! */
if (uptr) {                                             /* called from SET? */
    DEVICE *dptr = find_dev_from_unit (uptr);           /* find device */
    if (dptr == NULL)                                   /* what?? */
        return NULL;
    val = (int32) (uptr - dptr->units);                 /* implicit line # */
    }
if ((val < 0) || (val >= mp->lines))                    /* invalid line? */
    return NULL;
return mp->ldsc + val;                                  /* line descriptor */
}


/* Get a line descriptor indicated by a string or unit.

   A pointer to the line descriptor associated with multiplexer "mp" and unit
   "uptr" or specified by string "cptr" is returned.  If "uptr" is non-null,
   then the unit number within its associated device implies the line number.
   If "uptr" is null, then the string "cptr" is parsed for a decimal line
   number.  If the line number is missing, malformed, or outside of the range of
   line numbers associated with "mp", then NULL is returned with status set to
   SCPE_ARG.

   Implementation note:

    1. A return status of SCPE_IERR implies a programming error (passing an
       invalid pointer or an invalid unit).
*/

static TMLN *tmxr_get_ldsc (UNIT *uptr, const char *cptr, TMXR *mp, t_stat *status)
{
t_value  ln;
TMLN    *lp = NULL;
t_stat   code = SCPE_OK;

if (mp == NULL)                                         /* missing mux descriptor? */
    code = SCPE_IERR;                                   /* programming error! */

else if (uptr) {                                        /* implied line form? */
    lp = tmxr_find_ldsc (uptr, mp->lines, mp);          /* determine line from unit */

    if (lp == NULL)                                     /* invalid line number? */
        code = SCPE_IERR;                               /* programming error! */
    }

else if (cptr == NULL)                                  /* named line form, parameter supplied? */
    code = SCPE_MISVAL;                                 /* no, so report missing */

else {
    ln = get_uint (cptr, 10, mp->lines - 1, &code);     /* get line number */

    if (code == SCPE_OK)                                /* line number OK? */
        lp = mp->ldsc + (int32) ln;                     /* use as index to determine line */
    }

if (status)                                             /* return value pointer supplied? */
    *status = code;                                     /* store return status value */

return lp;                                              /* return pointer to line descriptor */
}

/* Generate the Attach string which will fully configure the multiplexer

   Inputs:
        old     =       pointer to the original configuration string which will be replaced
        *mp     =       pointer to multiplexer

   Output:
        a complete attach string for the current state of the multiplexer

*/
static char *growstring(char **string, size_t growth)
{
*string = (char *)realloc (*string, 1 + (*string ? strlen (*string) : 0) + growth);
return *string + strlen(*string);
}

static char *tmxr_mux_attach_string(char *old, TMXR *mp)
{
char* tptr = NULL;
int32 i;
TMLN *lp;

free (old);
tptr = (char *) calloc (1, 1);

if (tptr == NULL)                                       /* no more mem? */
    return tptr;

if (mp->port) {                                         /* copy port */
    char backlog[32] = "";

    if (mp->backlog != 0)
        snprintf (backlog, sizeof (backlog), ";backlog=%u", mp->backlog);
    sprintf (growstring(&tptr, 33 + strlen (mp->port)), "%s%s%s", mp->port, backlog,
                                                                mp->notelnet ? ";notelnet" :
                                                                               (mp->nomessage ? ";nomessage" :
                                                                                                ""));
    if (mp->acl) {                                      /* copy acl in pieces */
        char gbuf[CBUFSIZE];
        const char *c = mp->acl;

        while (*c != '\0') {
            c = get_glyph_nc (c, gbuf, ',');
            sprintf (growstring(&tptr, 9 + strlen (gbuf)), ";%s=%s", (gbuf[0] == '+') ? "Accept" : "Reject", gbuf + 1);
            }
        }
    }
if (mp->logfiletmpl[0])                                 /* logfile info */
    sprintf (growstring(&tptr, 7 + strlen (mp->logfiletmpl)), ",Log=%s", mp->logfiletmpl);
if (mp->buffered)
    sprintf (growstring(&tptr, 10 + 10), ",Buffered=%d", mp->buffered);
while ((*tptr == ',') || (*tptr == ' '))
    memmove (tptr, tptr+1, strlen(tptr+1)+1);
for (i=0; i<mp->lines; ++i) {
    char *lptr;
    lp = mp->ldsc + i;

    lptr = tmxr_line_attach_string(lp);
    if (lptr) {
        sprintf (growstring(&tptr, 10+strlen(lptr)), "%s%s", *tptr ? "," : "", lptr);
        free (lptr);
        }
    }
if (mp->lines == 1)
    while ((*tptr == ',') || (*tptr == ' '))
        memmove (tptr, tptr+1, strlen(tptr+1)+1);
if (*tptr == '\0') {
    free (tptr);
    tptr = NULL;
    }
return tptr;
}



/* Global routines */


/* Return the Line specific attach setup currently configured for a given line

   Inputs:
        *lp     =       pointer to terminal line descriptor
   Outputs:
        a string which can be used to reconfigure the line,
        NULL if the line isn't configured

   Note: The returned string is dynamically allocated memory and must be freed
         when it is no longer needed by calling free

*/

char *tmxr_line_attach_string(TMLN *lp)
{
char* tptr = NULL;

tptr = (char *) calloc (1, 1);

if (tptr == NULL)                                       /* no more mem? */
    return tptr;

if (lp->destination || lp->port || lp->txlogname || (lp->conn == TMXR_LINE_DISABLED) || lp->console) {
    if ((lp->mp->lines > 1) || (lp->port))
        sprintf (growstring(&tptr, 32), "Line=%d", (int)(lp-lp->mp->ldsc));
    if (lp->console)
        sprintf (growstring(&tptr, 32), "CONSOLE");
    if (lp->conn == TMXR_LINE_DISABLED)
        sprintf (growstring(&tptr, 32), ",Disabled");
    if (lp->modem_control != lp->mp->modem_control)
        sprintf (growstring(&tptr, 32), ",%s", lp->modem_control ? "Modem" : "NoModem");
    if (lp->txbfd && (lp->txbsz != lp->mp->buffered))
        sprintf (growstring(&tptr, 32), ",Buffered=%d", lp->txbsz);
    if (!lp->txbfd && (lp->mp->buffered > 0))
        sprintf (growstring(&tptr, 32), ",UnBuffered");
    if (lp->mp->datagram != lp->datagram)
        sprintf (growstring(&tptr, 8), ",%s", lp->datagram ? "UDP" : "TCP");
    if (lp->mp->packet != lp->packet)
        sprintf (growstring(&tptr, 8), ",Packet");
    if (lp->port) {
        char backlog[32] = "";

        if (lp->backlog != 0)
            snprintf (backlog, sizeof (backlog), ";backlog=%u", lp->backlog);
        sprintf (growstring(&tptr, 64 + strlen (lp->port)), ",%s%s%s%s", lp->port, backlog,
                                                                       ((lp->mp->notelnet != lp->notelnet) && (!lp->datagram)) ? (lp->notelnet ? ";notelnet" : ";telnet") : "",
                                                                       ((lp->mp->nomessage != lp->nomessage) && (!lp->datagram)) ? (lp->nomessage ? ";nomessage" : ";message") : "");
        if (lp->acl) {                                      /* copy acl in pieces */
            char gbuf[CBUFSIZE];
            const char *c = lp->acl;

            while (*c != '\0') {
                c = get_glyph_nc (c, gbuf, ',');
                sprintf (growstring(&tptr, 9 + strlen (gbuf)), ";%s=%s", (gbuf[0] == '+') ? "Accept" : "Reject", gbuf + 1);
                }
            }
        }
    if (lp->destination) {
        if (lp->serport) {
            char portname[CBUFSIZE];

            get_glyph_nc (lp->destination, portname, ';');
            sprintf (growstring(&tptr, 25 + strlen (lp->destination)), ",Connect=%s%s%s", portname, strcmp("9600-8N1", lp->serconfig ? lp->serconfig : "") ? ";" : "", strcmp("9600-8N1", lp->serconfig ? lp->serconfig : "") ? lp->serconfig : "");
            }
        else
            sprintf (growstring(&tptr, 25 + strlen (lp->destination)), ",Connect=%s%s", lp->destination, ((lp->mp->notelnet != lp->notelnet) && (!lp->datagram)) ? (lp->notelnet ? ";notelnet" : ";telnet") : "");
        }
    if (lp->txlogname)
        sprintf (growstring(&tptr, 12 + strlen (lp->txlogname)), ",Log=%s", lp->txlogname);
    if (lp->loopback)
        sprintf (growstring(&tptr, 12 ), ",Loopback");
    }
if (*tptr == '\0') {
    free (tptr);
    tptr = NULL;
    }
return tptr;
}

/*

Set the connection polling interval

*/
t_stat tmxr_connection_poll_interval (TMXR *mp, uint32 seconds)
{
if (0 == seconds)
    return SCPE_ARG;
mp->poll_interval = seconds;
return SCPE_OK;
}

/* Poll for new connection

   Called from unit service routine to test for new connection

   Inputs:
        *mp     =       pointer to terminal multiplexer descriptor
   Outputs:
        line number activated, -1 if none

   If a connection order is defined for the descriptor, and the first value is
   not -1 (indicating default order), then the order array is used to find an
   open line.  Otherwise, a search is made of all lines in numerical sequence.

*/

int32 tmxr_poll_conn (TMXR *mp)
{
SOCKET newsock;
TMLN *lp;
int32 *op;
int32 i, j;
int32 ringing = -1;
char *address;
char msg[512];
uint32 poll_time = sim_os_msec ();

memset (msg, 0, sizeof (msg));
if (mp->last_poll_time == 0) {                          /* first poll initializations */
    UNIT *uptr = mp->uptr;

    if (!uptr)                                          /* Attached ? */
        return -1;                                      /* No connections are possible! */

    uptr->tmxr = (void *)mp;                            /* Connect UNIT to TMXR */
    uptr->dynflags |= UNIT_TM_POLL;                     /* Tag as polling unit */

    if (mp->poll_interval == 0)                         /* Assure reasonable polling interval */
        mp->poll_interval = TMXR_DEFAULT_CONNECT_POLL_INTERVAL;

    for (i=0; i < mp->lines; i++) {
        if (mp->ldsc[i].uptr) {
            mp->ldsc[i].uptr->tmxr = (void *)mp;        /* Connect UNIT to TMXR */
            mp->ldsc[i].uptr->dynflags |= UNIT_TM_POLL; /* Tag as polling unit */
            }
        else
            mp->ldsc[i].uptr = uptr;                    /* default line input polling to primary poll unit */
        if (mp->ldsc[i].o_uptr) {
            mp->ldsc[i].o_uptr->tmxr = (void *)mp;      /* Connect UNIT to TMXR */
            mp->ldsc[i].o_uptr->dynflags |= UNIT_TM_POLL;/* Tag as polling unit */
            }
        else
            mp->ldsc[i].o_uptr = uptr;                  /* default line output polling to primary poll unit */
        }
    }

if (sim_is_running &&
    ((poll_time - mp->last_poll_time) < mp->poll_interval*1000))
    return -1;                                          /* too soon to try */

srand((unsigned int)poll_time);
tmxr_debug_trace (mp, "tmxr_poll_conn()");

mp->last_poll_time = poll_time;

/* Check for a pending Telnet/tcp connection */

if (mp->master) {
    if (mp->ring_sock != INVALID_SOCKET) {  /* Use currently 'ringing' socket if one is active */
        newsock = mp->ring_sock;
        mp->ring_sock = INVALID_SOCKET;
        address = mp->ring_ipad;
        mp->ring_ipad = NULL;
        }
    else
        newsock = sim_accept_conn_ex (mp->master, &address, (mp->packet ? SIM_SOCK_OPT_NODELAY : 0));/* poll connect */

    if (newsock != INVALID_SOCKET) {                    /* got a live one? */
        snprintf (msg, sizeof (msg) - 1, "tmxr_poll_conn() - Connection from %s", address);
        tmxr_debug_connect (mp, msg);
        op = mp->lnorder;                               /* get line connection order list pointer */
        i = mp->lines;                                  /* play it safe in case lines == 0 */
        ++mp->sessions;                                 /* count the new session */

        if (mp->acl) {
            if (sim_addr_acl_check (address, mp->acl) != 0) {
                tmxr_debug_connect (mp, "tmxr_poll_conn() - Connection Specifically rejected by ACL");
                sim_close_sock (newsock);
                newsock = INVALID_SOCKET;
                free (address);
                ++mp->acl_rejected_sessions;
                }
            else
                ++mp->acl_accepted_sessions;
            }

        if (newsock != INVALID_SOCKET) {
            for (j = 0; j < mp->lines; j++, i++) {          /* find next avail line */
                if (op && (*op >= 0) && (*op < mp->lines))  /* order list present and valid? */
                    i = *op++;                              /* get next line in list to try */
                else                                        /* no list or not used or range error */
                    i = j;                                  /* get next sequential line */

                lp = mp->ldsc + i;                          /* get pointer to line descriptor */
                if ((lp->conn == FALSE) &&                  /* is the line available? */
                    (lp->destination == NULL) &&
                    (lp->master == 0) &&
                    (lp->ser_connect_pending == FALSE) &&
                    (lp->modem_control ? ((lp->modembits & TMXR_MDM_DTR) != 0) : TRUE))
                    break;                                  /* yes, so stop search */
                }

            if (i >= mp->lines) {                           /* all busy? */
                int32 ringable_count = 0;

                for (j = 0; j < mp->lines; j++, i++) {      /* find next avail line */
                    lp = mp->ldsc + j;                      /* get pointer to line descriptor */
                    if (lp->framer)
                        continue;

                    if ((lp->conn == FALSE) &&              /* is the line available? */
                        (lp->destination == NULL) &&
                        (lp->master == 0) &&
                        (lp->ser_connect_pending == FALSE) &&
                        ((lp->modembits & TMXR_MDM_DTR) == 0)) {
                        ++ringable_count;
                        lp->modembits |= TMXR_MDM_RNG;
                        tmxr_debug_connect_line (lp, "tmxr_poll_conn() - Ringing line");
                        }
                    }
                if (ringable_count > 0) {
                    ringing = -2;
                    if (mp->ring_start_time == 0) {
                        mp->ring_start_time = poll_time;
                        mp->ring_sock = newsock;
                        mp->ring_ipad = address;
                        }
                    else {
                        if ((poll_time - mp->ring_start_time) < TMXR_MODEM_RING_TIME*1000) {
                            mp->ring_sock = newsock;
                            mp->ring_ipad = address;
                            }
                        else {                                      /* Timeout waiting for DTR */
                            int ln;

                            /* turn off pending ring signals */
                            for (ln = 0; ln < lp->mp->lines; ln++) {
                                TMLN *tlp = lp->mp->ldsc + ln;
                                if (((tlp->destination == NULL) && (tlp->master == 0)) &&
                                    (tlp->modembits & TMXR_MDM_RNG) && (tlp->conn == FALSE))
                                    tlp->modembits &= ~TMXR_MDM_RNG;
                                }
                            mp->ring_start_time = 0;
                            tmxr_msg (newsock, "No answer on any connection\r\n");
                            tmxr_debug_connect (mp, "tmxr_poll_conn() - No Answer - All connections busy");
                            sim_close_sock (newsock);
                            free (address);
                            }
                        }
                    }
                else {
                    tmxr_msg (newsock, "All connections busy\r\n");
                    tmxr_debug_connect (mp, "tmxr_poll_conn() - All connections busy");
                    sim_close_sock (newsock);
                    free (address);
                    }
                }
            else {
                lp = mp->ldsc + i;                          /* get line desc */
                lp->conn = TRUE;                            /* record connection */
                lp->sock = newsock;                         /* save socket */
                free (address);
                sim_getnames_sock (lp->sock, NULL, &address);
                lp->ipad = address;                         /* ip address */
                tmxr_init_line (lp);                        /* init line */
                lp->notelnet = mp->notelnet;                /* apply mux default telnet setting */
                lp->nomessage = mp->nomessage;              /* apply mux default telnet setting */
                if (!lp->notelnet) {
                    sim_write_sock (newsock, (char *)mantra, sizeof(mantra));
                    tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                    lp->telnet_sent_opts = (uint8 *)realloc (lp->telnet_sent_opts, 256);
                    memset (lp->telnet_sent_opts, 0, 256);
                    }
                tmxr_report_connection (mp, lp);
                lp->cnms = sim_os_msec ();                  /* time of connection */
                return i;
                }
            }
        }                                               /* end if newsock */
    }

/* Look for per line listeners or outbound connecting sockets */
for (i = 0; i < mp->lines; i++) {                       /* check each line in sequence */
    int j, r = rand();
    lp = mp->ldsc + i;                                  /* get pointer to line descriptor */

    /* Check for pending serial port connection notification */

    if (lp->ser_connect_pending) {
        lp->ser_connect_pending = FALSE;
        lp->conn = TRUE;
        return i;
        }

    /* Framer: report connected. */
    if (lp->framer) {
        if (lp->framer->connect_pending) {
            /* Say "connected" when first asked */
            lp->framer->connect_pending = FALSE;
            lp->conn = TRUE;                            /* record connection */
            return i;
            }
        continue;
        }

    /* Don't service network connections for loopbacked lines */

    if (lp->loopback)
        continue;

    /* If two simulators are configured with symmetric virtual null modem
       cables pointing at each other, there may be a problem establishing
       a connection if both systems happen to be checking for the success
       of their connections in the exact same order.  They can each observe
       success in their respective outgoing connections, which haven't
       actually been 'accept'ed on the peer end of the connection.
       We address this issue by checking for the success of an outgoing
       connection and the arrival of an incoming one in a random order.
     */
    for (j=0; j<2; j++)
        switch ((j+r)&1) {
            case 0:
                if (lp->connecting) {                           /* connecting? */
                    char *sockname, *peername;

                    switch (sim_check_conn(lp->connecting, FALSE))
                        {
                        case 1:                                 /* successful connection */
                            lp->conn = TRUE;                    /* record connection */
                            lp->sock = lp->connecting;          /* it now looks normal */
                            lp->connecting = 0;
                            lp->ipad = (char *)realloc (lp->ipad, 1+strlen (lp->destination));
                            strcpy (lp->ipad, lp->destination);
                            lp->cnms = sim_os_msec ();
                            sim_getnames_sock (lp->sock, &sockname, &peername);
                            snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - Outgoing Line Connection to %s (%s->%s) established", lp->destination, sockname, peername);
                            tmxr_debug_connect_line (lp, msg);
                            free (sockname);
                            free (peername);
                            if (!lp->notelnet) {
                                sim_write_sock (lp->sock, (char *)mantra, sizeof(mantra));
                                tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                                lp->telnet_sent_opts = (uint8 *)realloc (lp->telnet_sent_opts, 256);
                                memset (lp->telnet_sent_opts, 0, 256);
                                }
                            return i;
                        case -1:                                /* failed connection */
                            snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - Outgoing Line Connection to %s failed", lp->destination);
                            tmxr_debug_connect_line (lp, msg);
                            tmxr_reset_ln (lp);                 /* retry */
                            break;
                        }
                    }
                break;
            case 1:
                if (lp->master) {                                   /* Check for a pending Telnet/tcp connection */
                    while (INVALID_SOCKET != (newsock = sim_accept_conn_ex (lp->master, &address, (lp->packet ? SIM_SOCK_OPT_NODELAY : 0)))) {/* got a live one? */
                        char *sockname, *peername;

                        sim_getnames_sock (newsock, &sockname, &peername);
                        snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - Incoming Line Connection from %s (%s->%s)", address, peername, sockname);
                        tmxr_debug_connect_line (lp, msg);
                        free (sockname);
                        free (peername);
                        ++lp->sessions;                             /* count the new session */

                        if (lp->acl) {                              /* Restrict connection with ACL rules? */
                            if (sim_addr_acl_check (address, lp->acl) != 0) {
                                snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - ACL Rejecting line connection from: %s", address);
                                tmxr_debug_connect_line (lp, msg);
                                sim_close_sock (newsock);
                                free (address);
                                ++lp->acl_rejected_sessions;
                                continue;                               /* Go back for another connection */
                                }
                            else
                                ++lp->acl_accepted_sessions;
                            }
                        if (lp->destination) {                      /* Virtual Null Modem Cable? */
                            char host[sizeof(msg) - 64];

                            if (sim_parse_addr (lp->destination, host, sizeof(host), NULL, NULL, 0, NULL, address)) {
                                tmxr_msg (newsock, "Rejecting connection from unexpected source\r\n");
                                snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - Rejecting line connection from: %s, Expected: %s", address, host);
                                tmxr_debug_connect_line (lp, msg);
                                sim_close_sock (newsock);
                                free (address);
                                continue;                           /* Try for another connection */
                                }
                            if (lp->connecting) {
                                snprintf (msg, sizeof (msg) -1, "tmxr_poll_conn() - aborting outgoing line connection attempt to: %s", lp->destination);
                                tmxr_debug_connect_line (lp, msg);
                                sim_close_sock (lp->connecting);    /* abort our as yet unconnected socket */
                                lp->connecting = 0;
                                }
                            }
                        if (lp->conn == FALSE) {                    /* is the line available? */
                            if ((!lp->modem_control) || (lp->modembits & TMXR_MDM_DTR)) {
                                lp->conn = TRUE;                    /* record connection */
                                lp->sock = newsock;                 /* save socket */
                                lp->ipad = address;                 /* ip address */
                                tmxr_init_line (lp);                /* init line */
                                if (!lp->notelnet) {
                                    sim_write_sock (lp->sock, (char *)mantra, sizeof(mantra));
                                    tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                                    lp->telnet_sent_opts = (uint8 *)realloc (lp->telnet_sent_opts, 256);
                                    memset (lp->telnet_sent_opts, 0, 256);
                                    }
                                tmxr_report_connection (mp, lp);
                                lp->cnms = sim_os_msec ();          /* time of connection */
                                return i;
                                }
                            else {
                                tmxr_msg (newsock, "Line connection not available\r\n");
                                tmxr_debug_connect_line (lp, "tmxr_poll_conn() - Line connection not available");
                                sim_close_sock (newsock);
                                free (address);
                                }
                            }
                        else {
                            tmxr_msg (newsock, "Line connection busy\r\n");
                            tmxr_debug_connect_line (lp, "tmxr_poll_conn() - Line connection busy");
                            sim_close_sock (newsock);
                            free (address);
                            }
                        }
                    }
                break;
            }

    /* Check for needed outgoing connection initiation */

    if (lp->destination && (!lp->sock) && (!lp->connecting) && (!lp->serport) &&
        (!lp->modem_control || (lp->modembits & TMXR_MDM_DTR))) {
        snprintf (msg, sizeof (msg) - 1, "tmxr_poll_conn() - establishing outgoing connection to: %s", lp->destination);
        tmxr_debug_connect_line (lp, msg);
        lp->connecting = sim_connect_sock_ex (lp->datagram ? lp->port : NULL, lp->destination, "localhost", NULL, (lp->datagram ? SIM_SOCK_OPT_DATAGRAM : 0)  |
                                                                                                                  (lp->mp->packet ? SIM_SOCK_OPT_NODELAY : 0));
        }

    }

return ringing;                                         /* no new connections made */
}

/* Reset a line.

   The telnet/tcp or serial session associated with multiplexer descriptor "mp" and
   line descriptor "lp" is disconnected.  An associated tcp socket is
   closed; a serial port is closed if the closeserial parameter is true, otherwise
   for non modem control serial lines DTR is dropped and raised again after 500ms
   to signal the attached serial device.
*/

static t_stat tmxr_reset_ln_ex (TMLN *lp, t_bool closeserial)
{
char msg[512];

tmxr_debug_trace_line (lp, "tmxr_reset_ln_ex()");

if (lp->txlog)
    fflush (lp->txlog);                                 /* flush log */

tmxr_send_buffered_data (lp);                           /* send any buffered data */

sprintf (msg, "tmxr_reset_ln_ex(%s)", closeserial ? "TRUE" : "FALSE");
tmxr_debug_connect_line (lp, msg);

if (lp->serport) {
    if (closeserial) {
        sim_close_serial (lp->serport);
        lp->serport = 0;
        lp->ser_connect_pending = FALSE;
        free (lp->destination);
        lp->destination = NULL;
        free (lp->serconfig);
        lp->serconfig = NULL;
        lp->cnms = 0;
        lp->xmte = 1;
        }
    else
        if (!lp->modem_control) {                       /* serial connection? */
            sim_control_serial (lp->serport, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);/* drop DTR and RTS */
            sim_os_ms_sleep (TMXR_DTR_DROP_TIME);
            sim_control_serial (lp->serport, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);/* raise DTR and RTS */
            }
    }
else                                                    /* Telnet connection */
    if (lp->sock) {
        sim_close_sock (lp->sock);                      /* close socket */
        free (lp->telnet_sent_opts);
        lp->telnet_sent_opts = NULL;
        lp->sock = 0;
        lp->conn = FALSE;
        lp->cnms = 0;
        lp->xmte = 1;
        }
free(lp->ipad);
lp->ipad = NULL;
if ((lp->destination) && (!lp->serport)) {
    if (lp->connecting) {
        sim_close_sock (lp->connecting);
        lp->connecting = 0;
        }
    if ((!lp->modem_control) || (lp->modembits & TMXR_MDM_DTR)) {
        sprintf (msg, "tmxr_reset_ln_ex() - connecting to %s", lp->destination);
        tmxr_debug_connect_line (lp, msg);
        lp->connecting = sim_connect_sock_ex (lp->datagram ? lp->port : NULL, lp->destination, "localhost", NULL, (lp->datagram ? SIM_SOCK_OPT_DATAGRAM : 0) |
                                                                                                                  (lp->packet ? SIM_SOCK_OPT_NODELAY : 0));
        }
    }
tmxr_init_line (lp);                                /* initialize line state */
return SCPE_OK;
}

t_stat tmxr_close_ln (TMLN *lp)
{
tmxr_debug_trace_line (lp, "tmxr_close_ln()");
tmxr_debug_connect_line (lp, "tmxr_close_ln()");
return tmxr_reset_ln_ex (lp, TRUE);
}

t_stat tmxr_reset_ln (TMLN *lp)
{
tmxr_debug_trace_line (lp, "tmxr_reset_ln()");
return tmxr_reset_ln_ex (lp, FALSE);
}

/* Enable modem control pass thru

   Inputs:
        none

   Output:
        none

   Implementation note:

    1  Calling this API disables any actions on the part of this
       library to directly manipulate DTR (&RTS) on serial ports.

    2  Calling this API enables the tmxr_set_get_modem_bits and
       tmxr_set_config_line APIs.

*/
static t_stat tmxr_clear_modem_control_passthru_state (TMXR *mp, t_bool state)
{
int i;

if (mp->modem_control == state)
    return SCPE_OK;
if (mp->master)
    return SCPE_ALATT;
for (i=0; i<mp->lines; ++i) {
    TMLN *lp;

    lp = mp->ldsc + i;
    if ((lp->master)     ||
        (lp->sock)       ||
        (lp->connecting) ||
        (lp->serport))
        return SCPE_ALATT;
    }
mp->modem_control = state;
for (i=0; i<mp->lines; ++i)
    mp->ldsc[i].modem_control = state;
return SCPE_OK;
}

t_stat tmxr_set_modem_control_passthru (TMXR *mp)
{
return tmxr_clear_modem_control_passthru_state (mp, TRUE);
}

/* Disable modem control pass thru

   Inputs:
        none

   Output:
        none

   Implementation note:

    1  Calling this API enables this library's direct manipulation
       of DTR (&RTS) on serial ports.

    2  Calling this API disables the tmxr_set_get_modem_bits and
       tmxr_set_config_line APIs.

    3  This API will only change the state of the modem control processing
       of this library if there are no listening ports, serial ports or
       outgoing connections associated with the specified multiplexer

*/
t_stat tmxr_clear_modem_control_passthru (TMXR *mp)
{
return tmxr_clear_modem_control_passthru_state (mp, FALSE);
}

/* Declare that all lines on a mux have telnet disabled or enabled.

   This would best be called in a device reset routine and left.

   If the device implementor wants to make this behavior a user option
   we've got to reject the attempt to set or clear this mode if any
   ports on the MUX are attached.
*/
static t_stat tmxr_set_notelnet_state (TMXR *mp, t_bool state)
{
int i;

if (mp->master)
    return SCPE_ALATT;
for (i=0; i<mp->lines; ++i) {
    TMLN *lp;

    lp = mp->ldsc + i;
    if ((lp->master)     ||
        (lp->sock)       ||
        (lp->connecting) ||
        (lp->serport))
        return SCPE_ALATT;
    }
mp->notelnet = state;
for (i=0; i<mp->lines; ++i)
    mp->ldsc[i].notelnet = state;
return SCPE_OK;
}

/* Disable Telnet on lines in a mux

   Inputs:
        none

   Output:
        SCPE_OK or SCPE_ALATT

*/
t_stat tmxr_set_notelnet (TMXR *mp)
{
return tmxr_set_notelnet_state (mp, TRUE);
}

/* Enable Telnet on lines in a mux

   Inputs:
        none

   Output:
        SCPE_OK or SCPE_ALATT

*/
t_stat tmxr_clear_notelnet (TMXR *mp)
{
return tmxr_set_notelnet_state (mp, FALSE);
}

/* Declare that all lines on a mux will or won't spit out a message
   on incoming telnet connects.

   This would best be called in a device reset routine and left.

   If the device implementor wants to make this behavior a user option
   we've got to reject the attempt to set or clear this mode if any
   ports on the MUX are attached.
*/
static t_stat tmxr_set_nomessage_state (TMXR *mp, t_bool state)
{
int i;

if (mp->master)
    return SCPE_ALATT;
for (i=0; i<mp->lines; ++i) {
    TMLN *lp;

    lp = mp->ldsc + i;
    if ((lp->master)     ||
        (lp->sock)       ||
        (lp->connecting) ||
        (lp->serport))
        return SCPE_ALATT;
    }
mp->nomessage = state;
for (i=0; i<mp->lines; ++i)
    mp->ldsc[i].nomessage = state;
return SCPE_OK;
}

/* Disable Connect time message in incoming Telnet connections to all
   lines in a mux

   Inputs:
        none

   Output:
        SCPE_OK or SCPE_ALATT

*/
t_stat tmxr_set_nomessage (TMXR *mp)
{
return tmxr_set_nomessage_state (mp, TRUE);
}

/* Enable Connect time message in incoming Telnet connections to all
   lines in a mux

   Inputs:
        none

   Output:
        SCPE_OK or SCPE_ALATT

*/
t_stat tmxr_clear_nomessage (TMXR *mp)
{
return tmxr_set_nomessage_state (mp, FALSE);
}

/* Declare that tmxr_set_config_line is used.

   This would best be called in a device reset routine and left set.

   If the device implementor wants to make this behavior a user option
   we've got to reject the attempt to set or clear this mode if any
   ports on the MUX are attached.
*/
t_stat tmxr_set_port_speed_control (TMXR *mp)
{
int i;

if (!mp->port_speed_control && mp->uptr && !(mp->uptr->flags & UNIT_ATT))
    return sim_messagef (SCPE_ALATT, "Can't change speed mode while attached.\n:");
mp->port_speed_control = TRUE;
for (i=0; i<mp->lines; ++i)
    mp->ldsc[i].port_speed_control = mp->port_speed_control;
sim_debug (TMXR_DBG_CFG, mp->dptr, "Speed Mode: Enabled\n");
return SCPE_OK;
}

/* Declare that tmxr_set_config_line is not used.

   This should be only be called after a previous call to
   tmxr_set_port_speed_control since the default is cleared.  It can not
   be called if any ports on the device are attached.
*/
t_stat tmxr_clear_port_speed_control (TMXR *mp)
{
int i;

if (mp->port_speed_control && mp->uptr && !(mp->uptr->flags & UNIT_ATT))
    return sim_messagef (SCPE_ALATT, "Can't change speed mode while attached.\n:");
mp->port_speed_control = FALSE;
for (i=0; i<mp->lines; ++i)
    mp->ldsc[i].port_speed_control = mp->port_speed_control;
sim_debug (TMXR_DBG_CFG, mp->dptr, "Speed Mode: Disabled\n");
return SCPE_OK;
}

/* Specify a non-default listen backlog.

   The default listen backlog is 0.  This API overrides that default.
*/
t_stat tmxr_set_backlog (TMXR *mp, int32 backlog)
{
mp->backlog = backlog;
return SCPE_OK;
}

/* Declare that tmxr_set_config_line is used for line.

   This would best be called in a device reset routine and left set.

   If the device implementor wants to make this behavior a user option
   we've got to reject the attempt to set or clear this mode if any
   ports on the MUX are attached.
*/
t_stat tmxr_set_line_port_speed_control (TMXR *mp, int line)
{
if (mp->uptr && !(mp->uptr->flags & UNIT_ATT))
    return sim_messagef (SCPE_ALATT, "Can't change speed mode while attached.\n:");
if (line >= mp->lines)
    return sim_messagef (SCPE_ARG, "Invalid line for multiplexer: %d\n", line);
mp->ldsc[line].port_speed_control = TRUE;
sim_debug (TMXR_DBG_CFG, mp->dptr, "Speed Mode: Enabled for line %d\n", line);
return SCPE_OK;
}

/* Declare that tmxr_set_config_line is not used for line.

   This should be only be called after a previous call to
   tmxr_set_port_speed_control since the default is cleared.  It can not
   be called if any ports on the device are attached.
*/
t_stat tmxr_clear_line_port_speed_control (TMXR *mp, int line)
{
if (mp->uptr && !(mp->uptr->flags & UNIT_ATT))
    return sim_messagef (SCPE_ALATT, "Can't change speed mode while attached.\n:");
if (line >= mp->lines)
    return sim_messagef (SCPE_ARG, "Invalid line for multiplexer: %d\n", line);
mp->ldsc[line].port_speed_control = FALSE;
sim_debug (TMXR_DBG_CFG, mp->dptr, "Speed Mode: Disabled for line %d\n", line);
return SCPE_OK;
}

/* Manipulate the modem control bits of a specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
        bits_to_set     TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired
        bits_to_clear   TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired

   Output:
        status_bits     if non NULL, returns all of the current signal
                        state bits (incoming: DCD, RNG, CTS, DSR) along
                        with the specifically settable bits (DTR, RTS)

   Implementation note:

       If a line is connected to a serial port, then these values
       affect and reflect the state of the serial port.  If the line
       is connected to a network socket (or could be) then the network
       session state is set, cleared and/or returned.  If the line is
       connected to a DDCMP sync framer, only DTR and RTS set/clear
       are acted on, and the returned modem state bits are constructed
       based on the framer state.  For the framer, setting DTR starts
       the framer, and clearing DTR stops it.
*/
t_stat tmxr_set_get_modem_bits (TMLN *lp, int32 bits_to_set, int32 bits_to_clear, int32 *status_bits)
{
int32 before_modem_bits, incoming_state;
DEVICE *dptr;

tmxr_debug_trace_line (lp, "tmxr_set_get_modem_bits()");

if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;
if (lp->framer) {
    /* DDCMP framer attached, ignore set except for DTR and RTS.
     * Given the most recently received framer status, report DSR if
     * the framer is currently on, and CTS and Carrier Detect if a
     * carrier has been received.
     */
    bits_to_set &= TMXR_MDM_DTR | TMXR_MDM_RTS;
    bits_to_clear &= TMXR_MDM_DTR | TMXR_MDM_RTS;
    if ((bits_to_set & TMXR_MDM_DTR) && !(lp->modembits & TMXR_MDM_DTR)) {
        /* DTR being set, start framer if we're using one.  Use DMC
         * mode for now.
         */
        tmxr_start_framer (lp, TRUE);
        }
    else {
        if ((bits_to_clear & TMXR_MDM_DTR) && (lp->modembits & TMXR_MDM_DTR))
            /* DTR being cleared, stop framer if we're using one. */
            tmxr_stop_framer (lp);
        }
    incoming_state = lp->modembits | bits_to_set;
    incoming_state &= ~bits_to_clear;
    if (lp->framer->status.on)
        incoming_state |= TMXR_MDM_DSR;
    if (lp->framer->status.on & ON_SYN)
        /* Carrier detected */
        incoming_state |= TMXR_MDM_CTS | TMXR_MDM_DCD;
    lp->modembits = incoming_state;
    if (status_bits)
        *status_bits = incoming_state;
    return SCPE_OK;
}
before_modem_bits = lp->modembits;
lp->modembits |= bits_to_set;
lp->modembits &= ~bits_to_clear;
if ((lp->sock) || (lp->serport) || (lp->loopback)) {
    if (lp->modembits & TMXR_MDM_DTR) {
        incoming_state = TMXR_MDM_DSR;
        if (lp->modembits & TMXR_MDM_RTS)
            incoming_state |= TMXR_MDM_CTS;
        if (lp->halfduplex) {
            if (incoming_state & TMXR_MDM_CTS)
                incoming_state |= TMXR_MDM_DCD;
            }
        else
            incoming_state |= TMXR_MDM_DCD;
        }
    else
        incoming_state = TMXR_MDM_DCD | TMXR_MDM_DSR | ((lp->modembits & TMXR_MDM_DTR) ? 0 : TMXR_MDM_RNG);
    }
else {
    if (((before_modem_bits & TMXR_MDM_DTR) == 0) &&    /* Upward transition of DTR? */
        ((lp->modembits & TMXR_MDM_DTR) != 0)     &&
        (lp->conn == FALSE)                       &&    /* Not connected */
        (lp->modembits & TMXR_MDM_RNG)) {               /* and Ring Signal Present */
        if ((lp->destination == NULL) &&
            (lp->master == 0) &&
            (lp->mp && (lp->mp->ring_sock))) {
            int ln;

            lp->conn = TRUE;                            /* record connection */
            lp->sock = lp->mp->ring_sock;               /* save socket */
            lp->mp->ring_sock = INVALID_SOCKET;
            lp->ipad = lp->mp->ring_ipad;               /* ip address */
            lp->mp->ring_ipad = NULL;
            lp->mp->ring_start_time = 0;
            tmxr_init_line (lp);                        /* init line */
            lp->notelnet = lp->mp->notelnet;            /* apply mux default telnet setting */
            lp->nomessage = lp->mp->nomessage;          /* apply mux default telnet setting */
            if (!lp->notelnet) {
                sim_write_sock (lp->sock, (char *)mantra, sizeof(mantra));
                tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                lp->telnet_sent_opts = (uint8 *)realloc (lp->telnet_sent_opts, 256);
                memset (lp->telnet_sent_opts, 0, 256);
                }
            tmxr_report_connection (lp->mp, lp);
            lp->cnms = sim_os_msec ();                  /* time of connection */
            lp->modembits &= ~TMXR_MDM_RNG;             /* turn off ring on this line*/
            /* turn off other pending ring signals */
            for (ln = 0; ln < lp->mp->lines; ln++) {
                TMLN *tlp = lp->mp->ldsc + ln;
                if (((tlp->destination == NULL) && (tlp->master == 0)) &&
                    (tlp->modembits & TMXR_MDM_RNG) && (tlp->conn == FALSE))
                    tlp->modembits &= ~TMXR_MDM_RNG;
                }
            }
        }
    if (!lp->conn)
        lp->modembits &= ~(TMXR_MDM_DCD | TMXR_MDM_CTS);
    if ((lp->master) || (lp->mp && lp->mp->master) ||
        (lp->port && lp->destination))
        incoming_state = TMXR_MDM_DSR;
    else
        incoming_state = 0;
    }
lp->modembits |= incoming_state;
dptr = (lp->dptr ? lp->dptr : (lp->mp ? lp->mp->dptr : NULL));
if ((lp->modembits != before_modem_bits) && (sim_deb && lp->mp && dptr)) {
    sim_debug_bits (TMXR_DBG_MDM, dptr, tmxr_modem_bits, before_modem_bits, lp->modembits, FALSE);
    sim_debug (TMXR_DBG_MDM, dptr, " - Line %d - %p\n", (int)(lp-lp->mp->ldsc), lp->txb);
    }
if (status_bits)
    *status_bits = (lp->modembits & (TMXR_MDM_INCOMING | TMXR_MDM_OUTGOING));
if (lp->mp && lp->modem_control) {                  /* This API ONLY works on modem_control enabled multiplexer lines */
    if ((bits_to_set | bits_to_clear) || status_bits) {/* Anything to do? */
        if (lp->loopback) {
            if ((lp->modembits ^ before_modem_bits) & TMXR_MDM_DTR) { /* DTR changed? */
                lp->ser_connect_pending = (lp->modembits & TMXR_MDM_DTR);
                lp->conn = !(lp->modembits & TMXR_MDM_DTR);
                }
            return SCPE_OK;
            }
        if (lp->serport) {
            t_stat r = sim_control_serial (lp->serport, bits_to_set, bits_to_clear, status_bits);
            if (status_bits && (r == SCPE_OK))
                lp->modembits = (lp->modembits & ~TMXR_MDM_INCOMING) | *status_bits;
            return r;
            }
        if ((lp->sock) || (lp->connecting)) {
            if ((before_modem_bits & bits_to_clear & TMXR_MDM_DTR) != 0) { /* drop DTR? */
                if (lp->sock)
                    tmxr_report_disconnection (lp);     /* report closure */
                tmxr_reset_ln (lp);
                }
            }
        else {
            if ((lp->destination) &&                    /* Virtual Null Modem Cable */
                (bits_to_set & ~before_modem_bits &     /* and DTR being Raised */
                 TMXR_MDM_DTR)) {
                char msg[512];

                sprintf (msg, "tmxr_set_get_modem_bits() - establishing outgoing connection to: %s", lp->destination);
                tmxr_debug_connect_line (lp, msg);
                lp->connecting = sim_connect_sock_ex (lp->datagram ? lp->port : NULL, lp->destination, "localhost", NULL, (lp->datagram ? SIM_SOCK_OPT_DATAGRAM : 0) |
                                                                                                                          (lp->packet ? SIM_SOCK_OPT_NODELAY : 0));
                }
            }
        }
    return SCPE_OK;
    }
if ((lp->sock) || (lp->connecting)) {
    if ((before_modem_bits & bits_to_clear & TMXR_MDM_DTR) != 0) { /* drop DTR? */
        if (lp->sock)
            tmxr_report_disconnection (lp);     /* report closure */
        tmxr_reset_ln (lp);
        }
    }
if ((lp->serport) && (!lp->loopback))
    sim_control_serial (lp->serport, 0, 0, status_bits);
return SCPE_INCOMP;
}

/* Enable or Disable loopback mode on a line

   Inputs:
        lp -                the line to change
        enable_loopback -   enable or disable flag

   Output:
        none

   Implementation note:

    1) When enabling loopback mode, this API will disconnect any currently
       connected TCP or Serial session.
    2) When disabling loopback mode, prior network connections and/or
       serial port connections will be restored.

*/
t_stat tmxr_set_line_loopback (TMLN *lp, t_bool enable_loopback)
{
DEVICE *dptr = (lp->dptr ? lp->dptr : (lp->mp ? lp->mp->dptr : NULL));

if (lp->loopback == (enable_loopback != FALSE))
    return SCPE_OK;                 /* Nothing to do */
lp->loopback = (enable_loopback != FALSE);
if (lp->loopback) {
    lp->lpbsz = lp->rxbsz;
    lp->lpb = (char *)realloc(lp->lpb, lp->lpbsz);
    lp->lpbcnt = lp->lpbpi = lp->lpbpr = 0;
    if (!lp->conn)
        lp->ser_connect_pending = TRUE;
    }
else {
    free (lp->lpb);
    lp->lpb = NULL;
    lp->lpbsz = 0;
    }
sim_debug (TMXR_DBG_CFG, dptr, "Loopback %s for line %d\n", enable_loopback ? "Enabled" : "Disabled", (int)(lp - lp->mp->ldsc));
return SCPE_OK;
}

t_bool tmxr_get_line_loopback (TMLN *lp)
{
return (lp->loopback != FALSE);
}

/* Enable or Disable halfduplex mode on a line

   Inputs:
        lp -                the line to change
        enable_halfduplex - enable or disable flag

   Output:
        none

   When a network connected line is in halfduplex mode, DCD modem signal
   track with CTS.  When not in halfduplex mode the DCD modem signal for
   network connected lines tracks with DSR.

*/
t_stat tmxr_set_line_halfduplex (TMLN *lp, t_bool enable_halfduplex)
{
DEVICE *dptr = (lp->dptr ? lp->dptr : (lp->mp ? lp->mp->dptr : NULL));

if (lp->halfduplex == (enable_halfduplex != FALSE))
    return SCPE_OK;                 /* Nothing to do */
lp->halfduplex = (enable_halfduplex != FALSE);
sim_debug (TMXR_DBG_CFG, dptr, "Half Duplex %s for line %d\n", enable_halfduplex ? "Enabled" : "Disabled", (int)(lp - lp->mp->ldsc));
return SCPE_OK;
}

t_bool tmxr_get_line_halfduplex (TMLN *lp)
{
return (lp->halfduplex != FALSE);
}

t_stat tmxr_set_config_line (TMLN *lp, CONST char *config)
{
t_stat r;
DEVICE *dptr = (lp->dptr ? lp->dptr : (lp->mp ? lp->mp->dptr : NULL));
char *prior = lp->serconfig ? strdup (lp->serconfig) : NULL;

tmxr_debug_trace_line (lp, "tmxr_set_config_line()");
if (lp->serport) {
    r = sim_config_serial (lp->serport, config);
    if (r == SCPE_OK)
        r = tmxr_set_line_speed (lp, config);
    }
else {
    lp->serconfig = (char *)realloc (lp->serconfig, 1 + strlen (config));
    strcpy (lp->serconfig, config);
    r = tmxr_set_line_speed (lp, lp->serconfig);;
    if (r != SCPE_OK) {
        free (lp->serconfig);
        lp->serconfig = NULL;
        }
    }
sim_debug (TMXR_DBG_CFG, dptr, "Line %d changed from %s to %s\n", (int)(lp - lp->mp->ldsc), prior ? prior : "", lp->serconfig);
free (prior);
if ((r == SCPE_OK) && (lp->mp) && (lp->mp->uptr))   /* Record port state for proper restore */
    lp->mp->uptr->filename = tmxr_mux_attach_string (lp->mp->uptr->filename, lp->mp);
return r;
}


/* Get character from specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
   Output:
        (TMXR_VALID | char) or 0 if no data is currently available
                            on the specified line.

   Implementation note:

    1. If a line break was detected coincident with the current character, the
       receive break status associated with the character is cleared, and
       SCPE_BREAK is ORed into the return value.
*/

int32 tmxr_getc_ln (TMLN *lp)
{
int32 j;
t_stat val = 0;
uint32 tmp;
double sim_gtime_now = sim_gtime ();

tmxr_debug_trace_line (lp, "tmxr_getc_ln()");
if (((lp->conn || lp->txbfd) && lp->rcve) &&            /* (conn or buffered) & enb & */
    ((!lp->rxbps) ||                                    /* (!rate limited || enough time passed)? */
     (sim_gtime_now >= lp->rxnexttime))) {
    if (!sim_send_poll_data (lp->send, &val)) {         /* injected input characters available? */
        j = lp->rxbpi - lp->rxbpr;                      /* # input chrs */
        if (j) {                                        /* any? */
            tmp = lp->rxb[lp->rxbpr];                   /* get char */
            val = TMXR_VALID | (tmp & 0377);            /* valid + chr */
            if (lp->rbr[lp->rxbpr]) {                   /* break? */
                lp->rbr[lp->rxbpr] = 0;                 /* clear status */
                val = val | SCPE_BREAK;                 /* indicate to caller */
                }
            lp->rxbpr = lp->rxbpr + 1;                  /* adv pointer */
            }
        }
    }                                                   /* end if conn */
if (lp->rxbpi == lp->rxbpr)                             /* empty? zero ptrs */
    lp->rxbpi = lp->rxbpr = 0;
if (val) {                                              /* Got something? */
    if (lp->rxbps)
        lp->rxnexttime = floor (sim_gtime_now + ((lp->rxdeltausecs * sim_timer_inst_per_sec ()) / USECS_PER_SECOND));
    else
        lp->rxnexttime = floor (sim_gtime_now + ((lp->mp->uptr->wait * sim_timer_inst_per_sec ()) / USECS_PER_SECOND));
    }
tmxr_debug_return(lp, val);
return val;
}

/* Get packet from specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
        **pbuf  =       pointer to pointer of packet contents
        *psize  =       pointer to packet size
        frame_byte -    byte which separates packets in the tcp stream
                        (0 means no separation character)

   Output:
        SCPE_LOST       link state lost
        SCPE_OK         Packet returned OR no packet available

   Implementation notes:

    1. If a packet is not yet available, then the pbuf address returned is
       NULL, but success (SCPE_OK) is returned
*/

t_stat tmxr_get_packet_ln (TMLN *lp, const uint8 **pbuf, size_t *psize)
{
return tmxr_get_packet_ln_ex (lp, pbuf, psize, 0);
}

t_stat tmxr_get_packet_ln_ex (TMLN *lp, const uint8 **pbuf, size_t *psize, uint8 frame_byte)
{
int32 c;
size_t pktsize;
size_t fc_size = (frame_byte ? 1 : 0);

while (TMXR_VALID & (c = tmxr_getc_ln (lp))) {
    if (lp->rxpboffset + 3 > lp->rxpbsize) {
        lp->rxpbsize += 512;
        lp->rxpb = (uint8 *)realloc (lp->rxpb, lp->rxpbsize);
        }
    if ((lp->rxpboffset == 0) && (fc_size) && (c != frame_byte)) {
        tmxr_debug (TMXR_DBG_PRCV, lp, "Received Unexpected Framing Byte", (char *)&lp->rxpb[lp->rxpboffset], 1);
        continue;
        }
    if ((lp->datagram) && (lp->rxpboffset == fc_size)) {
        /* Datagram packet length is provided as a part of the natural datagram
           delivery, for TCP lines, we read the packet length from the data stream.
           So, here we stuff packet size into head of packet buffer so it looks like
           it was delivered by TCP and the below return logic doesn't have to worry */
        lp->rxpb[lp->rxpboffset++] = (uint8)(((1 + lp->rxbpi - lp->rxbpr) >> 8) & 0xFF);
        lp->rxpb[lp->rxpboffset++] = (uint8)((1 + lp->rxbpi - lp->rxbpr) & 0xFF);
        }
    lp->rxpb[lp->rxpboffset++] = c & 0xFF;
    if (lp->rxpboffset >= (2 + fc_size)) {
        pktsize = (lp->rxpb[0+fc_size] << 8) | lp->rxpb[1+fc_size];
        if (pktsize == (lp->rxpboffset - 2)) {
            ++lp->rxpcnt;
            *pbuf = &lp->rxpb[2+fc_size];
            *psize = pktsize;
            lp->rxpboffset = 0;
            tmxr_debug (TMXR_DBG_PRCV, lp, "Received Packet", (char *)&lp->rxpb[2+fc_size], pktsize);
            return SCPE_OK;
            }
        }
    }
*pbuf = NULL;
*psize = 0;
if (lp->conn)
    return SCPE_OK;
return SCPE_LOST;
}

/* Poll for input

   Inputs:
        *mp     =       pointer to terminal multiplexer descriptor
   Outputs:     none
*/

void tmxr_poll_rx (TMXR *mp)
{
int32 i, nbytes, j;
TMLN *lp;

tmxr_debug_trace (mp, "tmxr_poll_rx()");
for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (!(lp->sock || lp->serport || lp->loopback || lp->framer) ||
        !(lp->rcve))                                    /* skip if not connected */
        continue;

    nbytes = 0;
    if (lp->rxbpi == 0)                                 /* need input? */
        nbytes = tmxr_read (lp,                         /* yes, read */
            lp->rxbsz - TMXR_GUARD);                    /* leave spc for Telnet cruft */
    else {
        if (lp->tsta)                                   /* in Telnet seq? */
            nbytes = tmxr_read (lp,                     /* yes, read to end */
                                    lp->rxbsz - lp->rxbpi);
        }

    if (nbytes < 0) {                                   /* line error? */
        if (!lp->datagram) {                            /* ignore errors reading UDP sockets */
            if (!lp->txbfd || lp->notelnet)
                lp->txbpi = lp->txbpr = 0;              /* Drop the data we already know we can't send */
            tmxr_close_ln (lp);                         /* disconnect line */
            }
        }

    else if (nbytes > 0) {                              /* if data rcvd */

        tmxr_debug (TMXR_DBG_RCV, lp, "Received", &(lp->rxb[lp->rxbpi]), nbytes);

        j = lp->rxbpi;                                  /* start of data */
        lp->rxbpi = lp->rxbpi + nbytes;                 /* adv pointers */
        lp->rxcnt = lp->rxcnt + nbytes;

/* Examine new data, remove TELNET cruft before making input available */

        if (!lp->notelnet) {                            /* Are we looking for telnet interpretation? */
            for (; j < lp->rxbpi; ) {                   /* loop thru char */
                u_char tmp = (u_char)lp->rxb[j];        /* get char */
                switch (lp->tsta) {                     /* case tlnt state */

                case TNS_NORM:                          /* normal */
                    if (tmp == TN_IAC) {                /* IAC? */
                        lp->tsta = TNS_IAC;             /* change state */
                        tmxr_rmvrc (lp, j);             /* remove char */
                        break;
                        }
                    if ((tmp == TN_CR) && lp->dstb)     /* CR, no bin */
                        lp->tsta = TNS_CRPAD;           /* skip pad char */
                    j = j + 1;                          /* advance j */
                    break;

                case TNS_IAC:                           /* IAC prev */
                    if (tmp == TN_IAC) {                /* IAC + IAC */
                        lp->tsta = TNS_NORM;            /* treat as normal */
                        j = j + 1;                      /* advance j */
                        break;                          /* keep IAC */
                        }
                    if (tmp == TN_BRK) {                /* IAC + BRK? */
                        lp->tsta = TNS_NORM;            /* treat as normal */
                        lp->rxb[j] = 0;                 /* char is null */
                        lp->rbr[j] = 1;                 /* flag break */
                        j = j + 1;                      /* advance j */
                        break;
                        }
                    switch (tmp) {
                    case TN_WILL:                       /* IAC + WILL? */
                        lp->tsta = TNS_WILL;
                        break;
                    case TN_WONT:                       /* IAC + WONT? */
                        lp->tsta = TNS_WONT;
                        break;
                    case TN_DO:                         /* IAC + DO? */
                        lp->tsta = TNS_DO;
                        break;
                    case TN_DONT:                       /* IAC + DONT? */
                        lp->tsta = TNS_SKIP;            /* IAC + other */
                        break;
                    case TN_GA: case TN_EL:             /* IAC + other 2 byte types */
                    case TN_EC: case TN_AYT:
                    case TN_AO: case TN_IP:
                    case TN_NOP:
                        lp->tsta = TNS_NORM;            /* ignore */
                        break;
                    case TN_SB:                         /* IAC + SB sub-opt negotiation */
                    case TN_DATAMK:                     /* IAC + data mark */
                    case TN_SE:                         /* IAC + SE sub-opt end */
                        lp->tsta = TNS_NORM;            /* ignore */
                        break;
                        }
                    tmxr_rmvrc (lp, j);                 /* remove char */
                    break;

                case TNS_WILL:                          /* IAC+WILL prev */
                    if ((tmp == TN_STATUS) ||
                        (tmp == TN_TIMING) ||
                        (tmp == TN_NAOCRD) ||
                        (tmp == TN_NAOHTS) ||
                        (tmp == TN_NAOHTD) ||
                        (tmp == TN_NAOFFD) ||
                        (tmp == TN_NAOVTS) ||
                        (tmp == TN_NAOVTD) ||
                        (tmp == TN_NAOLFD) ||
                        (tmp == TN_EXTEND) ||
                        (tmp == TN_LOGOUT) ||
                        (tmp == TN_BM)     ||
                        (tmp == TN_DET)    ||
                        (tmp == TN_SENDLO) ||
                        (tmp == TN_TERMTY) ||
                        (tmp == TN_ENDREC) ||
                        (tmp == TN_TUID)   ||
                        (tmp == TN_OUTMRK) ||
                        (tmp == TN_TTYLOC) ||
                        (tmp == TN_3270)   ||
                        (tmp == TN_X3PAD)  ||
                        (tmp == TN_NAWS)   ||
                        (tmp == TN_TERMSP) ||
                        (tmp == TN_TOGFLO) ||
                        (tmp == TN_XDISPL) ||
                        (tmp == TN_ENVIRO) ||
                        (tmp == TN_AUTH)   ||
                        (tmp == TN_ENCRYP) ||
                        (tmp == TN_NEWENV) ||
                        (tmp == TN_TN3270) ||
                        (tmp == TN_CHARST) ||
                        (tmp == TN_COMPRT) ||
                        (tmp == TN_KERMIT)) {
                        /* Reject (DONT) these 'uninteresting' options only one time to avoid loops */
                        if (0 == (lp->telnet_sent_opts[tmp] & TNOS_DONT)) {
                            lp->notelnet = TRUE;                /* Temporarily disable so */
                            tmxr_putc_ln (lp, TN_IAC);          /* IAC gets injected bare */
                            lp->notelnet = FALSE;
                            tmxr_putc_ln (lp, TN_DONT);
                            tmxr_putc_ln (lp, tmp);
                            lp->telnet_sent_opts[tmp] |= TNOS_DONT;/* Record DONT sent */
                            }
                        }
                    /* fall through */
                case TNS_WONT:           /* IAC+WILL/WONT prev */
                    if (tmp == TN_BIN) {                /* BIN? */
                        if (lp->tsta == TNS_WILL) {
                            lp->dstb = 0;
                            }
                        else {
                            lp->dstb = 1;
                            }
                        }
                    tmxr_rmvrc (lp, j);                 /* remove it */
                    lp->tsta = TNS_NORM;                /* next normal */
                    break;

                /* Negotiation with the HP terminal emulator "QCTerm" is not working.
                   QCTerm says "WONT BIN" but sends bare CRs.  RFC 854 says:

                     Note that "CR LF" or "CR NUL" is required in both directions
                     (in the default ASCII mode), to preserve the symmetry of the
                     NVT model.  ...The protocol requires that a NUL be inserted
                     following a CR not followed by a LF in the data stream.

                   Until full negotiation is implemented, we work around the problem
                   by checking the character following the CR in non-BIN mode and
                   strip it only if it is LF or NUL.  This should not affect
                   conforming clients.
                */

                case TNS_CRPAD:                         /* only LF or NUL should follow CR */
                    lp->tsta = TNS_NORM;                /* next normal */
                    if ((tmp == TN_LF) ||               /* CR + LF ? */
                        (tmp == TN_NUL))                /* CR + NUL? */
                        tmxr_rmvrc (lp, j);             /* remove it */
                    break;

                case TNS_DO:                            /* pending DO request */
                    if ((tmp == TN_STATUS) ||
                        (tmp == TN_TIMING) ||
                        (tmp == TN_NAOCRD) ||
                        (tmp == TN_NAOHTS) ||
                        (tmp == TN_NAOHTD) ||
                        (tmp == TN_NAOFFD) ||
                        (tmp == TN_NAOVTS) ||
                        (tmp == TN_NAOVTD) ||
                        (tmp == TN_NAOLFD) ||
                        (tmp == TN_EXTEND) ||
                        (tmp == TN_LOGOUT) ||
                        (tmp == TN_BM)     ||
                        (tmp == TN_DET)    ||
                        (tmp == TN_SENDLO) ||
                        (tmp == TN_TERMTY) ||
                        (tmp == TN_ENDREC) ||
                        (tmp == TN_TUID)   ||
                        (tmp == TN_OUTMRK) ||
                        (tmp == TN_TTYLOC) ||
                        (tmp == TN_3270)   ||
                        (tmp == TN_X3PAD)  ||
                        (tmp == TN_NAWS)   ||
                        (tmp == TN_TERMSP) ||
                        (tmp == TN_TOGFLO) ||
                        (tmp == TN_XDISPL) ||
                        (tmp == TN_ENVIRO) ||
                        (tmp == TN_AUTH)   ||
                        (tmp == TN_ENCRYP) ||
                        (tmp == TN_NEWENV) ||
                        (tmp == TN_TN3270) ||
                        (tmp == TN_CHARST) ||
                        (tmp == TN_COMPRT) ||
                        (tmp == TN_KERMIT)) {
                        /* Reject (WONT) these 'uninteresting' options only one time to avoid loops */
                        if (0 == (lp->telnet_sent_opts[tmp] & TNOS_WONT)) {
                            lp->notelnet = TRUE;                /* Temporarily disable so */
                            tmxr_putc_ln (lp, TN_IAC);          /* IAC gets injected bare */
                            lp->notelnet = FALSE;
                            tmxr_putc_ln (lp, TN_WONT);
                            tmxr_putc_ln (lp, tmp);
                            if (lp->conn)                       /* Still connected ? */
                                lp->telnet_sent_opts[tmp] |= TNOS_WONT;/* Record WONT sent */
                            }
                        }
                    /* fall through */
                case TNS_SKIP: default:                 /* skip char */
                    tmxr_rmvrc (lp, j);                 /* remove char */
                    lp->tsta = TNS_NORM;                /* next normal */
                    break;
                    }                                   /* end case state */
                }                                       /* end for char */
            if (nbytes != (lp->rxbpi-lp->rxbpr)) {
                tmxr_debug (TMXR_DBG_RCV, lp, "Remaining", &(lp->rxb[lp->rxbpr]), lp->rxbpi-lp->rxbpr);
                }
            }
        }                                               /* end else nbytes */
    }                                                   /* end for lines */
for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (lp->rxbpi == lp->rxbpr)                         /* if buf empty, */
        lp->rxbpi = lp->rxbpr = 0;                      /* reset pointers */
    }                                                   /* end for */
}


static int32 tmxr_rqln_bare (const TMLN *lp, t_bool speed)
{
if (speed) {
    if ((lp->send != NULL) &&
        (lp->send->extoff < lp->send->insoff)) {/* buffered SEND data? */
        if (sim_gtime () < lp->send->next_time) /* too soon? */
            return 0;
        else
            return 1;
        }
    if (lp->rxbps) {                        /* consider speed and rate limiting? */
        if (sim_gtime () < lp->rxnexttime)  /* too soon? */
            return 0;
        else
            return ((lp->rxbpi - lp->rxbpr + ((lp->rxbpi < lp->rxbpr)? lp->rxbsz : 0)) > 0) ? 1 : 0;
        }
    }
return (lp->rxbpi - lp->rxbpr + ((lp->rxbpi < lp->rxbpr)? lp->rxbsz : 0));
}

/* Return count of available characters ready to be read for line */

int32 tmxr_rqln (const TMLN *lp)
{
return tmxr_rqln_bare (lp, TRUE);
}

int32 tmxr_input_pending_ln (TMLN *lp)
{
return (lp->rxbpi - lp->rxbpr);
}


/* Store character in line buffer

   Inputs:
        *lp     =       pointer to line descriptor
        chr     =       character
   Outputs:
        status  =       ok, connection lost, or stall

   Implementation note:

    1. If the line is not connected, SCPE_LOST is returned.
*/

t_stat tmxr_putc_ln (TMLN *lp, int32 chr)
{
if ((lp->conn == FALSE) &&                              /* no conn & not buffered telnet? */
    (!lp->txbfd || lp->notelnet)) {
    ++lp->txdrp;                                        /* lost */
    return SCPE_LOST;
    }
tmxr_debug_trace_line (lp, "tmxr_putc_ln()");
#define TXBUF_AVAIL(lp) ((lp->serport ? 2: lp->txbsz) - tmxr_tqln (lp))
#define TXBUF_CHAR(lp, c) {                               \
    lp->txb[lp->txbpi++] = (char)(c);                     \
    lp->txbpi %= lp->txbsz;                               \
    if (lp->txbpi == lp->txbpr)                           \
        lp->txbpr = (1+lp->txbpr)%lp->txbsz, ++lp->txdrp; \
    }
if ((lp->xmte == 0) && (TXBUF_AVAIL(lp) > 1) &&
    ((lp->txbps == 0) || (lp->txnexttime <= sim_gtime ())))
    lp->xmte = 1;                                       /* enable line transmit */
if ((lp->conn && (TXBUF_AVAIL(lp) > 1)) ||              /* connected and room for char (+ IAC)? OR */
    (!lp->conn && !lp->notelnet && lp->txbfd)) {        /* not connected and buffered ? */
    if ((TN_IAC == (u_char) chr) && (!lp->notelnet))    /* char == IAC in telnet session? */
        TXBUF_CHAR (lp, TN_IAC);                        /* stuff extra IAC char */
    TXBUF_CHAR (lp, chr);                               /* buffer char & adv pointer */
    if (((!lp->txbfd) &&
         (TXBUF_AVAIL (lp) <= TMXR_GUARD)) ||           /* near full? */
        (lp->txbps))                                    /* or we're rate limiting output */
        lp->xmte = 0;                                   /* disable line transmit until space available or character time has passed */
    if (lp->txlog) {                                    /* log if available */
        extern TMLN *sim_oline;                         /* Make sure to avoid recursion */
        TMLN *save_oline = sim_oline;                   /* when logging to a socket */

        sim_oline = NULL;                               /* save output socket */
        fputc (chr, lp->txlog);                         /* log to actual file */
        sim_oline = save_oline;                         /* restore output socket */
        }
    sim_exp_check (lp->expect, chr);                    /* process expect rules as needed */
    if (!sim_is_running &&                              /* attach message or other non simulation time message? */
        !sim_is_remote_console_master_line (lp)) {
        tmxr_send_buffered_data (lp);                   /* put data on wire */
        sim_os_ms_sleep(((lp->txbps) && (lp->txdeltausecs > 1000)) ? /* rate limiting output slower than 1000 cps */
                        (lp->txdeltausecs - 1000) / 1000 :
                        1);                             /* wait an approximate character delay */
        }
    return SCPE_OK;                                     /* char sent */
    }
++lp->txstall; lp->xmte = 0;                            /* no room, dsbl line */
return SCPE_STALL;                                      /* char not sent */
}

/* Store packet in line buffer

   Inputs:
        *lp     =       pointer to line descriptor
        *buf    =       pointer to packet data
        size    =       size of packet
        frame_char =    inter-packet framing character (0 means no frame character)

   Outputs:
        status  =       ok, connection lost, or stall

   Implementation notes:

    1. If the line is not connected, SCPE_LOST is returned.
    2. If prior packet transmission still in progress, SCPE_STALL is
       returned and no packet data is stored.  The caller must retry later.
*/
t_stat tmxr_put_packet_ln (TMLN *lp, const uint8 *buf, size_t size)
{
return tmxr_put_packet_ln_ex (lp, buf, size, 0);
}

t_stat tmxr_put_packet_ln_ex (TMLN *lp, const uint8 *buf, size_t size, uint8 frame_byte)
{
t_stat r;
size_t fc_size = (frame_byte ? 1 : 0);
size_t pktlen_size = (lp->datagram ? 0 : 2);

if ((!lp->conn) && (!lp->loopback))
    return SCPE_LOST;
if (lp->txppoffset < lp->txppsize) {
    tmxr_debug (TMXR_DBG_PXMT, lp, "Skipped Sending Packet - Transmit Busy", (char *)&lp->txpb[3], size);
    return SCPE_STALL;
    }
if (lp->txpbsize < size + pktlen_size + fc_size) {
    lp->txpbsize = size + pktlen_size + fc_size;
    lp->txpb = (uint8 *)realloc (lp->txpb, lp->txpbsize);
    }
lp->txpb[0] = frame_byte;
if (!lp->datagram) {
    lp->txpb[0+fc_size] = (size >> 8) & 0xFF;
    lp->txpb[1+fc_size] = size & 0xFF;
    }
memcpy (lp->txpb + pktlen_size + fc_size, buf, size);
lp->txppsize = size + pktlen_size + fc_size;
lp->txppoffset = 0;
tmxr_debug (TMXR_DBG_PXMT, lp, "Sending Packet", (char *)&lp->txpb[pktlen_size+fc_size], size);
++lp->txpcnt;
while ((lp->txppoffset < lp->txppsize) &&
       (SCPE_OK == (r = tmxr_putc_ln (lp, lp->txpb[lp->txppoffset]))))
   ++lp->txppoffset;
tmxr_send_buffered_data (lp);
return (lp->conn || lp->loopback) ? SCPE_OK : SCPE_LOST;
}

/* Poll for output

   Inputs:
        *mp     =       pointer to terminal multiplexer descriptor
   Outputs:
        none
*/

void tmxr_poll_tx (TMXR *mp)
{
int32 i, nbytes;
TMLN *lp;
double sim_gtime_now = sim_gtime ();

tmxr_debug_trace (mp, "tmxr_poll_tx()");
for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if ((!lp->conn) && (!lp->txbfd))                    /* skip if !conn and !buffered */
        continue;
    nbytes = tmxr_send_buffered_data (lp);              /* buffered bytes */
    if (nbytes == 0) {                                  /* buf empty? enab line */
        if ((lp->xmte == 0) &&
            ((lp->txbps == 0) ||
             (lp->txnexttime <= sim_gtime_now)))
            lp->xmte = 1;                               /* enable line transmit */
        }
    }                                                   /* end for */
}


/* Send buffered data across network

   Inputs:
        *lp     =       pointer to line descriptor
   Outputs:
        returns number of bytes still buffered
*/

int32 tmxr_send_buffered_data (TMLN *lp)
{
int32 nbytes, sbytes;
t_stat r;

tmxr_debug_trace_line (lp, "tmxr_send_buffered_data()");
nbytes = tmxr_tqln(lp);                                 /* avail bytes */
if (nbytes) {                                           /* >0? write */
    if (lp->txbpr < lp->txbpi)                          /* no wrap? */
        sbytes = tmxr_write (lp, nbytes);               /* write all data */
    else
        sbytes = tmxr_write (lp, lp->txbsz - lp->txbpr);/* write to end buf */
    if (sbytes >= 0) {                                  /* ok? */
        tmxr_debug (TMXR_DBG_XMT, lp, "Sent", &(lp->txb[lp->txbpr]), sbytes);
        lp->txbpr = (lp->txbpr + sbytes);               /* update remove ptr */
        if (lp->txbpr >= lp->txbsz)                     /* wrap? */
            lp->txbpr = 0;
        lp->txcnt = lp->txcnt + sbytes;                 /* update counts */
        nbytes = nbytes - sbytes;
        if ((nbytes == 0) && (lp->datagram))            /* if Empty buffer on datagram line */
            lp->txbpi = lp->txbpr = 0;                  /* Start next packet at beginning of buffer */
        }
    if (sbytes < 0) {                                   /* I/O Error? */
        lp->txbpi = lp->txbpr = 0;                      /* Drop the data we already know we can't send */
        lp->rxpboffset = lp->txppoffset = lp->txppsize = 0;/* Drop the data we already know we can't send */
        tmxr_close_ln (lp);                             /*  close line/port on error */
        return nbytes;                                  /*  done now. */
        }
    if (nbytes && (lp->txbpr == 0))     {               /* more data and wrap? */
        sbytes = tmxr_write (lp, nbytes);
        if (sbytes > 0) {                               /* ok */
            tmxr_debug (TMXR_DBG_XMT, lp, "Sent", lp->txb, sbytes);
            lp->txbpr = (lp->txbpr + sbytes);           /* update remove ptr */
            if (lp->txbpr >= lp->txbsz)                 /* wrap? */
                lp->txbpr = 0;
            lp->txcnt = lp->txcnt + sbytes;             /* update counts */
            nbytes = nbytes - sbytes;
            }
        }
    }                                                   /* end if nbytes */
while ((lp->txppoffset < lp->txppsize) &&               /* buffered packet data? */
       (lp->txbsz > nbytes) &&                          /* and room in xmt buffer */
       (SCPE_OK == (r = tmxr_putc_ln (lp, lp->txpb[lp->txppoffset]))))
   ++lp->txppoffset;
if ((nbytes == 0) && (tmxr_tqln(lp) > 0))
    return tmxr_send_buffered_data (lp);
return tmxr_tqln(lp) + tmxr_tpqln(lp);
}


/* Return count of buffered characters for line */

int32 tmxr_tqln (const TMLN *lp)
{
return (lp->txbpi - lp->txbpr + ((lp->txbpi < lp->txbpr)? lp->txbsz: 0));
}

/* Return count of buffered packet characters for line */

int32 tmxr_tpqln (const TMLN *lp)
{
return (lp->txppsize - lp->txppoffset);
}

/* Return transmit packet busy status for line */

t_bool tmxr_tpbusyln (const TMLN *lp)
{
return (0 != (lp->txppsize - lp->txppoffset));
}

/* Return transmitted data complete status */
/* 0 - not done, 1 - just now done, -1 - previously done. */

int32 tmxr_txdone_ln (TMLN *lp)
{
if (lp->txdone)
    return -1;                      /* previously done */
if ((lp->conn == 0) ||
    (lp->txbps == 0) ||
    (lp->txnexttime <= sim_gtime ())) {
    lp->txdone = TRUE;              /* done now */
    return 1;
    }
return 0;                           /* not done */
}

static void _mux_detach_line (TMLN *lp, t_bool close_listener, t_bool close_connecting)
{
if (lp->framer) {
    /* DDCMP framer in use, close that up.  Begin by making sure it is
     * stopped.
     */
    tmxr_stop_framer (lp);
    /* Finished with the framer's Ethernet interface */
    eth_close (lp->framer->eth);
    free (lp->framer->eth);
    free (lp->framer);
    lp->framer = NULL;
    }
if (close_listener && lp->master) {
    sim_close_sock (lp->master);
    lp->master = 0;
    free (lp->port);
    lp->port = NULL;
    }
if (lp->sock) {                             /* if existing tcp, drop it */
    tmxr_report_disconnection (lp);         /* report disconnection */
    tmxr_reset_ln (lp);
    }
if (close_connecting) {
    free (lp->destination);
    lp->destination = NULL;
    if (lp->connecting) {                   /* if existing outgoing tcp, drop it */
        lp->sock = lp->connecting;
        lp->connecting = 0;
        tmxr_reset_ln (lp);
        }
    }
if (lp->serport) {                          /* close current serial connection */
    tmxr_reset_ln (lp);
    sim_control_serial (lp->serport, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);/* drop DTR and RTS */
    sim_close_serial (lp->serport);
    lp->serport = 0;
    free (lp->serconfig);
    lp->serconfig = NULL;
    free (lp->destination);
    lp->destination = NULL;
    }
tmxr_set_line_loopback (lp, FALSE);
}

t_stat tmxr_detach_ln (TMLN *lp)
{
UNIT *uptr = NULL;

tmxr_debug_trace_line (lp, "tmxr_detach_ln()");
_mux_detach_line (lp, TRUE, TRUE);
if (lp->mp) {
    if (lp->uptr)
        uptr = lp->uptr;
    else
        uptr = lp->mp->uptr;
    }
if (uptr && uptr->filename) {
    /* Revise the unit's connect string to reflect the current attachments */
    uptr->filename = tmxr_mux_attach_string (uptr->filename, lp->mp);
    /* No connections or listeners exist, then we're equivalent to being fully detached.  We should reflect that */
    if (uptr->filename == NULL)
        tmxr_detach (lp->mp, uptr);
    }
return SCPE_OK;
}

static int32 _tmln_speed_delta (CONST char *cptr)
{
static struct {
    const char *bps;
    int32 delta;
    } *spd, speeds[] = {
    {"50",      TMLN_SPD_50_BPS},
    {"75",      TMLN_SPD_75_BPS},
    {"110",     TMLN_SPD_110_BPS},
    {"134",     TMLN_SPD_134_BPS},
    {"150",     TMLN_SPD_150_BPS},
    {"300",     TMLN_SPD_300_BPS},
    {"600",     TMLN_SPD_600_BPS},
    {"1200",    TMLN_SPD_1200_BPS},
    {"1800",    TMLN_SPD_1800_BPS},
    {"2000",    TMLN_SPD_2000_BPS},
    {"2400",    TMLN_SPD_2400_BPS},
    {"3600",    TMLN_SPD_3600_BPS},
    {"4800",    TMLN_SPD_4800_BPS},
    {"7200",    TMLN_SPD_7200_BPS},
    {"9600",    TMLN_SPD_9600_BPS},
    {"19200",   TMLN_SPD_19200_BPS},
    {"25000",   TMLN_SPD_25000_BPS},
    {"38400",   TMLN_SPD_38400_BPS},
    {"40000",   TMLN_SPD_40000_BPS},
    {"50000",   TMLN_SPD_50000_BPS},
    {"57600",   TMLN_SPD_57600_BPS},
    {"76800",   TMLN_SPD_76800_BPS},
    {"80000",   TMLN_SPD_80000_BPS},
    {"115200",  TMLN_SPD_115200_BPS},
    {"0",       0}};                    /* End of List, last valid value */
int nspeed;
char speed[24];
int nfactor = 1;

nspeed = (uint32)strtotv (cptr, &cptr, 10);
if ((*cptr != '\0') && (*cptr != '-') && (*cptr != '*'))
    return -1;
if (*cptr == '*') {
    nfactor = (uint32)strtotv (cptr+1, NULL, 10);
    if ((nfactor < 1) || (nfactor > 32))
        return -1;
    }
sprintf (speed, "%d", nspeed);

spd = speeds;
while (1) {
    if (0 == strcmp(spd->bps, speed))
        return spd->delta;
    if (spd->delta == 0)
        break;
    ++spd;
    }
return -1;
}

t_stat tmxr_set_line_modem_control (TMLN *lp, t_bool enab_disab)
{
lp->modem_control = enab_disab;
return SCPE_OK;
}

t_stat tmxr_set_line_speed (TMLN *lp, CONST char *speed)
{
UNIT *uptr;
CONST char *cptr;
t_stat r;
uint32 rxbps;

if (!speed || !*speed)
    return SCPE_2FARG;
if (_tmln_speed_delta (speed) < 0)
    return SCPE_ARG;
rxbps = (uint32)strtotv (speed, &cptr, 10);
if (*cptr == '*') {
    uint32 bpsfactor = (uint32) get_uint (cptr+1, 10, 32, &r);

    if (r != SCPE_OK)
        return r;
    lp->bpsfactor = bpsfactor;
    if (!(lp->serport) &&               /* Not a serial port */
        (speed == cptr)) {              /* AND just changing bps factor? */
        char speedbps[16];

        sprintf (speedbps, "%d", lp->rxbps);
        lp->rxdeltausecs = (uint32)(_tmln_speed_delta (speedbps) / lp->bpsfactor);
        lp->txdeltausecs = lp->rxdeltausecs;
        return SCPE_OK;                 /* Done now */
        }
    }
lp->rxbps = rxbps;                      /* use supplied speed */
if ((lp->bpsfactor == 0.0) ||           /* factor unspecified */
    (lp->serport))                      /* OR serial port */
    lp->bpsfactor = 1.0;                /* No bps factor */
lp->rxdeltausecs = (uint32)(_tmln_speed_delta (speed) / lp->bpsfactor);
lp->rxnexttime = 0.0;
uptr = lp->uptr;
if ((!uptr) && (lp->mp))
    uptr = lp->mp->uptr;
if (uptr)
    uptr->wait = lp->rxdeltausecs;
lp->txbps = lp->rxbps;
lp->txdeltausecs = lp->rxdeltausecs;
if (lp->o_uptr)
    lp->o_uptr->wait = lp->txdeltausecs;
return SCPE_OK;
}

static const char* _tmxr_getname(int number, char* name)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = eth_devices(ETH_MAX_DEVICE, list, TRUE);

  if ((number < 0) || (count <= number))
      return NULL;
  if (list[number].eth_api != ETH_API_PCAP) {
    sim_printf ("Tmxr: Synchronous line device not found.  You may need to run as root\n");
    return NULL;
    }

  strcpy(name, list[number].name);
  return name;
}

/* Open a master listening socket (and all of the other variances of connections).

   A listening socket for the port number described by "cptr" is opened for the
   multiplexer associated with descriptor "mp".  If the open is successful, all
   lines not currently otherwise connected (via serial, outgoing or direct
   listener) are initialized for Telnet connections.

   Initialization for all connection styles (MUX wide listener, per line serial,
   listener, outgoing, logging, buffering) are handled by this routine.

*/

t_stat tmxr_open_master (TMXR *mp, CONST char *cptr)
{
int32 i, line, nextline = -1;
char tbuf[CBUFSIZE], listen[CBUFSIZE], destination[CBUFSIZE],
     logfiletmpl[CBUFSIZE], buffered[CBUFSIZE], hostport[CBUFSIZE],
     port[CBUFSIZE], option[CBUFSIZE], speed[CBUFSIZE], dev_name[CBUFSIZE],
     acl[CBUFSIZE];
char framer[CBUFSIZE],fr_eth[CBUFSIZE];
int num;
int8 fr_mode;
int32 fr_speed;
FRAMER *framer_s;
ETH_DEV *eth;
SOCKET sock;
SERHANDLE serport;
CONST char *tptr = cptr;
t_bool nolog, notelnet, listennotelnet, nomessage, listennomessage, modem_control, loopback, datagram, packet, disabled, console;
int32 listenbacklog;
TMLN *lp;
t_stat r = SCPE_OK;

snprintf (dev_name, sizeof(dev_name), "%s%s", mp->uptr ? sim_dname (find_dev_from_unit (mp->uptr)) : "", mp->uptr ? " " : "");
if (*tptr == '\0')
    return SCPE_ARG;
for (i = 0; i < mp->lines; i++) {               /* initialize lines */
    lp = mp->ldsc + i;
    lp->mp = mp;                                /* set the back pointer */
    lp->modem_control = mp->modem_control;
    if (lp->bpsfactor == 0.0)
        lp->bpsfactor = 1.0;
    }
console = FALSE;
notelnet = listennotelnet = mp->notelnet;
nomessage = listennomessage = mp->nomessage;
listenbacklog = mp->backlog;
mp->ring_sock = INVALID_SOCKET;
free (mp->ring_ipad);
mp->ring_ipad = NULL;
mp->ring_start_time = 0;
tmxr_debug_trace (mp, "tmxr_open_master()");
while (*tptr) {
    line = nextline;
    memset(logfiletmpl, '\0', sizeof(logfiletmpl));
    memset(listen,      '\0', sizeof(listen));
    memset(destination, '\0', sizeof(destination));
    memset(buffered,    '\0', sizeof(buffered));
    memset(port,        '\0', sizeof(port));
    memset(acl,         '\0', sizeof(acl));
    memset(option,      '\0', sizeof(option));
    memset(speed,       '\0', sizeof(speed));
    memset(framer,      '\0', sizeof(framer));
    nolog = loopback = disabled = FALSE;
    datagram = mp->datagram;
    packet = mp->packet;
    if (mp->buffered)
        sprintf(buffered, "%d", mp->buffered);
    if (line != -1) {
        notelnet = listennotelnet = mp->notelnet;
        nomessage = listennomessage = mp->nomessage;
        }
    modem_control = mp->modem_control;
    while (*tptr) {
        tptr = get_glyph_nc (tptr, tbuf, ',');
        if (!tbuf[0])
            break;
        cptr = tbuf;
        if (!isdigit(*cptr)) {
            char gbuf[CBUFSIZE];
            CONST char *init_cptr = cptr;

            cptr = get_glyph (cptr, gbuf, '=');
            if (0 == MATCH_CMD (gbuf, "LINE")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return sim_messagef (SCPE_2FARG, "Missing Line Specifier\n");
                nextline = (int32) get_uint (cptr, 10, mp->lines-1, &r);
                if (r)
                    return sim_messagef (SCPE_ARG, "Invalid Line Specifier: %s\n", cptr);
                break;
                }
            if (0 == MATCH_CMD (gbuf, "LOG")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return sim_messagef (SCPE_2FARG, "Missing Log Specifier\n");
                strlcpy(logfiletmpl, cptr, sizeof(logfiletmpl));
                continue;
                }
             if (0 == MATCH_CMD (gbuf, "LOOPBACK")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Loopback Specifier: %s\n", cptr);
                loopback = TRUE;
                continue;
                }
           if ((0 == MATCH_CMD (gbuf, "NOBUFFERED")) ||
                (0 == MATCH_CMD (gbuf, "UNBUFFERED"))) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Unbuffered Specifier: %s\n", cptr);
                buffered[0] = '\0';
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "BUFFERED")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    strcpy (buffered, "32768");
                else {
                    i = (int32) get_uint (cptr, 10, 1024*1024*10, &r);
                    if (r || (i == 0))
                        return sim_messagef (SCPE_ARG, "Invalid Buffer Size Specifier: %s\n", cptr);
                    sprintf(buffered, "%d", i);
                    }
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "NOLOG")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected NoLog Specifier: %s\n", cptr);
                nolog = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "NOMODEM")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected NoModem Specifier: %s\n", cptr);
                modem_control = FALSE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "MODEM")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Modem Specifier: %s\n", cptr);
                modem_control = TRUE;
                continue;
                }
            if ((0 == MATCH_CMD (gbuf, "DATAGRAM")) || (0 == MATCH_CMD (gbuf, "UDP"))) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Datagram Specifier: %s\n", cptr);
                nomessage = notelnet = datagram = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "PACKET")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Packet Specifier: %s\n", cptr);
                packet = TRUE;
                continue;
                }
            if ((0 == MATCH_CMD (gbuf, "STREAM")) || (0 == MATCH_CMD (gbuf, "TCP"))) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2MARG, "Unexpected Stream Specifier: %s\n", cptr);
                datagram = FALSE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "CONNECT")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return sim_messagef (SCPE_2FARG, "Missing Connect Specifier\n");
                strlcpy (destination, cptr, sizeof(destination));
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "SYNC")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return sim_messagef (SCPE_2FARG, "Missing Framer Specifier\n");
                strlcpy (framer, cptr, sizeof(framer));
                nomessage = notelnet = datagram = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "DISABLED")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return sim_messagef (SCPE_2FARG, "Unexpected Disabled Specifier: %s\n", cptr);
                disabled = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "SPEED")) {
                if ((NULL == cptr) || ('\0' == *cptr) ||
                    (_tmln_speed_delta (cptr) < 0))
                    return sim_messagef (SCPE_ARG, "Invalid Speed Specifier: %s\n", (cptr ? cptr : ""));
                if (mp->port_speed_control &&
                    ((_tmln_speed_delta (cptr) > 0) || (*cptr != '*')) &&
                    (!(sim_switches & SIM_SW_REST)))
                    return sim_messagef (SCPE_ARG, "%s simulator programmatically sets %sport speed\n", sim_name, dev_name);
                strlcpy (speed, cptr, sizeof(speed));
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "CONSOLE")) {
                console = TRUE;
                continue;
                }
            cptr = get_glyph (gbuf, port, ';');
            if (sim_parse_addr (port, NULL, 0, NULL, NULL, 0, NULL, NULL))
                return sim_messagef (SCPE_ARG, "Invalid Port Specifier: %s\n", port);
            memset (acl, '\0', sizeof (acl));
            while (cptr && *cptr) {
                char *tptr = gbuf + (cptr - gbuf);

                cptr = get_glyph (cptr, tptr, ';');
                if (0 == MATCH_CMD (tptr, "NOTELNET"))
                    listennotelnet = TRUE;
                else
                    if (0 == MATCH_CMD (tptr, "TELNET"))
                        listennotelnet = FALSE;
                    else
                        if (0 == MATCH_CMD (tptr, "NOMESSAGE"))
                            listennomessage = TRUE;
                        else
                            if (0 == MATCH_CMD (tptr, "MESSAGE"))
                                listennomessage = FALSE;
                            else
                                if (0 == memcmp (option, "ACCEPT=", 7)) {
                                    if (sim_addr_acl_check (option + 7, NULL))
                                        return sim_messagef (SCPE_ARG, "Invalid Accept Criteria: %s\n", option + 7);
                                    if (acl[0] != '\0')
                                        strlcat (acl, ",", sizeof (acl));
                                    strlcat (acl, "+", sizeof (acl));       /* Tag as Accept rule */
                                    strlcat (acl, option + 7, sizeof (acl));
                                    }
                                else
                                    if (0 == memcmp (option, "REJECT=", 7)) {
                                        if (sim_addr_acl_check (option + 7, NULL))
                                            return sim_messagef (SCPE_ARG, "Invalid Reject Criteria: %s\n", option + 7);
                                        if (acl[0] != '\0')
                                            strlcat (acl, ",", sizeof (acl));
                                        strlcat (acl, "-", sizeof (acl));   /* Tag as Reject rule */
                                        strlcat (acl, option + 7, sizeof (acl));
                                        }
                                    else {
                                        if (*tptr)
                                            return sim_messagef (SCPE_ARG, "Invalid Specifier: %s\n", tptr);
                                        }
                }
            cptr = init_cptr;
            }
        cptr = get_glyph_nc (cptr, port, ';');
        sock = sim_master_sock (port, &r);                  /* make master socket to validate port */
        if (r)
            return sim_messagef (SCPE_ARG, "Invalid Port Specifier: %s\n", port);
        if (sock == INVALID_SOCKET)                             /* open error */
            return sim_messagef (SCPE_OPENERR, "Can't open network port: %s\n", port);
        sim_close_sock (sock);
        sim_os_ms_sleep (2);                                    /* let the close finish (required on some platforms) */
        strcpy (listen, port);
        memset (acl, '\0', sizeof (acl));
        cptr = get_glyph (cptr, option, ';');
        while (option[0]) {
            if (0 == MATCH_CMD (option, "NOTELNET"))
                listennotelnet = TRUE;
            else
                if (0 == MATCH_CMD (option, "TELNET"))
                    listennotelnet = FALSE;
                else
                    if (0 == MATCH_CMD (option, "NOMESSAGE"))
                        listennomessage = TRUE;
                    else
                        if (0 == MATCH_CMD (option, "MESSAGE"))
                            listennomessage = FALSE;
                        else
                            if (0 == memcmp (option, "ACCEPT=", 7)) {
                                if (sim_addr_acl_check (option + 7, NULL))
                                    return sim_messagef (SCPE_ARG, "Invalid Accept Criteria: %s\n", option + 7);
                                if (acl[0] != '\0')
                                    strlcat (acl, ",", sizeof (acl));
                                strlcat (acl, "+", sizeof (acl));       /* Tag as Accept rule */
                                strlcat (acl, option + 7, sizeof (acl));
                                }
                            else
                                if (0 == memcmp (option, "REJECT=", 7)) {
                                    if (sim_addr_acl_check (option + 7, NULL))
                                        return sim_messagef (SCPE_ARG, "Invalid Reject Criteria: %s\n", option + 7);
                                    if (acl[0] != '\0')
                                        strlcat (acl, ",", sizeof (acl));
                                    strlcat (acl, "-", sizeof (acl));   /* Tag as Reject rule */
                                    strlcat (acl, option + 7, sizeof (acl));
                                    }
                                else
                                    if (0 == memcmp (option, "BACKLOG=", 8)) {
                                        listenbacklog = (int32) get_uint (option + 8, 10, SOMAXCONN, &r);
                                        if (r)
                                            return sim_messagef (SCPE_ARG, "Invalid Listen Backlog: %s\n", option + 8);
                                        }
                                    else
                                        return sim_messagef (SCPE_ARG, "Invalid Specifier: %s\n", option);
            cptr = get_glyph (cptr, option, ';');
            }
        }
    if (disabled) {
        if (destination[0] || listen[0] || loopback || framer[0])
            return sim_messagef (SCPE_ARG, "Can't disable line with%s%s%s%s%s%s%s\n", destination[0] ? " CONNECT=" : "", destination, listen[0] ? " " : "", listen, loopback ? " LOOPBACK" : "", framer[0] ? " SYNC=" : "", framer);
        }
    if (console) {
        if (destination[0] || listen[0] || loopback || framer[0] || disabled)
            return sim_messagef (SCPE_ARG, "Can't have console line with%s%s%s%s%s%s%s%s\n", destination[0] ? " CONNECT=" : "", destination, listen[0] ? " " : "", listen, loopback ? " LOOPBACK" : "", framer[0] ? " SYNC=" : "", framer, disabled ? " DISABLED" : "");
        }
    if (destination[0]) {
        /* Validate destination */
        if (framer[0])
            return sim_messagef (SCPE_ARG, "Can't combine CONNECT=%s with SYNC=%s\n", destination, framer);
        serport = sim_open_serial (destination, NULL, &r);
        if (serport != INVALID_HANDLE) {
            sim_close_serial (serport);
            if (strchr (destination, ';') && (mp->modem_control || mp->port_speed_control) && !(sim_switches & SIM_SW_REST))
                return sim_messagef (SCPE_ARG, "%sSerial line parameters can only be set within simulated OS: %s\n", dev_name, 1 + strchr (destination, ';'));
            }
        else {
            char *eptr;

            memset (hostport, '\0', sizeof(hostport));
            strlcpy (hostport, destination, sizeof(hostport));
            if ((eptr = strchr (hostport, ';')))
                *(eptr++) = '\0';
            if (eptr) {
                get_glyph (eptr, eptr, 0);          /* upcase this string */
                if (0 == MATCH_CMD (eptr, "NOTELNET"))
                    notelnet = TRUE;
                else
                    if (0 == MATCH_CMD (eptr, "TELNET"))
                        if (datagram)
                            return sim_messagef (SCPE_ARG, "Telnet invalid on Datagram socket\n");
                        else
                            notelnet = FALSE;
                        else
                            return sim_messagef (SCPE_ARG, "Unexpected specifier: %s\n", eptr);
                }
            sock = sim_connect_sock_ex (NULL, hostport, "localhost", NULL, (datagram ? SIM_SOCK_OPT_DATAGRAM : 0) |
                                                                           (packet ? SIM_SOCK_OPT_NODELAY : 0));
            if (sock != INVALID_SOCKET)
                sim_close_sock (sock);
            else
                return sim_messagef (SCPE_ARG, "Invalid destination: %s\n", hostport);
            }
        }
    if (framer[0]) {
        if (listen[0] || loopback || (!notelnet) || (!datagram))
            return sim_messagef (SCPE_ARG, "Can't combined SYNC=%s with%s%s%s%s%s\n", framer,
                    listen[0] ? " " : "", listen, loopback ? " LOOPBACK" : "",
                                                  notelnet ? "" : " TELNET",
                                                  datagram ? "" : " STREAM");
        /* Validate framer spec */
        cptr = get_glyph_nc (framer, fr_eth, ':');
        cptr = get_glyph (cptr, option, ':');
        if (0 == MATCH_CMD (option, "INTEGRAL") ||
            0 == MATCH_CMD (option, "COAX"))
            fr_mode = 1;
        else {
            if (0 == MATCH_CMD (option, "LOOPBACK"))
                fr_mode = 1 | 4;  /* Integral modem, loopback */
            else
                if (0 == MATCH_CMD (option, "RS232_DCE"))
                    fr_mode = 2;
                else
                    if (0 == MATCH_CMD (option, "RS232_DTE"))
                        fr_mode = 0;
                    else
                        return sim_messagef (SCPE_ARG, "Invalid framer mode: %s\n", cptr);
            }
        /* Speed is a third value in the SYNC argument.  We don't
         * use the SPEED parameter because that only accepts the
         * standard UART rates, which for the most part are not normal
         * DDCMP line rates.
         */
        fr_speed = 0;
        if (cptr)
            fr_speed = atoi (cptr);
        /* Note that the framer ignores the speed parameter for DTE
         * mode, but we require it here in order to have a speed value
         * to control the scheduling machinery.  It would be possible
         * to make it optional and default it to some useable value
         * like 56k, or to default it that way and then later set it
         * according to the measured data rate reported by the framer.
         * For now, don't bother.
         *
         * Also, while the minimum speed is usually 500 bps, it is 56k
         * for integral modem mode -- the transformer coupling does
         * not work reliably at speeds lower than that, and this
         * matches the lowest speed supported by DEC hardware for that
         * interface type.
         */
        if (fr_speed < 500 || fr_speed > 1000000 ||
            (fr_speed < 56000 && (fr_mode & 1))) {
            return sim_messagef (SCPE_ARG, "Invalid framer speed %d\n", fr_speed);
            }
        }
    if (line == -1) {
        if (disabled)
            return sim_messagef (SCPE_ARG, "Must specify line to disable\n");
        if (framer[0])
            return sim_messagef (SCPE_ARG, "Must specify line for framer\n");
        if (modem_control != mp->modem_control)
            return SCPE_ARG;
        if (logfiletmpl[0]) {
            strlcpy(mp->logfiletmpl, logfiletmpl, sizeof(mp->logfiletmpl));
            for (i = 0; i < mp->lines; i++) {
                char gbuf[CBUFSIZE];

                lp = mp->ldsc + i;
                sim_close_logfile (&lp->txlogref);
                free (lp->txlogname);
                lp->txlogname = NULL;
                if (mp->lines > 1)
                    snprintf(gbuf, sizeof (gbuf), "%s_%d", mp->logfiletmpl, i);
                else
                    strlcpy (gbuf, mp->logfiletmpl, sizeof (gbuf));
                r = tmxr_set_log (lp->o_uptr, i, gbuf, mp);
                if (r != SCPE_OK) {
                    free (lp->txlogname);
                    lp->txlogname = NULL;
                    break;
                    }
                }
            }
        mp->buffered = atoi(buffered);
        for (i = 0; i < mp->lines; i++) { /* initialize line buffers */
            lp = mp->ldsc + i;
            if (mp->buffered) {
                lp->txbsz = mp->buffered;
                lp->txbfd = 1;
                lp->rxbsz = mp->buffered;
                }
            else {
                lp->txbsz = TMXR_MAXBUF;
                lp->txbfd = 0;
                lp->rxbsz = TMXR_MAXBUF;
                }
            lp->txbpi = lp->txbpr = 0;
            lp->txb = (char *)realloc(lp->txb, lp->txbsz);
            lp->rxb = (char *)realloc(lp->rxb, lp->rxbsz);
            lp->rbr = (char *)realloc(lp->rbr, lp->rxbsz);
            }
        if (nolog) {
            mp->logfiletmpl[0] = '\0';
            for (i = 0; i < mp->lines; i++) { /* close line logs */
                lp = mp->ldsc + i;
                free(lp->txlogname);
                lp->txlogname = NULL;
                if (lp->txlog) {
                    sim_close_logfile (&lp->txlogref);
                    lp->txlog = NULL;
                    }
                }
            }
        if (lp->framer)
            continue;                                       /* skip framer lines */
        if ((listen[0]) && (!datagram)) {
            sock = sim_master_sock_ex (listen, &r,          /* make master socket */
                                       ((sim_switches & SWMASK ('U')) ? SIM_SOCK_OPT_REUSEADDR : 0) |
                                       (mp->packet ? SIM_SOCK_OPT_NODELAY : 0)                      |
                                       SIM_SOCK_OPT_SET_BACKLOG(listenbacklog));

            if (r)
                return sim_messagef (SCPE_ARG, "Invalid network listen port: %s\n", listen);
            if (sock == INVALID_SOCKET)                     /* open error */
                return sim_messagef (SCPE_OPENERR, "Can't open network socket for listen port: %s\n", listen);
            if (mp->port) {                                 /* close prior listener */
                sim_close_sock (mp->master);
                mp->master = 0;
                free (mp->port);
                mp->port = NULL;
                }
            sim_messagef (SCPE_OK, "Listening on port %s\n", listen);
            mp->port = (char *)realloc (mp->port, 1 + strlen (listen));
            strcpy (mp->port, listen);                      /* save port */
            mp->master = sock;                              /* save master socket */
            mp->ring_sock = INVALID_SOCKET;
            free (mp->ring_ipad);
            mp->ring_ipad = NULL;
            mp->ring_start_time = 0;
            mp->notelnet = listennotelnet;                  /* save desired telnet behavior flag */
            mp->nomessage = listennomessage;                /* save desired telnet behavior flag */
            mp->backlog = listenbacklog;                    /* save desired listening port backlog */
            if (acl[0])
                mp->acl = strdup (acl);                     /* save specified access control list */
            for (i = 0; i < mp->lines; i++) {               /* initialize lines */
                lp = mp->ldsc + i;
                lp->mp = mp;                                /* set the back pointer */
                lp->packet = mp->packet;

                if (lp->serport) {                          /* serial port attached? */
                    tmxr_reset_ln (lp);                     /* close current serial connection */
                    sim_control_serial (lp->serport, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);/* drop DTR and RTS */
                    sim_close_serial (lp->serport);
                    lp->serport = 0;
                    free (lp->serconfig);
                    lp->serconfig = NULL;
                    }
                else {
                    if (speed[0])
                        tmxr_set_line_speed (lp, speed);
                    }
                tmxr_init_line (lp);                        /* initialize line state */
                lp->sock = 0;                               /* clear the socket */
                }
            }
        if (loopback) {
            if (mp->lines > 1)
                return sim_messagef (SCPE_ARG, "Ambiguous Loopback specification\n");
            sim_messagef (SCPE_OK, "Operating in loopback mode\n");
            for (i = 0; i < mp->lines; i++) {
                lp = mp->ldsc + i;
                tmxr_set_line_loopback (lp, loopback);
                if (speed[0])
                    tmxr_set_line_speed (lp, speed);
                }
            }
        if (console) {
            lp->console = TRUE;
            lp->conn = 1;
            }
        if (destination[0]) {
            if (mp->lines > 1)
                return sim_messagef (SCPE_ARG, "Ambiguous Destination specification\n");
            lp = &mp->ldsc[0];
            serport = sim_open_serial (destination, lp, &r);
            if (serport != INVALID_HANDLE) {
                _mux_detach_line (lp, TRUE, TRUE);
                if (lp->mp && lp->mp->master) {             /* if existing listener, close it */
                    sim_close_sock (lp->mp->master);
                    lp->mp->master = 0;
                    free (lp->mp->port);
                    lp->mp->port = NULL;
                    }
                lp->destination = (char *)malloc(1+strlen(destination));
                strcpy (lp->destination, destination);
                lp->mp = mp;
                lp->serport = serport;
                lp->ser_connect_pending = TRUE;
                lp->notelnet = TRUE;
                tmxr_init_line (lp);                        /* init the line state */
                if (!lp->mp->modem_control)                 /* raise DTR and RTS for non modem control lines */
                    sim_control_serial (lp->serport, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                lp->cnms = sim_os_msec ();                  /* record time of connection */
                if (sim_switches & SWMASK ('V'))            /* -V flag reports connection on port */
                    tmxr_report_connection (mp, lp);        /* report the connection to the line */
                }
            else {
                lp->datagram = datagram;
                if (datagram) {
                    if (listen[0]) {
                        lp->port = (char *)realloc (lp->port, 1 + strlen (listen));
                        strcpy (lp->port, listen);           /* save port */
                        }
                    else
                        return sim_messagef (SCPE_ARG, "Missing listen port for Datagram socket\n");
                    }
                lp->packet = packet;
                sock = sim_connect_sock_ex (datagram ? listen : NULL, hostport, "localhost", NULL, (datagram ? SIM_SOCK_OPT_DATAGRAM : 0) |
                                                                                                   (packet ? SIM_SOCK_OPT_NODELAY : 0));
                if (sock != INVALID_SOCKET) {
                    _mux_detach_line (lp, FALSE, TRUE);
                    lp->destination = (char *)malloc(1+strlen(hostport));
                    strcpy (lp->destination, hostport);
                    lp->mp = mp;
                    if (!lp->modem_control || (lp->modembits & TMXR_MDM_DTR)) {
                        lp->connecting = sock;
                        lp->ipad = (char *)malloc (1 + strlen (lp->destination));
                        strcpy (lp->ipad, lp->destination);
                        }
                    else
                        sim_close_sock (sock);
                    lp->notelnet = notelnet;
                    lp->nomessage = nomessage;
                    tmxr_init_line (lp);                    /* init the line state */
                    if (speed[0] && (!datagram))
                        tmxr_set_line_speed (lp, speed);
                    tmxr_add_to_open_list (mp);
                    return SCPE_OK;
                    }
                else
                    return sim_messagef (SCPE_ARG, "Can't open %s socket on %s%s%s\n", datagram ? "Datagram" : "Stream", datagram ? listen : "", datagram ? "<->" : "", hostport);
                }
            }
        if (speed[0] &&
            (destination[0] == '\0') &&
            (listen[0] == '\0') &&
            (!loopback)) {
            for (i = 0; i < mp->lines; i++) {
                lp = mp->ldsc + i;
                tmxr_set_line_speed (lp, speed);
                }
            }
        }
    else {                                                  /* line specific attach */
        lp = &mp->ldsc[line];
        lp->mp = mp;
        if (framer[0]) {
            /* translate name of type "sync<num>" to real eth device name */
            if ((strlen(fr_eth) == 5 || strlen(fr_eth) == 6)
                && (tolower(fr_eth[0]) == 's')
                && (tolower(fr_eth[1]) == 'y')
                && (tolower(fr_eth[2]) == 'n')
                && (tolower(fr_eth[3]) == 'c')
                && isdigit(fr_eth[4])
                && (strlen(fr_eth) == 5 || isdigit(fr_eth[5]))
                ) {
                num = atoi(&fr_eth[4]);
                if (_tmxr_getname(num, fr_eth) == NULL) /* didn't translate */
                    return SCPE_OPENERR;
            }
            /* Open the Ethernet device */
            framer_s = (FRAMER *)malloc (sizeof (FRAMER));
            memset (framer_s, 0, sizeof (*framer_s));
            eth = (ETH_DEV *)malloc (sizeof (ETH_DEV));
            memset (eth, 0, sizeof (*eth));
            eth->dptr = mp->dptr;
            framer_s->eth = eth;
            framer_s->connect_pending = TRUE;
            r = eth_open (eth, fr_eth, mp->dptr, 0);
            if (r != SCPE_OK) {
                sim_messagef (r, "Eth open error %d\n", r);
                free (eth);
                free (framer_s);
                return r;
                }
            /* Set the filters: our address, not all multi, not promiscuous */
            r = eth_filter (eth, 1, &(eth->host_nic_phy_hw_addr), 0, 0);
            if (r != SCPE_OK) {
                sim_messagef (r, "Eth set address filter error %d\n", r);
                eth_close (eth);
                free (eth);
                free (framer_s);
                return r;
                }
            lp = &mp->ldsc[line];
            lp->framer = framer_s;
            lp->datagram = lp->notelnet = TRUE;
            /* Remember these parameters for later; the framer is
             * started separately; in the DMC/DMP emulation this is
             * done at DDCMP startup to allow a DMP driver to select
             * the mode via the device API. */
            framer_s->fmode = fr_mode;
            framer_s->fspeed = fr_speed;
            /* Set the scheduling parameters from the line speed */
            lp->txdeltausecs = lp->rxdeltausecs = (uint32) (8000000 / fr_speed);
            tmxr_init_line (lp);                /* initialize line state */
            }
        if (logfiletmpl[0]) {
            sim_close_logfile (&lp->txlogref);
            lp->txlog = NULL;
            lp->txlogname = (char *)realloc (lp->txlogname, 1 + strlen (logfiletmpl));
            strcpy (lp->txlogname, logfiletmpl);
            r = sim_open_logfile (lp->txlogname, TRUE, &lp->txlog, &lp->txlogref);
            if (r == SCPE_OK)
                setvbuf(lp->txlog, NULL, _IOFBF, 65536);
            else {
                free (lp->txlogname);
                lp->txlogname = NULL;
                return sim_messagef (r, "Can't open log file: %s\n", logfiletmpl);
                }
            }
        if (buffered[0] == '\0') {
            lp->rxbsz = lp->txbsz = TMXR_MAXBUF;
            lp->txbfd = 0;
            }
        else {
            lp->rxbsz = lp->txbsz = atoi(buffered);
            lp->txbfd = 1;
            }
        lp->txbpi = lp->txbpr = 0;
        lp->txb = (char *)realloc (lp->txb, lp->txbsz);
        lp->rxb = (char *)realloc(lp->rxb, lp->rxbsz);
        lp->rbr = (char *)realloc(lp->rbr, lp->rxbsz);
        lp->packet = packet;
        if (nolog) {
            free(lp->txlogname);
            lp->txlogname = NULL;
            if (lp->txlog) {
                sim_close_logfile (&lp->txlogref);
                lp->txlog = NULL;
                }
            }
        if ((listen[0]) && (!datagram)) {
            if ((mp->lines == 1) && (mp->master))
                return sim_messagef (SCPE_ARG, "Single Line MUX can have either line specific OR MUX listener but NOT both\n");
            if (listenbacklog != mp->backlog)
                lp->backlog = listenbacklog;
            else
                lp->backlog = mp->backlog;
            sock = sim_master_sock_ex (listen, &r,          /* make master socket */
                                       ((sim_switches & SWMASK ('U')) ? SIM_SOCK_OPT_REUSEADDR : 0) |
                                       (lp->packet ? SIM_SOCK_OPT_NODELAY : 0)                      |
                                       SIM_SOCK_OPT_SET_BACKLOG(lp->backlog));
            if (r)
                return sim_messagef (SCPE_ARG, "Invalid Listen Specification: %s\n", listen);
            if (sock == INVALID_SOCKET)                     /* open error */
                return sim_messagef (SCPE_OPENERR, "Can't listen on port: %s\n", listen);
            _mux_detach_line (lp, TRUE, FALSE);
            sim_messagef (SCPE_OK, "Line %d Listening on port %s\n", line, listen);
            lp->port = (char *)realloc (lp->port, 1 + strlen (listen));
            strcpy (lp->port, listen);                       /* save port */
            lp->master = sock;                              /* save master socket */
            if (listennotelnet != mp->notelnet)
                lp->notelnet = listennotelnet;
            else
                lp->notelnet = mp->notelnet;
            if (listennomessage != mp->nomessage)
                lp->nomessage = listennomessage;
            else
                lp->nomessage = mp->nomessage;
            if (acl[0])
                lp->acl = strdup (acl);
            }
        if (destination[0]) {
            serport = sim_open_serial (destination, lp, &r);
            if (serport != INVALID_HANDLE) {
                _mux_detach_line (lp, TRUE, TRUE);
                lp->destination = (char *)malloc(1+strlen(destination));
                strcpy (lp->destination, destination);
                lp->serport = serport;
                lp->ser_connect_pending = TRUE;
                lp->notelnet = TRUE;
                tmxr_init_line (lp);                        /* init the line state */
                if (!lp->mp->modem_control)                 /* raise DTR and RTS for non modem control lines */
                    sim_control_serial (lp->serport, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                lp->cnms = sim_os_msec ();                  /* record time of connection */
                if (sim_switches & SWMASK ('V'))            /* -V flag reports connection on port */
                    tmxr_report_connection (mp, lp);        /* report the connection to the line */
                }
            else {
                if (!lp->framer) {
                    lp->datagram = datagram;
                    if (datagram) {
                        if (listen[0]) {
                            lp->port = (char *)realloc (lp->port, 1 + strlen (listen));
                            strcpy (lp->port, listen);          /* save port */
                            }
                        else
                            return sim_messagef (SCPE_ARG, "Missing listen port for Datagram socket\n");
                        }
                    sock = sim_connect_sock_ex (datagram ? listen : NULL, hostport, "localhost", NULL, (datagram ? SIM_SOCK_OPT_DATAGRAM : 0) |
                                                                                                       (packet ? SIM_SOCK_OPT_NODELAY : 0));
                    if (sock != INVALID_SOCKET) {
                        _mux_detach_line (lp, FALSE, TRUE);
                        lp->destination = (char *)malloc(1+strlen(hostport));
                        strcpy (lp->destination, hostport);
                        if (!lp->modem_control || (lp->modembits & TMXR_MDM_DTR)) {
                            lp->connecting = sock;
                            lp->ipad = (char *)malloc (1 + strlen (lp->destination));
                            strcpy (lp->ipad, lp->destination);
                            }
                        else
                            sim_close_sock (sock);
                        lp->notelnet = notelnet;
                        lp->nomessage = nomessage;
                        tmxr_init_line (lp);                    /* init the line state */
                        }
                    else
                        return sim_messagef (SCPE_ARG, "Can't open %s socket on %s%s%s\n", datagram ? "Datagram" : "Stream", datagram ? listen : "", datagram ? "<->" : "", hostport);
                    }
                }
            }
        if (loopback) {
            tmxr_set_line_loopback (lp, loopback);
            sim_messagef (SCPE_OK, "Line %d operating in loopback mode\n", line);
            }
        if (disabled)
            lp->conn = TMXR_LINE_DISABLED;                  /* Mark as not available */
        lp->modem_control = modem_control;
        if (speed[0] && (!datagram) && (!lp->serport))
            tmxr_set_line_speed (lp, speed);
        r = SCPE_OK;
        }
    }
if (r == SCPE_OK)
    tmxr_add_to_open_list (mp);
return r;
}


/* Declare which unit polls for input

   Inputs:
        *mp     =       the mux
        line    =       the line number
        *uptr_poll =    the unit which polls

   Outputs:
        none

   Implementation note:

      - This routine must be called before the MUX is attached.
      - Only devices which poll on a unit different from the unit provided
        at MUX attach time need call this function.  Calling this API is
        necessary for asynchronous multiplexer support and if speed limited
        behaviors are desired.

*/

t_stat tmxr_set_line_unit (TMXR *mp, int line, UNIT *uptr_poll)
{
if ((line < 0) || (line >= mp->lines))
    return SCPE_ARG;
if (mp->ldsc[line].uptr)
    mp->ldsc[line].uptr->dynflags &= ~UNIT_TM_POLL;
mp->ldsc[line].uptr = uptr_poll;
if (uptr_poll->tmxr)                /* associated with a TMXR? */
    mp->ldsc[line].uptr->dynflags |= UNIT_TM_POLL;
return SCPE_OK;
}

/* Declare which unit performs output transmission in its unit service
   routine for a particular line.

   Inputs:
        *mp     =       the mux
        line    =       the line number
        *uptr_poll =    the unit which polls for output

   Outputs:
        none

   Implementation notes:

      - This routine must be called before the MUX is attached.
      - Only devices which poll on a unit different from the unit provided
        at MUX attach time need call this function ABD different from the
        unit which polls for input.  Calling this API is necessary for
        asynchronous multiplexer support and if speed limited behaviors are
        desired.


*/

t_stat tmxr_set_line_output_unit (TMXR *mp, int line, UNIT *uptr_poll)
{
if ((line < 0) || (line >= mp->lines))
    return SCPE_ARG;
if (mp->ldsc[line].o_uptr)
    mp->ldsc[line].o_uptr->dynflags &= ~UNIT_TM_POLL;
mp->ldsc[line].o_uptr = uptr_poll;
if (uptr_poll->tmxr)                /* associated with a TMXR? */
    mp->ldsc[line].o_uptr->dynflags |= UNIT_TM_POLL;
return SCPE_OK;
}

/* Declare which units are the console input and out devices

   Inputs:
        *rxuptr =    the console input unit
        *txuptr =    the console output unit

   Outputs:
        none

   Implementation note:

        This routine is exported by the tmxr library so that it gets
        defined to code which uses it by including sim_tmxr.h.  Including
        sim_tmxr.h is necessary so that sim_activate is properly defined
        in the caller's code to actually call tmxr_activate.

*/

t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr)
{
extern TMXR sim_con_tmxr;

rxuptr->tmxr = &sim_con_tmxr;
txuptr->tmxr = &sim_con_tmxr;
tmxr_set_line_unit (&sim_con_tmxr, 0, rxuptr);
tmxr_set_line_output_unit (&sim_con_tmxr, 0, txuptr);
return SCPE_OK;
}


static TMXR **tmxr_open_devices = NULL;
static int tmxr_open_device_count = 0;

static void tmxr_add_to_open_list (TMXR* mux)
{
int i;
t_bool found = FALSE;

for (i=0; i<tmxr_open_device_count; ++i)
    if (tmxr_open_devices[i] == mux) {
        found = TRUE;
        break;
        }
if (!found) {
    tmxr_open_devices = (TMXR **)realloc(tmxr_open_devices, (tmxr_open_device_count+1)*sizeof(*tmxr_open_devices));
    tmxr_open_devices[tmxr_open_device_count++] = mux;
    for (i=0; i<mux->lines; i++) {
        if (mux->ldsc[i].send == NULL)
            mux->ldsc[i].send = (SEND *)calloc (1, sizeof (SEND));
        if (mux->ldsc[i].expect == NULL)
            mux->ldsc[i].expect = (EXPECT *)calloc (1, sizeof (EXPECT));
        mux->ldsc[i].send->after = mux->ldsc[i].send->delay = 0;
        }
    }
}

static void _tmxr_remove_from_open_list (TMXR* mux)
{
int i, j;

for (i=0; i<tmxr_open_device_count; ++i)
    if (tmxr_open_devices[i] == mux) {
        for (j=i+1; j<tmxr_open_device_count; ++j)
            tmxr_open_devices[j-1] = tmxr_open_devices[j];
        --tmxr_open_device_count;
        break;
        }
}

static t_stat _tmxr_locate_line_send_expect (const char *cptr, TMLN **lp, SEND **snd, EXPECT **exp)
{
char gbuf[CBUFSIZE];
DEVICE *dptr;
int i;
t_stat r;

if (snd)
    *snd = NULL;
if (exp)
    *exp = NULL;
cptr = get_glyph(cptr, gbuf, ':');
dptr = find_dev (gbuf);                 /* device match? */
if (!dptr)
    return SCPE_ARG;

for (i=0; i<tmxr_open_device_count; ++i)
    if (tmxr_open_devices[i]->dptr == dptr) {
        int line = (int)get_uint (cptr, 10, tmxr_open_devices[i]->lines, &r);
        if (r != SCPE_OK)
            return r;
        if (lp)
            *lp = &tmxr_open_devices[i]->ldsc[line];
        if (snd)
            *snd = tmxr_open_devices[i]->ldsc[line].send;
        if (exp)
            *exp = tmxr_open_devices[i]->ldsc[line].expect;
        return SCPE_OK;
        }
return SCPE_ARG;
}

t_stat tmxr_locate_line_send (const char *cptr, SEND **snd)
{
return _tmxr_locate_line_send_expect (cptr, NULL, snd, NULL);
}

t_stat tmxr_locate_line_expect (const char *cptr, EXPECT **exp)
{
return _tmxr_locate_line_send_expect (cptr, NULL, NULL, exp);
}

t_stat tmxr_locate_line (const char *cptr, TMLN **lp)
{
return _tmxr_locate_line_send_expect (cptr, lp, NULL, NULL);
}

static const char *_tmxr_send_expect_line_name (const SEND *snd, const EXPECT *exp)
{
static char line_name[CBUFSIZE];
int i, j;

strcpy (line_name, "");
for (i=0; i<tmxr_open_device_count; ++i)
    for (j=0; j<tmxr_open_devices[i]->lines; ++j)
        if ((snd == tmxr_open_devices[i]->ldsc[j].send) ||
            (exp == tmxr_open_devices[i]->ldsc[j].expect)) {
            if (tmxr_open_devices[i]->lines > 1)
                snprintf (line_name, sizeof (line_name), "%s:%d", tmxr_open_devices[i]->ldsc[j].send->dptr->name, j);
            else
                strlcpy (line_name, tmxr_open_devices[i]->ldsc[j].send->dptr->name, sizeof (line_name));
            break;
            }
return line_name;
}

const char *tmxr_send_line_name (const SEND *snd)
{
if (snd == sim_cons_get_send ())
    return "CONSOLE";
else
    return _tmxr_send_expect_line_name (snd, NULL);
}

const char *tmxr_expect_line_name (const EXPECT *exp)
{
if (exp == sim_cons_get_expect ())
    return "CONSOLE";
else
    return _tmxr_send_expect_line_name (NULL, exp);
}

static DEBTAB tmxr_debug[] = {
  {"XMT",       TMXR_DBG_XMT,       "Transmit Data"},
  {"RCV",       TMXR_DBG_RCV,       "Received Data"},
  {"RET",       TMXR_DBG_RET,       "Returned Received Data"},
  {"MODEM",     TMXR_DBG_MDM,       "Modem Signals"},
  {"CONNECT",   TMXR_DBG_CON,       "Connection Activities"},
  {"TRACE",     TMXR_DBG_TRC,       "trace routine calls"},
  {"XMTPKT",    TMXR_DBG_PXMT,      "Transmit Packet Data"},
  {"RCVPKT",    TMXR_DBG_PRCV,      "Received Packet Data"},
  {"EXPECT",    TMXR_DBG_EXP,       "Expect Activities"},
  {"SEND",      TMXR_DBG_SEND,      "Send Activities"},
  {0}
};

t_stat tmxr_add_debug (DEVICE *dptr)
{
if (DEV_TYPE(dptr) != DEV_MUX)
    return SCPE_OK;
return sim_add_debug_flags (dptr, tmxr_debug);
}

/* Attach unit to master socket */

t_stat tmxr_attach (TMXR *mp, UNIT *uptr, CONST char *cptr)
{
t_stat r;
int32 i;

if (mp->dptr == NULL)                                   /* has device been set? */
    mp->dptr = find_dev_from_unit (uptr);               /* no, so set device now */

if (mp->uptr == NULL)                                   /* has polling unit been set? */
    mp->uptr = uptr;                                    /* save unit for polling */
r = tmxr_open_master (mp, cptr);                        /* open master socket */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->filename = tmxr_mux_attach_string (uptr->filename, mp);/* save */
if (uptr->filename == NULL)                             /* avoid dangling NULL pointer */
    uptr->filename = (char *)calloc (1, 1);             /* provide an empty string */
uptr->flags = uptr->flags | UNIT_ATT;                   /* no more errors */
uptr->tmxr = (void *)mp;
if ((mp->lines > 1) ||
    ((mp->master == 0) &&
     (mp->ldsc[0].connecting == 0) &&
     (mp->ldsc[0].serport == 0)))
    uptr->dynflags = uptr->dynflags | UNIT_ATTMULT;     /* allow multiple attach commands */

uptr->dynflags |= UNIT_TM_POLL;                         /* tag as polling unit */
if (mp->dptr) {
    for (i=0; i<mp->lines; i++) {
        mp->ldsc[i].expect->dptr = mp->dptr;
        mp->ldsc[i].expect->dbit = TMXR_DBG_EXP;
        mp->ldsc[i].send->dptr = mp->dptr;
        mp->ldsc[i].send->dbit = TMXR_DBG_SEND;
        if (mp->ldsc[i].uptr == NULL)
            mp->ldsc[i].uptr = mp->uptr;
        mp->ldsc[i].uptr->tmxr = (void *)mp;
        mp->ldsc[i].uptr->dynflags |= UNIT_TM_POLL;     /* tag as polling unit */
        if (mp->ldsc[i].o_uptr == NULL)
            mp->ldsc[i].o_uptr = mp->ldsc[i].uptr;
        mp->ldsc[i].o_uptr->tmxr = (void *)mp;
        mp->ldsc[i].o_uptr->dynflags |= UNIT_TM_POLL;   /* tag as polling unit */
        }
    }
tmxr_add_to_open_list (mp);
return SCPE_OK;
}


t_stat tmxr_startup (void)
{
return SCPE_OK;
}

t_stat tmxr_shutdown (void)
{
if (tmxr_open_device_count)
    return SCPE_IERR;
return SCPE_OK;
}


t_stat tmxr_show_open_device (FILE* st, TMXR *mp)
{
int j;
TMLN *lp;
UNIT *o_uptr = mp->ldsc[0].o_uptr;
UNIT *uptr = mp->ldsc[0].uptr;
char *attach;

fprintf(st, "Multiplexer device: %s", (mp->dptr ? sim_dname (mp->dptr) : ""));
if (mp->lines > 1) {
    fprintf(st, ", ");
    tmxr_show_lines(st, NULL, 0, mp);
    }
if (mp->packet)
    fprintf(st, ", Packet");
if (mp->datagram)
    fprintf(st, ", UDP");
if (mp->notelnet)
    fprintf(st, ", Telnet=disabled");
if ((!mp->notelnet) && (mp->nomessage))
    fprintf(st, ", Message=disabled");
if (mp->modem_control)
    fprintf(st, ", ModemControl=enabled");
if (mp->buffered)
    fprintf(st, ", Buffered=%d", mp->buffered);
for (j = 1; j < mp->lines; j++)
    if (o_uptr != mp->ldsc[j].o_uptr)
        break;
if (j == mp->lines)
    fprintf(st, ", Output Unit: %s", sim_uname (o_uptr));
else
    o_uptr = NULL;
for (j = 1; j < mp->lines; j++)
    if (uptr != mp->ldsc[j].uptr)
        break;
if (j == mp->lines) {
    fprintf(st, ",\n    Input Polling Unit: %s", sim_uname (uptr));
    if (uptr != mp->uptr)
        fprintf(st, ", Connection Polling Unit: %s", sim_uname (mp->uptr));
    }
attach = tmxr_mux_attach_string (NULL, mp);
if (attach)
    fprintf(st, ",\n    attached to %s, ", attach);
free (attach);
tmxr_show_summ(st, NULL, 0, mp);
fprintf(st, ", sessions=%d", mp->sessions);
if (mp->acl_accepted_sessions)
    fprintf(st, ", accepted=%d", mp->acl_accepted_sessions);
if (mp->acl_rejected_sessions)
    fprintf(st, ", rejected=%d", mp->acl_rejected_sessions);
if (mp->lines == 1) {
    if (mp->ldsc->rxbps) {
        fprintf(st, ", Speed=%d", mp->ldsc->rxbps);
        if (mp->ldsc->bpsfactor != 1.0)
            fprintf(st, "*%.0f", mp->ldsc->bpsfactor);
        fprintf(st, " bps");
        }
    }
fprintf(st, "\n");
if (mp->ring_start_time) {
    fprintf (st, "    incoming Connection from: %s ringing for %d milliseconds\n", mp->ring_ipad, sim_os_msec () - mp->ring_start_time);
    }
for (j = 0; j < mp->lines; j++) {
    lp = mp->ldsc + j;
    if (mp->lines > 1) {
        if (lp->dptr && (mp->dptr != lp->dptr))
            fprintf (st, "Device: %s ", sim_dname(lp->dptr));
        fprintf (st, "Line: %d", j);
        if (lp->conn == TMXR_LINE_DISABLED)
            fprintf (st, " - Disabled");
        if (mp->notelnet != lp->notelnet)
            fprintf (st, " - %stelnet", lp->notelnet ? "no" : "");
        if ((!lp->notelnet) && (mp->nomessage != lp->nomessage))
            fprintf (st, " - %smessage", lp->nomessage ? "no" : "");
        if (lp->uptr && (lp->uptr != lp->mp->uptr))
            fprintf (st, " - Unit: %s", sim_uname (lp->uptr));
        if ((lp->o_uptr != o_uptr) && lp->o_uptr && (lp->o_uptr != lp->mp->uptr) && (lp->o_uptr != lp->uptr))
            fprintf (st, " - Output Unit: %s", sim_uname (lp->o_uptr));
        if (mp->modem_control != lp->modem_control)
            fprintf(st, ", ModemControl=%s", lp->modem_control ? "enabled" : "disabled");
        if (lp->loopback)
            fprintf(st, ", Loopback");
        if (lp->rxbps) {
            fprintf(st, ", Speed=%d", lp->rxbps);
            if (lp->bpsfactor != 1.0)
                fprintf(st, "*%.0f", lp->bpsfactor);
            fprintf(st, " bps");
            }
        else {
            if (lp->bpsfactor != 1.0)
                fprintf(st, ", Speed=*%.0f bps", lp->bpsfactor);
            }
        if (lp->sessions) {
            fprintf(st, ", Sessions=%d", lp->sessions);
            if (lp->acl_accepted_sessions)
                fprintf(st, ", Accepted=%d", lp->acl_accepted_sessions);
            if (lp->acl_rejected_sessions)
                fprintf(st, ", Rejected=%d", lp->acl_rejected_sessions);
            }
        fprintf (st, "\n");
        }
    if ((!lp->sock) && (!lp->connecting) && (!lp->serport) && (!lp->master)) {
        if ((lp->modem_control) || (lp->txbfd))
            tmxr_fconns (st, lp, -1);
        continue;
        }
    tmxr_fconns (st, lp, -1);
    tmxr_fstats (st, lp, -1);
    }
return SCPE_OK;
}


t_stat tmxr_show_open_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* cptr)
{
int i;
char gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, 0);
if (*cptr)
    return SCPE_2MARG;
if ((0 == tmxr_open_device_count) &&
    (gbuf[0] == '\0'))
    fprintf(st, "No Attached Multiplexer Devices\n");
else {
    for (i=0; i<tmxr_open_device_count; ++i) {
        TMXR *mp = tmxr_open_devices[i];

        if ((gbuf[0] == '\0') ||
            (0 == strcmp (gbuf, mp->dptr->name))) {
            tmxr_show_open_device (st, mp);
            if (gbuf[0] != '\0')
                break;
            }
        }
    if ((gbuf[0] != '\0') &&
        (i == tmxr_open_device_count))
        return sim_messagef (SCPE_ARG, "Multiplexer device %s not found or attached\n", gbuf);
    }
return SCPE_OK;
}

t_stat tmxr_flush_log_files (void)
{
int i, j;

for (i=0; i<tmxr_open_device_count; ++i) {
    TMXR *mp = tmxr_open_devices[i];

    for (j=0; j<mp->lines; ++j)
        if (mp->ldsc[j].txlog)
            fflush (mp->ldsc[j].txlog);
    }
return SCPE_OK;
}

/* Close a master listening socket.

   The listening socket associated with multiplexer descriptor "mp" is closed
   and deallocated.  In addition, all current Telnet sessions are disconnected.
   Serial, outgoing sessions and console connections are also disconnected.
*/

t_stat tmxr_close_master (TMXR *mp)
{
int32 i;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {  /* loop thru conn */
    lp = mp->ldsc + i;

    if (!lp->destination && lp->sock) {            /* not serial and is connected? */
        tmxr_report_disconnection (lp);                 /* report disconnection */
        tmxr_reset_ln (lp);                             /* disconnect line */
        }
    else {
        if (lp->sock) {
            tmxr_report_disconnection (lp);             /* report disconnection */
            tmxr_reset_ln (lp);
            }
        if (lp->serport) {
            sim_control_serial (lp->serport, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);/* drop DTR and RTS */
            tmxr_close_ln (lp);
            }
        lp->console = FALSE;
        free (lp->destination);
        lp->destination = NULL;
        free (lp->acl);
        lp->acl = NULL;
        if (lp->connecting) {
            lp->sock = lp->connecting;
            lp->connecting = 0;
            tmxr_reset_ln (lp);
            }
        lp->conn = FALSE;
        }
    if (lp->master) {
        sim_close_sock (lp->master);                    /* close master socket */
        lp->master = 0;
        free (lp->port);
        lp->port = NULL;
        }
    lp->txbfd = 0;
    free (lp->txb);
    lp->txb = NULL;
    free (lp->rxb);
    lp->rxb = NULL;
    free (lp->rbr);
    lp->rbr = NULL;
    lp->modembits = 0;
    }

if (mp->master)
    sim_close_sock (mp->master);                        /* close master socket */
mp->master = 0;
free (mp->port);
mp->port = NULL;
if (mp->ring_sock != INVALID_SOCKET) {
    sim_close_sock (mp->ring_sock);
    mp->ring_sock = INVALID_SOCKET;
    free (mp->ring_ipad);
    mp->ring_ipad = NULL;
    mp->ring_start_time = 0;
    }
_tmxr_remove_from_open_list (mp);
return SCPE_OK;
}


/* Detach unit from master socket and close all active network connections
   and/or serial ports.

   Note that we return SCPE_OK, regardless of whether a listening socket was
   attached.
*/

t_stat tmxr_detach (TMXR *mp, UNIT *uptr)
{
int32 i;
char portname[CBUFSIZE];

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
for (i=0; i < mp->lines; i++) {
    mp->ldsc[i].uptr->dynflags &= ~UNIT_TM_POLL;                    /* no polling */
    mp->ldsc[i].uptr->tmxr = NULL;
    mp->ldsc[i].o_uptr->dynflags &= ~UNIT_TM_POLL;                  /* no polling */
    mp->ldsc[i].o_uptr->tmxr = NULL;
    sprintf (portname, "%s:%d", mp->dptr->name, i);
    expect_cmd (0, portname);                           /* clear dangling expects */
    send_cmd (0, portname);                             /* clear dangling send data */
    }
tmxr_close_master (mp);                                 /* close master socket */
free (uptr->filename);                                  /* free setup string */
uptr->filename = NULL;
uptr->tmxr = NULL;
mp->last_poll_time = 0;
uptr->flags &= ~(UNIT_ATT);                             /* not attached */
uptr->dynflags &= ~UNIT_TM_POLL;                        /* no polling */
return SCPE_OK;
}

static int32 _tmxr_activate_delay (UNIT *uptr, int32 interval)
{
TMXR *mp = (TMXR *)uptr->tmxr;
int32 i, sooner = interval, due;
double sim_gtime_now = sim_gtime ();

for (i=0; i<mp->lines; i++) {
    TMLN *lp = &mp->ldsc[i];

    if (uptr == lp->uptr) {                     /* read polling unit? */
        if ((lp->send != NULL) &&
            (lp->send->extoff < lp->send->insoff) &&
            (sim_gtime_now < lp->send->next_time))
            due = (int32)(lp->send->next_time - sim_gtime_now);
        else {
            if ((lp->rxbps)        &&           /* while rate limiting? */
                (tmxr_rqln_bare (lp, FALSE))) { /* with pending input data */
                if (lp->rxnexttime > sim_gtime_now)
                    due = (int32)(lp->rxnexttime - sim_gtime_now);
                else
                    due = sim_processing_event ? 1 : 0; /* avoid potential infinite loop if called from service routine */
                }
            else
                due = interval;
            }
        sooner = MIN(sooner, due);
        }
    if ((lp->conn || lp->txbfd) &&      /* Connected (or buffered)? */
        (uptr == lp->o_uptr) &&         /* output completion unit? */
        (lp->txbps)) {                  /* while rate limiting */
        if ((tmxr_tqln(lp)) &&          /* pending output data */
            (lp->txnexttime < sim_gtime_now))/* that can be transmitted now? */
            tmxr_send_buffered_data (lp);/* flush it */
        if (lp->txnexttime > sim_gtime_now)
            due = (int32)(lp->txnexttime - sim_gtime_now);
        else {
            if (tmxr_tqln(lp) == 0)         /* no pending output data */
                due = interval;             /* No rush */
            else
                due = sim_processing_event ? 1 : 0; /* avoid potential infinite loop if called from service routine */
            }
        sooner = MIN(sooner, due);
        }
    }
return sooner;
}

t_stat tmxr_activate (UNIT *uptr, int32 interval)
{
int32 sooner;

if (uptr->dynflags & UNIT_TMR_UNIT)
    return sim_timer_activate (uptr, interval);         /* Handle the timer case */
if (!(uptr->dynflags & UNIT_TM_POLL))
    return _sim_activate (uptr, interval);              /* Handle the non mux case */
sooner = _tmxr_activate_delay (uptr, interval);
if (sooner != interval) {
    sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_activate() - scheduling %s after %d instructions rather than %d instructions\n", sim_uname (uptr), sooner, interval);
    return _sim_activate (uptr, sooner);                /* Handle the busy case */
    }
sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_activate() - scheduling %s after %d instructions\n", sim_uname (uptr), interval);
return _sim_activate (uptr, interval);
}

t_stat tmxr_activate_abs (UNIT *uptr, int32 interval)
{
AIO_VALIDATE(uptr);             /* Can't call asynchronously */
sim_cancel (uptr);
return tmxr_activate (uptr, interval);
}

t_stat tmxr_activate_after (UNIT *uptr, uint32 usecs_walltime)
{
int32 sooner;

if (uptr->dynflags & UNIT_TMR_UNIT)
    return _sim_activate_after (uptr, (double)usecs_walltime);  /* Handle the timer case */
if (!(uptr->dynflags & UNIT_TM_POLL))
    return _sim_activate_after (uptr, (double)usecs_walltime);  /* Handle the non mux case */
sooner = _tmxr_activate_delay (uptr, 0x7FFFFFFF);
if (sooner != 0x7FFFFFFF) {
    if (sooner < 0) {
        sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_activate_after() - scheduling %s for %u usecs produced overflow interval %d instructions, scheduling for %d instructions\n", sim_uname (uptr), usecs_walltime, sooner, 0x7FFFFFFF);
        sooner = _tmxr_activate_delay (uptr, 0x7FFFFFFF);   /* Breakpoint here on unexpected value */
        }
    sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_activate_after() - scheduling %s after %d instructions rather than %u usecs\n", sim_uname (uptr), sooner, usecs_walltime);
    return _sim_activate (uptr, sooner);                        /* Handle the busy case directly */
    }
sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_activate_after() - scheduling %s after %.0f usecs\n", sim_uname (uptr), (double)usecs_walltime);
return _sim_activate_after (uptr, (double)usecs_walltime);
}

t_stat tmxr_activate_after_abs (UNIT *uptr, uint32 usecs_walltime)
{
sim_cancel (uptr);
return tmxr_activate_after (uptr, usecs_walltime);
}

t_stat tmxr_clock_coschedule (UNIT *uptr, int32 interval)
{
int32 tmr = sim_rtcn_calibrated_tmr ();
int32 ticks = (interval + (sim_rtcn_tick_size (tmr)/2))/sim_rtcn_tick_size (tmr);/* Convert to ticks */

return tmxr_clock_coschedule_tmr (uptr, tmr, ticks);
}

t_stat tmxr_clock_coschedule_abs (UNIT *uptr, int32 interval)
{
sim_cancel (uptr);
return tmxr_clock_coschedule (uptr, interval);
}

t_stat tmxr_clock_coschedule_tmr (UNIT *uptr, int32 tmr, int32 ticks)
{
int32 interval = ticks * sim_rtcn_tick_size (tmr);
int32 sooner;

if (uptr->dynflags & UNIT_TMR_UNIT)
    return sim_clock_coschedule_tmr (uptr, tmr, ticks); /* Handle the timer case */
if (!(uptr->dynflags & UNIT_TM_POLL))
    return sim_clock_coschedule_tmr (uptr, tmr, ticks); /* Handle the non mux case */
sooner = _tmxr_activate_delay (uptr, interval);
if (sooner != interval) {
    sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_clock_coschedule_tmr(tmr=%d) - scheduling %s after %d instructions rather than %d ticks (%d instructions)\n", tmr, sim_uname (uptr), sooner, ticks, interval);
    return _sim_activate (uptr, sooner);                /* Handle the busy case directly */
    }
sim_debug (TIMER_DBG_MUX, &sim_timer_dev, "tmxr_clock_coschedule_tmr(tmr=%d) - coscheduling %s after interval %d ticks\n", tmr, sim_uname (uptr), ticks);
return sim_clock_coschedule_tmr (uptr, tmr, ticks);
}

t_stat tmxr_clock_coschedule_tmr_abs (UNIT *uptr, int32 tmr, int32 ticks)
{
sim_cancel (uptr);
return tmxr_clock_coschedule_tmr (uptr, tmr, ticks);
}

/* Generic Multiplexer attach help */

t_stat tmxr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
TMXR *mux = (TMXR *)dptr->help_ctx;
t_bool single_line = FALSE;               /* default to Multi-Line help */
t_bool port_speed_control = FALSE;
t_bool modem_control = FALSE;

if (mux) {
    single_line = (mux->lines == 1);
    port_speed_control = mux->port_speed_control;
    modem_control = mux->modem_control;
    }

if (!flag)
    fprintf (st, "%s Multiplexer Attach Help\n\n", dptr->name);
if (single_line) {          /* Single Line Multiplexer */
    fprintf (st, "The %s multiplexer may be connected to terminal emulators supporting the\n", dptr->name);
    fprintf (st, "Telnet protocol via sockets, or to hardware terminals via host serial\n");
    fprintf (st, "ports.\n\n");
    if (modem_control) {
        fprintf (st, "The %s device is a full modem control device and therefore is capable of\n", dptr->name);
        fprintf (st, "passing port configuration information and modem signals.\n");
        }
    fprintf (st, "A Telnet listening port can be configured with:\n\n");
    fprintf (st, "   sim> ATTACH %s {interface:}port{;backlog=n}\n\n", dptr->name);
    fprintf (st, "Connections to the specified port, by default, will be unrestricted.\n");
    fprintf (st, "Connections from particular IPv4 or IPv6 addresses can be restricted\n");
    fprintf (st, "or allowed based on rules you can add to the \"{interface:}port\"\n");
    fprintf (st, "specifier on the attach command.  You can add as many rules as you need\n");
    fprintf (st, "to the attach command specified with \";ACCEPT=rule-detail\" or\n");
    fprintf (st, "\";REJECT=rule-detail\" where rule-detail can be an IP address, hostname\n");
    fprintf (st, "or network block in CIDR form.  Rules are interpreted in order and if,\n");
    fprintf (st, "while processing the list, the end is reached the connection will be\n");
    fprintf (st, "rejected.\n\n");
    fprintf (st, "The -U switch can be specified on the attach command that specifies\n");
    fprintf (st, "a listening port.  This will allow a listening port to be reused if\n");
    fprintf (st, "some prior connections haven't completely shutdown.\n\n");
    fprintf (st, "Line buffering can be enabled for the %s device with:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Buffer{=bufsize}\n\n", dptr->name);
    fprintf (st, "Line buffering can be disabled for the %s device with:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s NoBuffer\n\n", dptr->name);
    fprintf (st, "The default buffer size is 32k bytes, the max buffer size is 1024k bytes\n\n");
    fprintf (st, "The outbound traffic the %s device can be logged to a file with:\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Log=LogFileName\n\n", dptr->name);
    fprintf (st, "File logging can be disabled for the %s device with:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s NoLog\n\n", dptr->name);
    fprintf (st, "The %s device may be connected to a serial port on the host system.\n", dptr->name);
    }
else {
    fprintf (st, "%s multiplexer lines may be connected to terminal emulators supporting the\n", dptr->name);
    fprintf (st, "Telnet protocol via sockets, or to hardware terminals via host serial\n");
    fprintf (st, "ports.  Concurrent Telnet and serial connections may be mixed on a given\n");
    fprintf (st, "multiplexer.\n\n");
    if (modem_control) {
        fprintf (st, "The %s device is a full modem control device and therefore is capable of\n", dptr->name);
        fprintf (st, "passing port configuration information and modem signals on all lines.\n");
        }
    fprintf (st, "Modem Control signalling behaviors can be enabled/disabled on a specific\n");
    fprintf (st, "multiplexer line with:\n\n");
    fprintf (st, "   sim> ATTACH %s Line=n,Modem\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Line=n,NoModem\n\n", dptr->name);
    fprintf (st, "A Telnet listening port can be configured with:\n\n");
    fprintf (st, "   sim> ATTACH %s {interface:}port\n\n", dptr->name);
    fprintf (st, "The -U switch can be specified on the attach command that specifies\n");
    fprintf (st, "a listening port.  This will allow a listening port to be reused if\n");
    fprintf (st, "some prior connections haven't completely shutdown.\n\n");
    if (mux)
        fprintf (st, "Line buffering for all %d lines on the %s device can be configured with:\n\n", mux->lines, dptr->name);
    else
        fprintf (st, "Line buffering for all lines on the %s device can be configured with:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Buffer{=bufsize}\n\n", dptr->name);
    if (mux)
        fprintf (st, "Line buffering for all %d lines on the %s device can be disabled with:\n\n", mux->lines, dptr->name);
    else
        fprintf (st, "Line buffering for all lines on the %s device can be disabled with:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s NoBuffer\n\n", dptr->name);
    fprintf (st, "The default buffer size is 32k bytes, the max buffer size is 1024k bytes\n\n");
    fprintf (st, "The outbound traffic for the lines of the %s device can be logged to files\n", dptr->name);
    fprintf (st, "with:\n\n");
    fprintf (st, "   sim> ATTACH %s Log=LogFileName\n\n", dptr->name);
    fprintf (st, "The log file name for each line uses the above LogFileName as a template\n");
    fprintf (st, "for the actual file name which will be LogFileName_n where n is the line\n");
    fprintf (st, "number.\n\n");
    fprintf (st, "Multiplexer lines may be connected to serial ports on the host system.\n");
    }
fprintf (st, "Serial ports may be specified as an operating system specific device names\n");
fprintf (st, "or using simh generic serial names.  simh generic names are of the form\n");
fprintf (st, "serN, where N is from 0 thru one less than the maximum number of serial\n");
fprintf (st, "ports on the local system.  The mapping of simh generic port names to OS \n");
fprintf (st, "specific names can be displayed using the following command:\n\n");
fprintf (st, "   sim> SHOW SERIAL\n");
fprintf (st, "   Serial devices:\n");
fprintf (st, "    ser0   COM1 (\\Device\\Serial0)\n");
fprintf (st, "    ser1   COM3 (Winachcf0)\n\n");
if (single_line) {          /* Single Line Multiplexer */
    fprintf (st, "   sim> ATTACH %s Connect=ser0\n\n", dptr->name);
    fprintf (st, "or equivalently:\n\n");
    fprintf (st, "   sim> ATTACH %s Connect=COM1\n\n", dptr->name);
    }
else {
    fprintf (st, "   sim> ATTACH %s Line=n,Connect=ser0\n\n", dptr->name);
    fprintf (st, "or equivalently:\n\n");
    fprintf (st, "   sim> ATTACH %s Line=n,Connect=COM1\n\n", dptr->name);
    if (mux)
        fprintf (st, "Valid line numbers are from 0 thru %d\n\n", mux->lines-1);
    }
if (single_line) {          /* Single Line Multiplexer */
    if (port_speed_control) {
        fprintf (st, "The data rate for the %s device is set programmatically within\n", dptr->name);
        fprintf (st, "the running simulator.  When connected via a telnet session, a\n");
        fprintf (st, "speed increase factor can be specified with a SPEED=*factor on\n");
        fprintf (st, "the ATTACH command.\n");
        }
    else {
        fprintf (st, "The data rate for the %s device can be controlled by\n", dptr->name);
        fprintf (st, "specifying SPEED=nnn{*factor} on the the ATTACH command.\n");
        }
    }
else {
    if (port_speed_control) {
        fprintf (st, "The data rates for the lines of the %s device are set\n", dptr->name);
        fprintf (st, "programmatically within the running simulator.  When connected\n");
        fprintf (st, "via telnet sessions, a speed increase factor can be specified with\n");
        fprintf (st, "a SPEED=*factor on the ATTACH command.\n");
        }
    else {
        fprintf (st, "The data rate for all lines or a particular line of a the %s\n", dptr->name);
        fprintf (st, "device can be controlled by specifying SPEED=nnn{*fac} on the ATTACH\n");
        fprintf (st, "command.\n");
        }
    }
if (!port_speed_control) {
    fprintf (st, "SPEED values can be any one of:\n\n");
    fprintf (st, "    0 50 75 110 134 150 300 600 1200 1800 2000 2400\n");
    fprintf (st, "    3600 4800 7200 9600 19200 38400 57600 76800 115200\n\n");
    fprintf (st, "A SPEED value of 0 causes input data to be delivered to the simulated\n");
    fprintf (st, "port as fast as it arrives.\n\n");
    }
else {
    fprintf (st, "\n");
    }
fprintf (st, "Some simulated systems run very much faster than the original system\n");
fprintf (st, "which is being simulated.  To accommodate this, multiplexer lines \n");
fprintf (st, "connected via telnet sessions may include a factor which will increase\n");
fprintf (st, "the input and output data delivery rates by the specified factor.\n");
fprintf (st, "A factor is specified with a speed ");
if (!port_speed_control) {
    fprintf (st, "value of the form \"speed*factor\"\n");
    fprintf (st, "Factor values can range from 1 thru 32.\n");
    fprintf (st, "Example:\n\n");
    fprintf (st, "   sim> ATTACH %s 1234,SPEED=2400\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s 1234,SPEED=9600*8\n", dptr->name);
    if (!single_line)
        fprintf (st, "   sim> ATTACH %s Line=2,SPEED=2400\n", dptr->name);
    fprintf (st, "\n");
    }
else {
    fprintf (st, "value of the form \"*factor\"\n");
    fprintf (st, "Factor values can range from 1 thru 32.\n");
    fprintf (st, "Example:\n\n");
    fprintf (st, "   sim> ATTACH %s 1234,SPEED=*8\n", dptr->name);
    if (!single_line)
        fprintf (st, "   sim> ATTACH %s Line=2,SPEED=*4\n", dptr->name);
    fprintf (st, "\n");
    fprintf (st, "If an attach command specifies a speed multiply factor, that value will\n");
    fprintf (st, "persist independent of any programatic action by the simulated system to\n");
    fprintf (st, "change the port speed.\n\n");
    }
if (!port_speed_control) {
    fprintf (st, "An optional serial port configuration string may be present after the port\n");
    fprintf (st, "name.  If present, it must be separated from the port name with a semicolon\n");
    fprintf (st, "and has this form:\n\n");
    fprintf (st, "   <rate>-<charsize><parity><stopbits>\n\n");
    fprintf (st, "where:\n");
    fprintf (st, "   rate     = communication rate in bits per second\n");
    fprintf (st, "   charsize = character size in bits (5-8, including optional parity)\n");
    fprintf (st, "   parity   = parity designator (N/E/O/M/S for no/even/odd/mark/space parity)\n");
    fprintf (st, "   stopbits = number of stop bits (1, 1.5, or 2)\n\n");
    fprintf (st, "As an example:\n\n");
    fprintf (st, "   9600-8n1\n\n");
    fprintf (st, "The supported rates, sizes, and parity options are host-specific.  If\n");
    fprintf (st, "a configuration string is not supplied, then the default of 9600-8N1\n");
    fprintf (st, "is used.\n");
    fprintf (st, "Note: The serial port configuration option is only available on multiplexer\n");
    fprintf (st, "      lines which are not operating with full modem control behaviors enabled.\n");
    fprintf (st, "      Lines with full modem control behaviors enabled have all of their\n");
    fprintf (st, "      configuration managed by the Operating System running within the\n");
    fprintf (st, "      simulator.\n\n");
    }
fprintf (st, "An attachment to a serial port with the '-V' switch will cause a\n");
fprintf (st, "connection message to be output to the connected serial port.\n");
fprintf (st, "This will help to confirm the correct port has been connected and\n");
fprintf (st, "that the port settings are reasonable for the connected device.\n");
fprintf (st, "This would be done as:\n\n");
if (single_line)            /* Single Line Multiplexer */
    fprintf (st, "   sim> ATTACH -V %s Connect=SerN\n", dptr->name);
else {
    fprintf (st, "   sim> ATTACH -V %s Line=n,Connect=SerN\n\n", dptr->name);
    fprintf (st, "Line specific tcp listening ports are supported.  These are configured\n");
    fprintf (st, "using commands of the form:\n\n");
    fprintf (st, "   sim> ATTACH %s Line=n,{interface:}port{;notelnet}{;nomessage}{;backlog=n}\n\n", dptr->name);
    fprintf (st, "Connections to the specified port, by default, will be unrestricted.\n");
    fprintf (st, "Connections from particular IPv4 or IPv6 addresses can be restricted\n");
    fprintf (st, "or allowed based on rules you can add to the \"{interface:}port\"\n");
    fprintf (st, "specifier on the attach command.  You can add as many rules as you need\n");
    fprintf (st, "to the attach command specified with \";ACCEPT=rule-detail\" or\n");
    fprintf (st, "\";REJECT=rule-detail\" where rule-detail can be an IP address, hostname\n");
    fprintf (st, "or network block in CIDR form.  Rules are interpreted in order and if,\n");
    fprintf (st, "while processing the list, the end is reached the connection will be\n");
    fprintf (st, "rejected.\n\n");
    }
fprintf (st, "Direct computer to computer connections (Virtual Null Modem cables) may\n");
fprintf (st, "be established using the telnet protocol or via raw tcp sockets.\n\n");
fprintf (st, "   sim> ATTACH %s Line=n,Connect=host:port{;notelnet}|{;nomessage}\n\n", dptr->name);
fprintf (st, "Computer to computer virtual connections can be one way (as illustrated\n");
fprintf (st, "above) or symmetric.  A symmetric connection is configured by combining\n");
if (single_line) {          /* Single Line Multiplexer */
    fprintf (st, "a one way connection with a tcp listening port on the same line:\n\n");
    fprintf (st, "   sim> ATTACH %s listenport,Connect=host:port\n\n", dptr->name);
    }
else {
    fprintf (st, "a one way connection with a tcp listening port on the same line:\n\n");
    fprintf (st, "   sim> ATTACH %s Line=n,listenport,Connect=host:port\n\n", dptr->name);
    }
fprintf (st, "When symmetric virtual connections are configured, incoming connections\n");
fprintf (st, "on the specified listening port are checked to assure that they actually\n");
fprintf (st, "come from the specified connection destination host system.\n\n");
if (single_line) {          /* Single Line Multiplexer */
    fprintf (st, "The %s device can be attached in LOOPBACK mode:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Loopback\n\n", dptr->name);
    }
else {
    fprintf (st, "A line on the %s device can be attached in LOOPBACK mode:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Line=n,Loopback\n\n", dptr->name);
    fprintf (st, "A line on the %s device can be specifically disabled:\n\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Line=n,Disable\n\n", dptr->name);
    }
fprintf (st, "When operating in LOOPBACK mode, all outgoing data arrives as input and\n");
fprintf (st, "outgoing modem signals (if enabled) (DTR and RTS) are reflected in the\n");
fprintf (st, "incoming modem signals (DTR->(DCD and DSR), RTS->CTS)\n\n");
if (single_line)            /* Single Line Multiplexer */
    fprintf (st, "The connection configured for the %s device is unconfigured by:\n\n", dptr->name);
else
    fprintf (st, "All connections configured for the %s device are unconfigured by:\n\n", dptr->name);
fprintf (st, "   sim> DETACH %s\n\n", dptr->name);
if (dptr->modifiers) {
    MTAB *mptr;

    for (mptr = dptr->modifiers; mptr->mask != 0; mptr++)
        if (mptr->valid == &tmxr_dscln) {
            fprintf (st, "A specific line on the %s device can be disconnected with:\n\n", dptr->name);
            fprintf (st, "   sim> SET %s %s=n\n\n", dptr->name, mptr->mstring);
            fprintf (st, "This will cause a telnet connection to be closed, but a serial port will\n");
            fprintf (st, "normally have DTR dropped for 500ms and raised again (thus hanging up a\n");
            fprintf (st, "modem on that serial port).\n\n");
            fprintf (st, "Any lines connected to serial port can be manually closed by unplugging\n");
            fprintf (st, "the serial cable from the host computer.  Dynamically adding or removing\n");
            fprintf (st, "a serial port from a mux while the simulated operating system is running\n");
            fprintf (st, "is guaranteed to have an inconsistent state between the running OS and\n");
            fprintf (st, "the simulated port state.  Restart the simulator without the serial port\n");
            fprintf (st, "attached.\n\n");
            }
    }
return SCPE_OK;
}

/* Stub examine and deposit */

t_stat tmxr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
return SCPE_NOFNC;
}

t_stat tmxr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
return SCPE_NOFNC;
}


/* Write a message directly to a socket */

void tmxr_msg (SOCKET sock, const char *msg)
{
if ((sock) && (sock != INVALID_SOCKET))
    sim_write_sock (sock, msg, (int32)strlen (msg));
}


/* Write a message to a line */

void tmxr_linemsg (TMLN *lp, const char *msg)
{
while (*msg) {
    while (SCPE_STALL == tmxr_putc_ln (lp, (int32)(*msg)))
        if (lp->txbsz == tmxr_send_buffered_data (lp))
            sim_os_ms_sleep (10);
    ++msg;
    }
}


/* Write a formatted message to a line */

void tmxr_linemsgf (TMLN *lp, const char *fmt, ...)
{
va_list arglist;

va_start (arglist, fmt);
tmxr_linemsgvf (lp, fmt, arglist);
va_end (arglist);
}

void tmxr_linemsgvf (TMLN *lp, const char *fmt, va_list arglist)
{
char stackbuf[STACKBUFSIZE];
int32 bufsize = sizeof(stackbuf);
char *buf = stackbuf;
int32 i, len;

buf[bufsize-1] = '\0';
while (1) {                                         /* format passed string, args */
#if defined(NO_vsnprintf)
    len = vsprintf (buf, fmt, arglist);
#else                                               /* !defined(NO_vsnprintf) */
    len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                              /* NO_vsnprintf */

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || (len >= bufsize-1)) {
        if (buf != stackbuf)
            free (buf);
        bufsize = bufsize * 2;
        if (bufsize < len + 2)
            bufsize = len + 2;
        buf = (char *) malloc (bufsize);
        if (buf == NULL)                            /* out of memory */
            return;
        buf[bufsize-1] = '\0';
        continue;
        }
    break;
    }

/* Output the formatted data expanding newlines where they exist */

for (i = 0; i < len; ++i) {
    if (('\n' == buf[i]) && ((i == 0) || ('\r' != buf[i-1]))) {
        while (SCPE_STALL == tmxr_putc_ln (lp, '\r'))
            if (lp->txbsz == tmxr_send_buffered_data (lp))
                sim_os_ms_sleep (10);
        }
    while (SCPE_STALL == tmxr_putc_ln (lp, buf[i]))
        if (lp->txbsz == tmxr_send_buffered_data (lp))
            sim_os_ms_sleep (10);
    }
if (buf != stackbuf)
    free (buf);
}


/* Print connections - used only in named SHOW command */

void tmxr_fconns (FILE *st, const TMLN *lp, int32 ln)
{
int32 hr, mn, sc;
uint32 ctime;

if (ln >= 0)
    fprintf (st, "line %d: ", ln);

if ((lp->sock) || (lp->connecting)) {                   /* tcp connection? */
    if (lp->destination)                                /* remote connection? */
        if (lp->datagram)
            fprintf (st, "Datagram Connection from %s to remote port %s\n", lp->port, lp->destination);/* print port name */
        else
            fprintf (st, "Connection to remote port %s\n", lp->destination);/* print port name */
    else                                                /* incoming connection */
        fprintf (st, "Connection from IP address %s\n", lp->ipad);
    }
else
    if ((lp->destination) && (!lp->serport))            /* remote connection? */
        fprintf (st, "Connecting to remote port %s\n", lp->destination);/* print port name */
if (lp->sock) {
    char *sockname, *peername;

    sim_getnames_sock (lp->sock, &sockname, &peername);
    fprintf (st, "Connection %s->%s\n", sockname, peername);
    free (sockname);
    free (peername);
    }

if ((lp->port) && (!lp->datagram)) {
    fprintf (st, "Listening on port %s\n", lp->port);   /* print port name */
    if (lp->acl)
        fprintf (st, "Connections will be accepted/rejected based on: %s\n", lp->acl);
    }

if (lp->serport)                                        /* serial connection? */
    fprintf (st, "Connected to serial port %s\n", lp->destination);  /* print port name */

if (lp->cnms) {
    ctime = (sim_os_msec () - lp->cnms) / 1000;
    hr = ctime / 3600;
    mn = (ctime / 60) % 60;
    sc = ctime % 60;
    if (ctime)
        fprintf (st, " %s %02d:%02d:%02d\n", lp->connecting ? "Connecting for" : "Connected", hr, mn, sc);
    }
else
    fprintf (st, " Line disconnected%s\n", lp->txbfd ? " (buffered)" : "");

if (lp->modem_control) {
    fprintf (st, " Modem Bits: %s%s%s%s%s%s\n", (lp->modembits & TMXR_MDM_DTR) ? "DTR " : "",
                                                (lp->modembits & TMXR_MDM_RTS) ? "RTS " : "",
                                                (lp->modembits & TMXR_MDM_DCD) ? "DCD " : "",
                                                (lp->modembits & TMXR_MDM_RNG) ? "RNG " : "",
                                                (lp->modembits & TMXR_MDM_CTS) ? "CTS " : "",
                                                (lp->modembits & TMXR_MDM_DSR) ? "DSR " : "");
    }

if ((lp->serport == 0) && (lp->sock) && (!lp->datagram))
    fprintf (st, " %s\n", (lp->notelnet) ? "Telnet disabled (RAW data)" : "Telnet protocol");
if ((!lp->notelnet) && (lp->nomessage))
    fprintf (st, " Telnet connect message disabled\n");
if ((lp->send != NULL) && (lp->send->buffer))
    sim_show_send_input (st, lp->send);
if ((lp->expect != NULL) && (lp->expect->buf))
    sim_exp_showall (st, lp->expect);
if (lp->txlog)
    fprintf (st, " Logging to %s\n", lp->txlogname);
}


/* Print statistics - used only in named SHOW command */

void tmxr_fstats (FILE *st, const TMLN *lp, int32 ln)
{
static const char *enab = "on";
static const char *dsab = "off";

if (ln >= 0)
    fprintf (st, "Line %d:", ln);
if ((!lp->sock) && (!lp->connecting) && (!lp->serport) && (!lp->framer))
    fprintf (st, " not connected\n");
else {
    if (ln >= 0)
        fprintf (st, "\n");
    fprintf (st, "  input (%s)", (lp->rcve? enab: dsab));
    if (lp->rxcnt)
        fprintf (st, " queued/total = %d/%d", tmxr_rqln (lp), lp->rxcnt);
    if (lp->rxpcnt)
        fprintf (st, " packets = %d", lp->rxpcnt);
    fprintf (st, "\n  output (%s)", (lp->xmte? enab: dsab));
    if (lp->txcnt || lp->txbpi)
        fprintf (st, " queued/total = %d/%d", tmxr_tqln (lp), lp->txcnt);
    if (lp->txpcnt || tmxr_tpqln (lp))
        fprintf (st, " packet data queued/packets sent = %d/%d",
            tmxr_tpqln (lp), lp->txpcnt);
    fprintf (st, "\n");
    if ((lp->rxbps) || (lp->txbps)) {
        if ((lp->rxbps == lp->txbps))
            fprintf (st, "  speed = %u", lp->rxbps);
        else
            fprintf (st, "  speed = %u/%u", lp->rxbps, lp->txbps);
        if (lp->bpsfactor > 1.0)
            fprintf (st, "*%.0f", lp->bpsfactor);
        fprintf (st, " bps\n");
        }
    }
if (lp->txbfd)
    fprintf (st, "  output buffer size = %d\n", lp->txbsz);
if (lp->txcnt || lp->txbpi)
    fprintf (st, "  bytes in buffer = %d\n",
               ((lp->txcnt > 0) && (lp->txcnt > lp->txbsz)) ? lp->txbsz : lp->txbpi);
if (lp->txdrp)
    fprintf (st, "  dropped = %d\n", lp->txdrp);
if (lp->txstall)
    fprintf (st, "  stalled = %d\n", lp->txstall);
}


/* Disconnect a line.

   Disconnect a line of the multiplexer associated with descriptor "desc" from a
   tcp session or a serial port.  Two calling sequences are supported:

    1. If "val" is zero, then "uptr" is implicitly associated with the line
       number corresponding to the position of the unit in the zero-based array
       of units belonging to the associated device, and "cptr" is ignored.  For
       example, if "uptr" points to unit 3 in a given device, then line 3 will
       be disconnected.

    2. If "val" is non-zero, then "cptr" points to a string that is parsed for
       an explicit line number, and "uptr" is ignored.  For example, if "cptr"
       points to the string "3", then line 3 will be disconnected.

   If the line was connected to a tcp session, the socket associated with the
   line will be closed.  If the line was connected to a serial port, the port
   will NOT be closed, but DTR will be dropped.  After a 500ms delay DTR will
   be raised again.

   Implementation notes:

    1. This function is usually called as an MTAB processing routine.
*/

t_stat tmxr_dscln (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
t_stat status;

if (val)                                                        /* explicit line? */
    uptr = NULL;                                                /* indicate to get routine */

tmxr_debug_trace (mp, "tmxr_dscln()");

lp = tmxr_get_ldsc (uptr, cptr, mp, &status);                   /* get referenced line */

if (lp == NULL)                                                 /* bad line number? */
    return status;                                              /* report it */

if ((lp->sock) || (lp->serport)) {                              /* connection active? */
    if ((!lp->notelnet) && (!lp->nomessage))
        tmxr_linemsg (lp, "\r\nOperator disconnected line\r\n\n");/* report closure */
    if (lp->serport && (sim_switches & SWMASK ('C'))) {
        sim_messagef (SCPE_OK, "If you really feel the need to disconnect this serial port, unplug the cable\n");
        sim_messagef (SCPE_OK, "from the serial port on your system.  Alternatively, you should restart the\n");
        sim_messagef (SCPE_OK, "simulator without attaching the serial port in your configuration.\n");
        }
    return tmxr_reset_ln_ex (lp, FALSE);                        /* drop the line */
    }

return SCPE_OK;
}


/* Enable logging for line */

t_stat tmxr_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
t_stat r;
int32 nbytes, boffset;

if (cptr == NULL)                                       /* no file name? */
    return SCPE_2FARG;
lp = tmxr_find_ldsc (uptr, val, mp);                    /* find line desc */
if (lp == NULL)
    return SCPE_IERR;
if (lp->txlog)                                          /* close existing log */
    tmxr_set_nolog (NULL, val, NULL, desc);
lp->txlogname = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc namebuf */
if (lp->txlogname == NULL)                              /* can't? */
    return SCPE_MEM;
strlcpy (lp->txlogname, cptr, CBUFSIZE);                /* save file name */
r = sim_open_logfile (cptr, TRUE, &lp->txlog, &lp->txlogref);/* open log */
if ((r != SCPE_OK) || (lp->txlog == NULL)) {            /* error? */
    free (lp->txlogname);                               /* free buffer */
    return SCPE_OPENERR;
    }
if (mp->uptr)                                           /* attached?, then update attach string */
    lp->mp->uptr->filename = tmxr_mux_attach_string (lp->mp->uptr->filename, lp->mp);
if (lp->conn) { /* If we're connected, flush the buffer out now */
    while (tmxr_send_buffered_data (lp) > 0)
        sim_os_ms_sleep (10);
    }
else {
    if (lp->txcnt > lp->txbsz) {
        boffset = (lp->txbpi + 1) % lp->txbsz;
        nbytes = lp->txbsz;                             /* avail buffered bytes */
        }
    else {
        boffset = 0;
        nbytes = lp->txbpi;
        }
    while (nbytes) {                                    /* >0? write */
        int32 sbytes;

        if (boffset < lp->txbpi)                        /* no wrap? */
            sbytes = fwrite (&(lp->txb[boffset]), 1, nbytes, lp->txlog);/* write all data */
        else
            sbytes = fwrite (&(lp->txb[boffset]), 1, lp->txbsz - boffset, lp->txlog);/* write to end buf */
        if (sbytes >= 0) {                              /* ok? */
            boffset += sbytes;                          /* update remove ptr */
            if (boffset >= lp->txbsz)                   /* wrap? */
                boffset = 0;
            nbytes -= sbytes;                           /* update remaining count */
            }
        else
            break;
        }
    }
return SCPE_OK;
}


/* Disable logging for line */

t_stat tmxr_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;

if (cptr)                                               /* no arguments */
    return SCPE_2MARG;
lp = tmxr_find_ldsc (uptr, val, mp);                    /* find line desc */
if (lp == NULL)
    return SCPE_IERR;
if (lp->txlog) {                                        /* logging? */
    sim_close_logfile (&lp->txlogref);                  /* close log */
    free (lp->txlogname);                               /* free namebuf */
    lp->txlog = NULL;
    lp->txlogname = NULL;
    }
if (mp->uptr)
    lp->mp->uptr->filename = tmxr_mux_attach_string (lp->mp->uptr->filename, lp->mp);
return SCPE_OK;
}


/* Show logging status for line */

t_stat tmxr_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;
TMLN *lp;

lp = tmxr_find_ldsc (uptr, val, mp);                    /* find line desc */
if (lp == NULL)
    return SCPE_IERR;
if (lp->txlog)
    fprintf (st, "logging to %s", lp->txlogname);
else fprintf (st, "no logging");
return SCPE_OK;
}


/* Set the line connection order.

   This validation routine is called to set the connection order for the
   multiplexer whose TMXR pointer is passed in the "desc" parameter.  It parses
   the line order list, specified by the "cptr" parameter, of commands such as:

      SET <dev> LINEORDER=4-7
      SET <dev> LINEORDER=1;5;2-4;7;ALL
      SET <dev> LINEORDER=ALL

   Assuming an 8-channel multiplexer, the first form sets the connection order
   to line numbers 4, 5, 6, and 7.  The remaining lines will not be connected; a
   connection attempt will be refused with "All connections busy."  The second
   form sets the connection order to line 1, 5, 2, 3, 4, 7, 0, and 6.  The
   trailing "ALL" parameter causes any unspecified lines to be added to the
   connection order in ascending value.  The third form sets the order to lines
   0-7, which is the default order in the absence of a line connection order
   array.

   The range of accepted line numbers, including those implied by "ALL", can be
   restricted by specifying a non-zero "val" parameter, with the upper 16 bits
   specifying the maximum line number, and the lower 16 bits specifying the
   minimum line number.  If a minimum is specified but a maximum is not (i.e.,
   is zero), the maximum is the last line number defined by the multiplexer
   descriptor.

   The "uptr" parameter is not used.

   On entry, "cptr" points to the value portion of the command string, which may
   be either a semicolon-separated list of line ranges or the keyword "ALL".  If
   "ALL" is specified, it must be the last (or only) item in the list.

   If a line connection order array is not defined in the multiplexer
   descriptor, or a line range string is not present, or the optional minimum
   and maximum restrictions in the "val" parameter are not valid, the command is
   rejected.  If the specified range encompasses all of the lines defined by the
   multiplexer, the first value of the connection order array is set to -1 to
   indicate sequential connection order.  Otherwise, the line values in the
   array are set to the order specified by the command string.  If fewer values
   are supplied than there are lines supported by the device, and the final
   parameter is not ALL, the remaining lines will be inaccessible and are
   indicated by a -1 value after the last specified value.

   If an error occurs, the original line order is not disturbed.
*/

t_stat tmxr_set_lnorder (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
TMXR   *mp = (TMXR *) desc;
char   *tbuf = NULL;
char   *tptr;
t_addr low, high, min, max;
int32  *list;
t_bool *set;
uint32 line, idx;
t_addr lncount = (t_addr) (mp->lines - 1);
t_stat result = SCPE_OK;

if (mp->lnorder == NULL)                                /* if the connection order array is not defined */
    return SCPE_NXPAR;                                  /*   then report a "Non-existent parameter" error */

else if ((cptr == NULL) || (*cptr == '\0'))             /* otherwise if a line range was not supplied */
    return SCPE_MISVAL;                                 /*   then report a "Missing value" error */

else {                                                  /* otherwise */
    min = (t_addr) (val & 0xFFFF);                      /*   split the restriction into */
    max = (t_addr) ((val >> 16) & 0xFFFF);              /*     minimum and maximum line numbers */

    if (max == 0)                                       /* if the maximum line number isn't specified */
        max = lncount;                                  /*   then use the defined maximum */

    if (min > lncount || max > lncount || min > max)    /* if the restriction isn't valid */
        return SCPE_IERR;                               /*   then report an "Internal error" */
    }

list = (int32 *) calloc (mp->lines, sizeof (int32));    /* allocate the new line order array */

if (list == NULL)                                       /* if the allocation failed */
    return SCPE_MEM;                                    /*   then report a "Memory exhausted" error */

set = (t_bool *) calloc (mp->lines, sizeof (t_bool));   /* allocate the line set tracking array */

if (set == NULL) {                                      /* if the allocation failed */
    free (list);                                        /*   then free the successful list allocation */
    return SCPE_MEM;                                    /*      and report a "Memory exhausted" error */
    }

tbuf = (char *) calloc (strlen(cptr)+2, sizeof(*cptr));
if (tbuf == NULL) {                                     /* if the allocation failed */
    free (set);                                         /*   then free the line set tracking array */
    free (list);                                        /*        and successful list allocation */
    return SCPE_MEM;                                    /*      and report a "Memory exhausted" error */
    }

strcpy (tbuf, cptr);
tptr = tbuf + strlen (tbuf);                            /* append a semicolon */
*tptr++ = ';';                                          /*   to the command string */
*tptr = '\0';                                           /*   to make parsing easier for get_range */
cptr = tbuf;

idx = 0;                                                /* initialize the index of ordered values */

while (*cptr != '\0') {                                     /* while characters remain in the command string */
    if (strncasecmp (cptr, "ALL;", 4) == 0) {               /*   if the parameter is "ALL" */
        if ((val != 0) || ((idx > 0) && (idx <= max)))      /*     then if some lines are restricted or unspecified */
            for (line = (uint32)min; line <= (uint32)max; line++)/*  then fill them in sequentially */
                if (set [line] == FALSE)                    /*         setting each unspecified line */
                    list [idx++] = line;                    /*           into the line order */

        cptr = cptr + 4;                                /* advance past "ALL" and the trailing semicolon */

        if (*cptr != '\0')                              /* if "ALL" is not the last parameter */
            result = sim_messagef (SCPE_2MARG,          /*   then report extraneous items */
                            "Too many args: %s\n", cptr);

        break;                                          /* "ALL" terminates the order list */
        }

    cptr = get_range (NULL, cptr, &low, &high, 10, max, ';');   /* get a line range */

    if (cptr == NULL) {                                 /* if a parsing error occurred */
        result = SCPE_ARG;                              /*   then report an invalid argument */
        break;                                          /*     and terminate the parse */
        }

    else if ((low < min) ||
             (low > max) ||
             (high > max)) {                            /* otherwise if the line number is invalid */
        result = SCPE_SUB;                              /*   then report the subscript is out of range */
        break;                                          /*     and terminate the parse */
        }

    else                                                /* otherwise it's a valid range */
        for (line = (uint32)low; line <= (uint32)high; line++)/* so add the line(s) to the order */
            if (set [line] == FALSE) {                  /* if the line number has not been specified */
                set [line] = TRUE;                      /*   then now it is */
                list [idx++] = line;                    /*     and add it to the connection order */
                }
    }

if (result == SCPE_OK) {                                /* if the assignment succeeded */
    if (idx <= max)                                     /*   then if any lines were not specified */
        list [idx] = -1;                                /*     then terminate the order list after the last one */

    memcpy (mp->lnorder, list,                          /* copy the working array to the connection array */
            mp->lines * sizeof (int32));
    }

free (list);                                            /* free the list allocation */
free (set);                                             /*   and the set allocation */
free (tbuf);                                            /*   and the arg copy with ; */

return result;                                          /* return the status */
}

/* Show the line connection order.

   Parameters:
    - st   = stream on which output is to be written
    - uptr = (not used)
    - val  = (not used)
    - desc = pointer to multiplexer's TMXR structure

   If a connection order array is not defined in the multiplexer descriptor, the
   command is rejected.  If the first value of the connection order array is set
   to -1, then the connection order is sequential.  Otherwise, the line values
   in the array are printed as a semicolon-separated list.  Ranges are printed
   where possible to shorten the output.  A -1 value within the array indicates
   the end of the order list.
*/

t_stat tmxr_show_lnorder (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i, j, low, last;
const TMXR *mp = (const TMXR *) desc;
int32 *iptr = mp->lnorder;
t_bool first = TRUE;

if (iptr == NULL)                                       /* connection order undefined? */
    return SCPE_NXPAR;                                  /* "Non-existent parameter" error */

if (*iptr < 0)                                          /* sequential order indicated? */
    fprintf (st, "Order=0-%d\n", mp->lines - 1);        /* print full line range */

else {
    low = last = *iptr++;                               /* set first line value */

    for (j = 1; last != -1; j++) {                      /* print the remaining lines in the order list */
        if (j < mp->lines)                              /* more lines to process? */
            i = *iptr++;                                /* get next line in list */
        else                                            /* final iteration */
            i = -1;                                     /* get "tie-off" value */

        if (i != last + 1) {                            /* end of a range? */
            if (first) {                                /* first line to print? */
                fputs ("Order=", st);                   /* print header */
                first = FALSE;
                }

            else                                        /* not first line printed */
                fputc (';', st);                        /* print separator */

            if (low == last)                            /* range null? */
                fprintf (st, "%d", last);               /* print single line value */

            else                                        /* range established */
                fprintf (st, "%d-%d", low, last);       /* print start and end line */

            low = i;                                    /* start new range */
            }

        last = i;                                       /* note value for range check */
        }
    }

if (first == FALSE)                                     /* sanity check for lines == 0 */
    fputc ('\n', st);

return SCPE_OK;
}

/* Show summary processor */

t_stat tmxr_show_summ (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;
int32 i, t;

if (mp == NULL)
    return SCPE_IERR;
for (i = t = 0; i < mp->lines; i++)
    if ((mp->ldsc[i].sock != 0) || (mp->ldsc[i].serport != 0) || (mp->ldsc[i].console != 0))
        t = t + 1;
if (mp->lines > 1)
    fprintf (st, "%d current connection%s", t, (t != 1) ? "s" : "");
else
    fprintf (st, "%s", (t == 1) ? "connected" : "disconnected");
return SCPE_OK;
}

/* Show conn/stat processor */

t_stat tmxr_show_cstat (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;
int32 i, any;

if (mp == NULL)
    return SCPE_IERR;
for (i = any = 0; i < mp->lines; i++) {
    if ((mp->ldsc[i].sock != 0) ||
        (mp->ldsc[i].serport != 0) || mp->ldsc[i].modem_control) {
        if ((mp->ldsc[i].sock != 0) || (mp->ldsc[i].serport != 0))
            any++;
        if (val)
            tmxr_fconns (st, &mp->ldsc[i], i);
        else
            if ((mp->ldsc[i].sock != 0) || (mp->ldsc[i].serport != 0))
                tmxr_fstats (st, &mp->ldsc[i], i);
        }
    }
if (any == 0)
    fprintf (st, (mp->lines == 1? "disconnected\n": "all disconnected\n"));
return SCPE_OK;
}

/* Show synchronous devices */

t_stat tmxr_show_sync_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char *desc)
{
return tmxr_show_sync (st, uptr, val, NULL);
}

t_stat tmxr_show_sync (FILE* st, UNIT* uptr, int32 val, CONST void *desc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int number, fcnt = 0;

  number = eth_devices(ETH_MAX_DEVICE, list, TRUE);
  fprintf(st, "DDCMP synchronous link devices:\n");
  if (number == -1)
    fprintf(st, "  network support not available in simulator\n");
  else
    if (number == 0)
      fprintf(st, "  no ddcmp synchronous link devices are available\n");
    else {
      int i;
      for (i=0; i<number; i++) {
          fprintf(st," sync%d\t%s\n", i, list[i].name);
          fcnt++;
      }
    }
  return SCPE_OK;
}

/* Show number of lines */

t_stat tmxr_show_lines (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;

if (mp == NULL)
    return SCPE_IERR;
fprintf (st, "lines=%d", mp->lines);
return SCPE_OK;
}


static struct {
    u_char value;
    const char *name;
    } tn_chars[] =
    {
        {TN_IAC,    "TN_IAC"},                /* protocol delim */
        {TN_DONT,   "TN_DONT"},               /* dont */
        {TN_DO,     "TN_DO"},                 /* do */
        {TN_WONT,   "TN_WONT"},               /* wont */
        {TN_WILL,   "TN_WILL"},               /* will */
        {TN_SB,     "TN_SB"},                 /* sub-option negotiation */
        {TN_GA,     "TN_SG"},                 /* go ahead */
        {TN_EL,     "TN_EL"},                 /* erase line */
        {TN_EC,     "TN_EC"},                 /* erase character */
        {TN_AYT,    "TN_AYT"},                /* are you there */
        {TN_AO,     "TN_AO"},                 /* abort output */
        {TN_IP,     "TN_IP"},                 /* interrupt process */
        {TN_BRK,    "TN_BRK"},                /* break */
        {TN_DATAMK, "TN_DATAMK"},             /* data mark */
        {TN_NOP,    "TN_NOP"},                /* no operation */
        {TN_SE,     "TN_SE"},                 /* end sub-option negot */
        /* Options */
        {TN_BIN,    "TN_BIN"},                /* bin */
        {TN_ECHO,   "TN_ECHO"},               /* echo */
        {TN_SGA,    "TN_SGA"},                /* sga */
        {TN_STATUS, "TN_STATUS"},             /* option status query */
        {TN_TIMING, "TN_TIMING"},             /* Timing Mark */
        {TN_NAOCRD, "TN_NAOCRD"},             /* Output Carriage-Return Disposition */
        {TN_NAOHTS, "TN_NAOHTS"},             /* Output Horizontal Tab Stops */
        {TN_NAOHTD, "TN_NAOHTD"},             /* Output Horizontal Tab Stop Disposition */
        {TN_NAOFFD, "TN_NAOFFD"},             /* Output Formfeed Disposition */
        {TN_NAOVTS, "TN_NAOVTS"},             /* Output Vertical Tab Stop */
        {TN_NAOVTD, "TN_NAOVTD"},             /* Output Vertical Tab Stop Disposition */
        {TN_NAOLFD, "TN_NAOLFD"},             /* Output Linefeed Disposition */
        {TN_EXTEND, "TN_EXTEND"},             /* Extended Ascii */
        {TN_LOGOUT, "TN_LOGOUT"},             /* Logout */
        {TN_BM,     "TN_BM"},                 /* Byte Macro */
        {TN_DET,    "TN_DET"},                /* Data Entry Terminal */
        {TN_SENDLO, "TN_SENDLO"},             /* Send Location */
        {TN_TERMTY, "TN_TERMTY"},             /* Terminal Type */
        {TN_ENDREC, "TN_ENDREC"},             /* Terminal Type */
        {TN_TUID,   "TN_TUID"},               /* TACACS User Identification */
        {TN_OUTMRK, "TN_OUTMRK"},             /* Output Marking */
        {TN_TTYLOC, "TN_TTYLOC"},             /* Terminal Location Number */
        {TN_3270,   "TN_3270"},               /* 3270 Regime */
        {TN_X3PAD,  "TN_X3PAD"},              /* X.3 PAD */
        {TN_NAWS,   "TN_NAWS"},               /* Negotiate About Window Size */
        {TN_TERMSP, "TN_TERMSP"},             /* Terminal Speed */
        {TN_TOGFLO, "TN_TOGFLO"},             /* Remote Flow Control */
        {TN_LINE,   "TN_LINE"},               /* line mode */
        {TN_XDISPL, "TN_XDISPL"},             /* X Display Location */
        {TN_ENVIRO, "TN_ENVIRO"},             /* Environment */
        {TN_AUTH,   "TN_AUTH"},               /* Authentication */
        {TN_ENCRYP, "TN_ENCRYP"},             /* Data Encryption */
        {TN_NEWENV, "TN_NEWENV"},             /* New Environment */
        {TN_TN3270, "TN_TN3270"},             /* TN3270 Enhancements */
        {TN_CHARST, "TN_CHARST"},             /* CHARSET */
        {TN_COMPRT, "TN_COMPRT"},             /* Com Port Control */
        {TN_KERMIT, "TN_KERMIT"},             /* KERMIT */
        {0, NULL}};

static char *tmxr_debug_buf = NULL;
static size_t tmxr_debug_buf_used = 0;
static size_t tmxr_debug_buf_size = 0;

static void tmxr_buf_debug_char (char value)
{
if (tmxr_debug_buf_used+2 > tmxr_debug_buf_size) {
    tmxr_debug_buf_size += 1024;
    tmxr_debug_buf = (char *)realloc (tmxr_debug_buf, tmxr_debug_buf_size);
    }
tmxr_debug_buf[tmxr_debug_buf_used++] = value;
tmxr_debug_buf[tmxr_debug_buf_used] = '\0';
}

static void tmxr_buf_debug_string (const char *string)
{
while (*string)
    tmxr_buf_debug_char (*string++);
}

static void tmxr_buf_debug_telnet_option (u_char chr)
{
int j;

for (j=0; 1; ++j) {
    if (NULL == tn_chars[j].name) {
        if (isprint(chr))
            tmxr_buf_debug_char (chr);
        else {
            tmxr_buf_debug_char ('_');
            if ((chr >= 1) && (chr <= 26)) {
                tmxr_buf_debug_char ('^');
                tmxr_buf_debug_char ('A' + chr - 1);
                }
            else {
                char octal[8];

                sprintf(octal, "\\%03o", (u_char)chr);
                tmxr_buf_debug_string (octal);
                }
            tmxr_buf_debug_char ('_');
            }
        break;
        }
    if ((u_char)chr == tn_chars[j].value) {
        tmxr_buf_debug_char ('_');
        tmxr_buf_debug_string (tn_chars[j].name);
        tmxr_buf_debug_char ('_');
        break;
        }
    }
}

static int tmxr_buf_debug_telnet_options (u_char *buf, int bufsize)
{
int optsize = 2;

tmxr_buf_debug_telnet_option ((u_char)buf[0]);
tmxr_buf_debug_telnet_option ((u_char)buf[1]);
switch ((u_char)buf[1]) {
    case TN_IAC:
    default:
        return optsize;
        break;
    case TN_WILL:
    case TN_WONT:
    case TN_DO:
    case TN_DONT:
        ++optsize;
        tmxr_buf_debug_telnet_option ((u_char)buf[2]);
        break;
    }
return optsize;
}

void _tmxr_debug (uint32 dbits, TMLN *lp, const char *msg, char *buf, int bufsize)
{
DEVICE *dptr = (lp->dptr ? lp->dptr : (lp->mp ? lp->mp->dptr : NULL));

if ((dptr) && (dbits & dptr->dctrl)) {
    int i;

    tmxr_debug_buf_used = 0;
    if (tmxr_debug_buf)
        tmxr_debug_buf[tmxr_debug_buf_used] = '\0';

    if (lp->notelnet) {
        int same, group, sidx, oidx;
        char outbuf[80], strbuf[18];
        static char hex[] = "0123456789ABCDEF";

        for (i=same=0; i<bufsize; i += 16) {
            if ((i > 0) && (0 == memcmp(&buf[i], &buf[i-16], 16))) {
                ++same;
                continue;
                }
            if (same > 0) {
                if (lp->mp->lines > 1)
                    sim_debug (dbits, dptr, "Line:%d %04X thru %04X same as above\n", (int)(lp-lp->mp->ldsc), i-(16*same), i-1);
                else
                    sim_debug (dbits, dptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
                same = 0;
                }
            group = (((bufsize - i) > 16) ? 16 : (bufsize - i));
            for (sidx=oidx=0; sidx<group; ++sidx) {
                outbuf[oidx++] = ' ';
                outbuf[oidx++] = hex[(buf[i+sidx]>>4)&0xf];
                outbuf[oidx++] = hex[buf[i+sidx]&0xf];
                if (isprint((u_char)buf[i+sidx]))
                    strbuf[sidx] = buf[i+sidx];
                else
                    strbuf[sidx] = '.';
                }
            outbuf[oidx] = '\0';
            strbuf[sidx] = '\0';
            if (lp->mp->lines > 1)
                sim_debug (dbits, dptr, "Line:%d %04X%-48s %s\n", (int)(lp-lp->mp->ldsc), i, outbuf, strbuf);
            else
                sim_debug (dbits, dptr, "%04X%-48s %s\n", i, outbuf, strbuf);
            }
        if (same > 0) {
            if (lp->mp->lines > 1)
                sim_debug (dbits, dptr, "Line:%d %04X thru %04X same as above\n", (int)(lp-lp->mp->ldsc), i-(16*same), bufsize-1);
            else
                sim_debug (dbits, dptr, "%04X thru %04X same as above\n", i-(16*same), bufsize-1);
            }
        }
    else {
        tmxr_debug_buf_used = 0;
        if (tmxr_debug_buf)
            tmxr_debug_buf[tmxr_debug_buf_used] = '\0';
        for (i=0; i<bufsize; ++i) {
            switch ((u_char)buf[i]) {
                case TN_CR:
                    tmxr_buf_debug_string ("_TN_CR_");
                    break;
                case TN_LF:
                    tmxr_buf_debug_string ("_TN_LF_");
                    break;
                case TN_IAC:
                    if (!lp->notelnet) {
                        i += (tmxr_buf_debug_telnet_options ((u_char *)(&buf[i]), bufsize-i) - 1);
                        break;
                        }
                    /* fall through */
                default:
                    if (isprint((u_char)buf[i]))
                        tmxr_buf_debug_char (buf[i]);
                    else {
                        tmxr_buf_debug_char ('_');
                        if ((buf[i] >= 1) && (buf[i] <= 26)) {
                            tmxr_buf_debug_char ('^');
                            tmxr_buf_debug_char ('A' + buf[i] - 1);
                            }
                        else {
                            char octal[8];

                            sprintf(octal, "\\%03o", (u_char)buf[i]);
                            tmxr_buf_debug_string (octal);
                            }
                        tmxr_buf_debug_char ('_');
                        }
                    break;
                }
            }
        if (lp->mp->lines > 1)
            sim_debug (dbits, dptr, "Line:%d %s %d bytes '%s'\n", (int)(lp-lp->mp->ldsc), msg, bufsize, tmxr_debug_buf);
        else
            sim_debug (dbits, dptr, "%s %d bytes '%s'\n", msg, bufsize, tmxr_debug_buf);
        }
    if ((lp->rxnexttime != 0.0) || (lp->txnexttime != 0.0)) {
        if (lp->rxnexttime != 0.0)
            sim_debug (dbits, dptr, " rxnexttime=%.0f (%.0f usecs)", lp->rxnexttime, ((lp->rxnexttime - sim_gtime ()) / sim_timer_inst_per_sec ()) * 1000000.0);
        if (lp->txnexttime != 0.0)
            sim_debug (dbits, dptr, " txnexttime=%.0f (%.0f usecs)", lp->txnexttime, ((lp->txnexttime - sim_gtime ()) / sim_timer_inst_per_sec ()) * 1000000.0);
        sim_debug (dbits, dptr, "\n");
        }
    }
}

/* Testing of sim_sock and tmxr */

static struct lnorder_test {
    const char *orderspec;
    int32 valspec;
    t_stat expected_stat;
    int32 expected_orderlist[8];
    } lnorders[] = {
        {NULL,            0,         SCPE_MISVAL},
        {"",              0,         SCPE_MISVAL},
        {"4-7",  0x3FFF3FFF,         SCPE_IERR},
        {"6-8",           0,         SCPE_SUB},
        {"9-11",          0,         SCPE_SUB},
        {"4-7",           0,         SCPE_OK,
               {  4,  5,  6,  7, -1}},
        {"1;5;2-4;7;ALL", 0,         SCPE_OK,
               {  1,  5,  2,  3,  4,  7,  0,  6}},
        {"ALL",           0,         SCPE_OK,
               {  -1}},
    };

static t_stat _lnorder_test (TMXR *tmxr,
                             struct lnorder_test *t)
{
t_stat r;
char msg[80] = "";
int i;

r = tmxr_set_lnorder (NULL, t->valspec, t->orderspec, tmxr);
if (r != t->expected_stat) {
    snprintf (msg, sizeof (msg), "Unexpected lnorder result status for \"%s\" Expected: %s", t->orderspec, sim_error_text (t->expected_stat));
    return sim_messagef (SCPE_ARG, "%s, Got: %s\n", msg, sim_error_text (r));
    }
if (r == SCPE_OK) {
    for (i = 0; i<8; i++)
        if (t->expected_orderlist[i] != tmxr->lnorder[i])
            return sim_messagef (SCPE_ARG, "Unexpected order entry for line %d: %d vs %d\n", i, tmxr->lnorder[i], t->expected_orderlist[i]);
    }
return SCPE_OK;
}

static t_stat sim_tmxr_test_lnorder (TMXR *tmxr)
{
uint32 i;
int32 *saved_lnorder = tmxr->lnorder;
int32 saved_lines = tmxr->lines;

tmxr->lnorder = (int32 *)calloc (tmxr->lines, sizeof (*tmxr->lnorder));
if (tmxr->lines >= 8) {
    tmxr->lines = 8;
    for (i = 0; i < (sizeof (lnorders)/sizeof (lnorders[0])); ++i)
        _lnorder_test (tmxr, &lnorders[i]);
    }
free (tmxr->lnorder);
tmxr->lnorder = saved_lnorder;
tmxr->lines = saved_lines;
return SCPE_OK;
}


t_stat tmxr_sock_test (DEVICE *dptr, const char *cptr)
{
char cmd[CBUFSIZE], host[CBUFSIZE], port[CBUFSIZE];
int line;
TMXR *tmxr;
TMLN *ln;
int32 tmp1, tmp2;
t_stat stat = SCPE_OK;
SOCKET sock_mux = INVALID_SOCKET;
SOCKET sock_line = INVALID_SOCKET;
SIM_TEST_INIT;

sim_printf ("Testing %s:\n", dptr->name);
SIM_TEST(sim_parse_addr ("", NULL, 0, "localhost", NULL, 0, "1234", NULL) != -1);
SIM_TEST(sim_parse_addr ("", host, 0, "localhost", NULL, 0, "1234", NULL) != -1);
SIM_TEST(sim_parse_addr ("", host, sizeof(host), "localhost", port, 0, "1234", NULL) != -1);
SIM_TEST((sim_parse_addr ("", host, sizeof(host), "localhost", port, sizeof(port), "1234", NULL) == -1) || (strcmp(host, "localhost")) || (strcmp(port,"1234")));
SIM_TEST(sim_addr_acl_check ("127.0.0.1", NULL) == -1);
SIM_TEST(sim_addr_acl_check ("127.0.0.1/0", NULL) != -1);
SIM_TEST(sim_addr_acl_check ("127.0.0.1/32", NULL) == -1);
SIM_TEST(sim_addr_acl_check ("127.0.0.1/64", NULL) != -1);
SIM_TEST(sim_addr_acl_check ("127.0.0.6", "+127.0.0.1/32,-127.0.0.2") != -1);
SIM_TEST(sim_addr_acl_check ("127.0.0.2", "+127.0.0.1,-127.0.0.2/32,+127.0.0.3") != -1);
SIM_TEST(sim_parse_addr ("", host, sizeof(host), "localhost", port, sizeof(port), "1234", "127.0.0.1") == -1);
SIM_TEST((sim_parse_addr ("localhost:6666", host, sizeof(host), "localhost", port, sizeof(port), "1234", NULL) == -1) || (strcmp(host, "localhost")) || (strcmp(port,"6666")));
SIM_TEST(sim_parse_addr ("localhost:66666", host, sizeof(host), "localhost", port, sizeof(port), "1234", NULL) != -1);
SIM_TEST((sim_parse_addr ("localhost:telnet", host, sizeof(host), "localhost", port, sizeof(port), "1234", NULL) == -1) || (strcmp(host, "localhost")) || (strcmp(port,"telnet")));
SIM_TEST((sim_parse_addr ("telnet", host, sizeof(host), "localhost", port, sizeof(port), "1234", NULL) == -1) || (strcmp(host, "localhost")) || (strcmp(port,"telnet")));
dptr->dctrl = 0xFFFFFFFF;
dptr->dctrl &= ~TMXR_DBG_TRC;
sprintf (cmd, "%s -u localhost:65500;telnet;nomessage", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
tmxr = (TMXR *)dptr->units->tmxr;
ln = &tmxr->ldsc[tmxr->lines - 1];
SIM_TEST(detach_cmd (0, dptr->name));
sprintf (cmd, "%s -u localhost:65500;notelnet", dptr->name);
SIM_TEST(attach_cmd (0, cmd));
tmxr = (TMXR *)dptr->units->tmxr;
ln = &tmxr->ldsc[tmxr->lines - 1];
SIM_TEST(detach_cmd (0, dptr->name));
if (tmxr->lines > 1) {
    tmxr->modem_control = FALSE;
    for (line=0; line < tmxr->lines; line++)
        tmxr->ldsc[line].modem_control = FALSE;
    snprintf (cmd + strlen (cmd), sizeof (cmd) - strlen (cmd), ",Line=%d,localhost:65501", tmxr->lines - 1);
    snprintf (cmd + strlen (cmd), sizeof (cmd) - strlen (cmd), ",Line=0,connect=localhost:65500");
    SIM_TEST(attach_cmd (0, cmd));
    sock_line = sim_connect_sock_ex (NULL, "localhost:65501", NULL, NULL, 0);
    sim_os_ms_sleep (100);
    SIM_TEST((((tmp1 = tmxr_poll_conn (tmxr)) == tmxr->lines - 1) || (tmp1 == 1)) ? SCPE_OK : SCPE_IERR);
    sock_mux = sim_connect_sock ("", "localhost", "65500");
    sim_os_ms_sleep (100);
    SIM_TEST(((tmp2 = tmxr_poll_conn (tmxr)) == 0) || (tmp2 == 2) ? SCPE_OK : SCPE_IERR);
    show_cmd (0, "MUX");
    sim_close_sock (sock_mux);
    sock_mux = INVALID_SOCKET;
    sim_close_sock (sock_line);
    sock_line = INVALID_SOCKET;
    SIM_TEST(detach_cmd (0, dptr->name));
    SIM_TEST(sim_tmxr_test_lnorder (tmxr));
    }
return stat;
}

static int framer_await_status (TMLN *line, int cnt)
{
int i, stat, attempt, flen;
ETH_PACK framer_rpkt;

i = line->framer->status_cnt;
attempt = 0;
while (attempt < 5) {
    stat = eth_read (line->framer->eth, &framer_rpkt, NULL);
    if (stat) {
        flen  = framer_rpkt.msg[14] + (framer_rpkt.msg[15] << 8);
        if (framer_rpkt.msg[18] == 021) {
            /* DC1, so it's a framer status message.  Save it. */
            if (flen > sizeof (struct status_msg_t))
                flen = sizeof (struct status_msg_t);
            memcpy (&line->framer->status, framer_rpkt.msg + 18, flen);
            line->framer->status_cnt++;
            continue;
            }
        }
    if (i != line->framer->status_cnt)
        return 1;
    attempt++;
    sim_os_ms_sleep (50);
    }
tmxr_debug_trace_line (line, "no status received\n");
return 0;
}

static void tmxr_setup_framer(TMLN *line, ETH_PACK *packet, int len)
{
/* First clear everything */
memset (packet, 0, sizeof (*packet));
/* Set up the MAC header */
memcpy (&packet->msg[0], line->framer->eth->physical_addr, 6);
memcpy (&packet->msg[6], line->framer->eth->physical_addr, 6);
/* Framer address is one higher than host interface MAC address */
packet->msg[5]++;
/* Set ethertype 60-06 */
packet->msg[12] = 0x60;
packet->msg[13] = 0x06;
/* Set length */
packet->msg[14] = len & 0xff;
packet->msg[15] = len >> 8;
/* Set length and padded length */
len += 16;  /* Add in header length */
if (len < 60)
    len = 60;
packet->len = len;
packet->crc_len = len + 4;
}

void tmxr_start_framer (TMLN *line, int dmc_mode)
{
t_stat ret;
int cnt;
ETH_PACK framer_start;

if (!line->framer)
    /* Not a framer line, NOP */
    return;

tmxr_setup_framer (line, &framer_start, 8);
framer_start.msg[16] = 0x11;
framer_start.msg[17] = 1;       /* Command 1: start framer */
/* Set DMC mode (DDCMP 3.1) if requested */
if (dmc_mode)
    line->framer->fmode |= 32;
else
    line->framer->fmode &= ~32;
/* Set mode in the command buffer */
framer_start.msg[18] = line->framer->fmode & 0xff;
framer_start.msg[19] = line->framer->fmode >> 8;
/* Set speed in the command buffer */
framer_start.msg[20] = line->framer->fspeed & 0xff;
framer_start.msg[21] = (line->framer->fspeed >> 8) & 0xff;
framer_start.msg[22] = (line->framer->fspeed >> 16) & 0xff;
framer_start.msg[23] = line->framer->fspeed >> 24;
/* Send the request */
cnt = line->framer->status_cnt;
ret = eth_write (line->framer->eth, &framer_start, NULL);
framer_await_status (line, cnt);
}

void tmxr_stop_framer (TMLN *line)
{
t_stat ret;
int cnt;
ETH_PACK framer_stop;

if (!line->framer)
    /* Not a framer line, NOP */
    return;

tmxr_setup_framer (line, &framer_stop, 2);
framer_stop.msg[16] = 0x11;
framer_stop.msg[17] = 2;       /* Command 2: stop framer */
/* Send the request */
cnt = line->framer->status_cnt;
ret = eth_write (line->framer->eth, &framer_stop, NULL);
/* Mark the framer off right now */
line->framer->status.on = 0;
framer_await_status (line, cnt);
}

static int  tmxr_framer_read (TMLN *line, char *buf, int nbytes)
{
int stat, flen, fstat;
ETH_PACK framer_rpkt;

while (1) {
    stat = eth_read (line->framer->eth, &framer_rpkt, NULL);
    if (!stat)
        return 0;
    /* Size reported by framer includes status, subtract that */
    flen  = (framer_rpkt.msg[14] + (framer_rpkt.msg[15] << 8)) - 2;
    fstat = framer_rpkt.msg[16] + (framer_rpkt.msg[17] << 8);
    if (framer_rpkt.msg[18] == 021) {
        /* DC1, so it's a framer status message.  Save it. */
        if (flen > sizeof (struct status_msg_t))
            flen = sizeof (struct status_msg_t);
        memcpy (&line->framer->status,
                framer_rpkt.msg + 18, flen);
        /* report interesting bits */
        sim_debug (TMXR_DBG_RCV, line->dptr,
                   "framer status, on %d, last_cmd_sts %d\n",
                   line->framer->status.on,
                   line->framer->status.last_cmd_sts);
        line->framer->status_cnt++;
        /* Look for another packet */
        continue;
        }
    else {
        /* Real DDCMP packet.  Pass the buffer pointer/len */
        if (flen > nbytes)
            flen = nbytes;
        memcpy (buf, framer_rpkt.msg + 18, flen);
        return flen;
        }
    }
}

static int tmxr_framer_write (TMLN *line, const char *buf, int32 length)
{
t_stat ret;
ETH_PACK framer_tx;

tmxr_setup_framer (line, &framer_tx, length);
memcpy (&framer_tx.msg[16], buf, length);
/* Send the request */
ret = eth_write (line->framer->eth, &framer_tx, NULL);
/* Always report the whole thing was written */
return length;
}

