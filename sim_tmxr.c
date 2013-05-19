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
   tmxr_poll_rx -                       poll receive
   tmxr_putc_ln -                       put character for line
   tmxr_poll_tx -                       poll transmit
   tmxr_send_buffered_data -            transmit buffered data
   tmxr_set_modem_control_passthru -    enable modem control on a multiplexer
   tmxr_clear_modem_control_passthru -  disable modem control on a multiplexer
   tmxr_set_get_modem_bits -            set and/or get a line modem bits
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
     
        sim> attach MUX Line=2,port{;notelnet}

    Direct computer to computer connections (Virutal Null Modem cables) may 
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

        sim> attach MX port{;notelnet}
        sim> attach MX Connect=serN{;config}
        sim> attach MX Connect=COM9{;config}
        sim> attach MX Connect=host:port{;notelnet}

     The command syntax for ANY multi-line device is:

        sim> attach MX port{;notelnet}              ; Defines the master listening port for the mux and optionally allows non-telnet (i.e. raw socket) operation for all lines.
        sim> attach MX Line=n,port{;notelnet}       ; Defines a line specific listen port for a particular line. Each line can have a separate listen port and the mux can have its own as well.  Optionally disable telnet wire protocol (i.e. raw socket)
        sim> attach MX Line=n,Connect=serN{;config} ; Connects line n to simh generic serial port N (port list visible with the sim> SHOW SERIAL command), the optional ";config" data specifies the speed, parity and stop bits for the connection
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
        sim> attach MUX 12366
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

   A line which is connected to a serial port can be manually closed by
   adding the -C switch to a disconnect command.

     sim> set -C MUX disconnect=2

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
#include "sim_serial.h"
#include "sim_sock.h"
#include "sim_timer.h"
#include "sim_tmxr.h"
#include "scp.h"

#include <ctype.h>

/* Telnet protocol constants - negatives are for init'ing signed char data */

/* Commands */
#define TN_IAC          0xFF /* -1 */                              /* protocol delim */
#define TN_DONT         0xFE /* -2 */                              /* dont */
#define TN_DO           0xFD /* -3 */                              /* do */
#define TN_WONT         0xFC /* -4 */                              /* wont */
#define TN_WILL         0xFB /* -5 */                              /* will */
#define TN_SB           0xFA /* -6 */                              /* sub-option negotiation */
#define TN_GA           0xF9 /* -7 */                              /* go ahead */
#define TN_EL           0xF8 /* -8 */                              /* erase line */
#define TN_EC           0xF7 /* -9 */                              /* erase character */
#define TN_AYT          0xF6 /* -10 */                             /* are you there */
#define TN_AO           0xF5 /* -11 */                             /* abort output */
#define TN_IP           0xF4 /* -12 */                             /* interrupt process */
#define TN_BRK          0xF3 /* -13 */                             /* break */
#define TN_DATAMK       0xF2 /* -14 */                             /* data mark */
#define TN_NOP          0xF1 /* -15 */                             /* no operation */
#define TN_SE           0xF0 /* -16 */                             /* end sub-option negot */

/* Options */

#define TN_BIN            0                             /* bin */
#define TN_ECHO           1                             /* echo */
#define TN_SGA            3                             /* sga */
#define TN_STATUS         5                             /* option status query */
#define TN_TIMING         6                             /* Timing Mark */
#define TN_NAOCRD        10                             /* Output Carriage-Return Disposition */
#define TN_NAOHTS        11                             /* Output Horizontal Tab Stops */
#define TN_NAOHTD        12                             /* Output Horizontal Tab Stop Disposition */
#define TN_NAOFFD        13                             /* Output Forfeed Disposition */
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

static BITFIELD tmxr_modem_bits[] = {
  BIT(DTR),                                 /* Data Terminal Ready */
  BIT(RTS),                                 /* Request To Send     */
  BIT(DCD),                                 /* Data Carrier Detect */
  BIT(RNG),                                 /* Ring Indicator      */
  BIT(CTS),                                 /* Clear To Send       */
  BIT(DSR),                                 /* Data Set Ready      */
  ENDBITS
};

/* Local routines */


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
lp->rxbpr = lp->rxbpi = lp->rxcnt = 0;                  /* init receive indexes */
if (!lp->txbfd)                                         /* if not buffered */
    lp->txbpr = lp->txbpi = lp->txcnt = 0;              /*   init transmit indexes */
memset (lp->rbr, 0, sizeof(lp->rbr));                   /* clear break status array */
lp->txdrp = 0;
if (lp->modem_control)
    lp->modembits = TMXR_MDM_CTS | TMXR_MDM_DSR;
if (!lp->mp->buffered) {
    lp->txbfd = 0;
    lp->txbsz = TMXR_MAXBUF;
    lp->txb = (char *)realloc (lp->txb, lp->txbsz);
    }
return;
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
char cmsg[80];
char dmsg[80] = "";
char lmsg[80] = "";
char msgbuf[256] = "";

if ((!lp->notelnet) || (sim_switches & SWMASK ('V'))) {
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
    lp->rxcnt = lp->txcnt = lp->txdrp = 0;              /* init counters */
    }
else
    if (lp->txcnt > lp->txbsz)
        lp->txbpr = (lp->txbpi + 1) % lp->txbsz;
    else
        lp->txbpr = (int32)(lp->txbsz - strlen (msgbuf));

psave = lp->txbpi;                                      /* save insertion pointer */
lp->txbpi = lp->txbpr;                                  /* insert connection message */
tmxr_linemsg (lp, msgbuf);                              /* beginning of buffer */
lp->txbpi = psave;                                      /* restore insertion pointer */

unwritten = tmxr_send_buffered_data (lp);               /* send the message */

if (unwritten == 0)                                     /* buffer now empty? */
    lp->xmte = 1;                                       /* reenable transmission if paused */

lp->txcnt -= (int32)strlen (msgbuf);                    /* adjust statistics */
return;
}


/* Report a disconnection to a line.

   A notification of the form:

      Disconnected from the <sim> simulator

   is sent to the line about to be disconnected.  We do not flush the buffer
   here, because the disconnect routines will do that just after calling us.
*/

static void tmxr_report_disconnection (TMLN *lp)
{
if (lp->notelnet)
    return;
tmxr_linemsgf (lp, "\nDisconnected from the %s simulator\n\n", sim_name);/* report disconnection */
return;
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

if (lp->serport)                                        /* serial port connection? */
    return sim_read_serial (lp->serport, &(lp->rxb[i]), length, &(lp->rbr[i]));
else                                                    /* Telnet connection */
    return sim_read_sock (lp->sock, &(lp->rxb[i]), length);
}


/* Write to a line.

   Up to "length" characters are written from the character buffer associated
   with "lp".  The actual number of characters written is returned.  If an error
   occurred while writing, -1 is returned.
*/

static int32 tmxr_write (TMLN *lp, int32 length)
{
int32 written;
int32 i = lp->txbpr;

if (lp->serport)                                        /* serial port connection? */
    return sim_write_serial (lp->serport, &(lp->txb[i]), length);

else {                                                  /* Telnet connection */
    written = sim_write_sock (lp->sock, &(lp->txb[i]), length);

    if (written == SOCKET_ERROR)                        /* did an error occur? */
        return -1;                                      /* return error indication */
    else
        return written;
    }
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
return;
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

static TMLN *tmxr_find_ldsc (UNIT *uptr, int32 val, TMXR *mp)
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

static TMLN *tmxr_get_ldsc (UNIT *uptr, char *cptr, TMXR *mp, t_stat *status)
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
    code = SCPE_ARG;                                    /* no, so report missing */

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

static char *_mux_attach_string(char *old, TMXR *mp)
{
char* tptr = NULL;
int32 i;
TMLN *lp;

free (old);
tptr = (char *) calloc (1, 1);

if (tptr == NULL)                                       /* no more mem? */
    return tptr;

if (mp->port)                                           /* copy port */
    sprintf (growstring(&tptr, 13 + strlen (mp->port)), "%s%s", mp->port, mp->notelnet ? ";notelnet" : "");
if (mp->buffered)
    sprintf (growstring(&tptr, 32), ",Buffered=%d", mp->buffered);
if (mp->logfiletmpl[0])                                 /* logfile info */
    sprintf (growstring(&tptr, 7 + strlen (mp->logfiletmpl)), ",Log=%s", mp->logfiletmpl);
while ((*tptr == ',') || (*tptr == ' '))
    strcpy(tptr, tptr+1);
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
        strcpy(tptr, tptr+1);
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

if (lp->destination || lp->port || lp->txlogname) {
    if ((lp->mp->lines > 1) || (lp->port))
        sprintf (growstring(&tptr, 32), "Line=%d", (int)(lp-lp->mp->ldsc));
    if (lp->modem_control != lp->mp->modem_control)
        sprintf (growstring(&tptr, 32), ",%s", lp->modem_control ? "Modem" : "NoModem");
    if (lp->destination) {
        if (lp->serport) {
            char portname[CBUFSIZE];

            get_glyph_nc (lp->destination, portname, ';');
            sprintf (growstring(&tptr, 25 + strlen (lp->destination)), ",Connect=%s%s%s", portname, strcmp("9600-8N1", lp->serconfig) ? ";" : "", strcmp("9600-8N1", lp->serconfig) ? lp->serconfig : "");
            }
        else
            sprintf (growstring(&tptr, 25 + strlen (lp->destination)), ",Connect=%s%s", lp->destination, (lp->mp->notelnet != lp->notelnet) ? (lp->notelnet ? ";notelnet" : ";telnet") : "");
        }
    if (lp->port)
        sprintf (growstring(&tptr, 12 + strlen (lp->port)), ",%s%s", lp->port, (lp->mp->notelnet != lp->notelnet) ? (lp->notelnet ? ";notelnet" : ";telnet") : "");
    if (lp->txlogname)
        sprintf (growstring(&tptr, 12 + strlen (lp->txlogname)), ",Log=%s", lp->txlogname);
    }
if (*tptr == '\0') {
    free (tptr);
    tptr = NULL;
    }
return tptr;
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
char *address;
char msg[512];
uint32 poll_time = sim_os_msec ();
static u_char mantra[] = {
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

if (mp->last_poll_time == 0) {                          /* first poll initializations */
    UNIT *uptr = mp->uptr;

    if (!uptr)                                          /* Attached ? */
        return -1;                                      /* No connections are possinle! */

    if (!(uptr->dynflags & TMUF_NOASYNCH)) {            /* if asynch not disabled */
        uptr->dynflags |= UNIT_TM_POLL;                 /* tag as polling unit */
        sim_cancel (uptr);
        }
    for (i=0; i < mp->lines; i++) {
        uptr = mp->ldsc[i].uptr ? mp->ldsc[i].uptr : mp->uptr;

        if (!(mp->uptr->dynflags & TMUF_NOASYNCH)) {    /* if asynch not disabled */
            uptr->dynflags |= UNIT_TM_POLL;             /* tag as polling unit */
            sim_cancel (uptr);
            }
        }
    }

if ((poll_time - mp->last_poll_time) < TMXR_CONNECT_POLL_INTERVAL)
    return -1;                                          /* too soon to try */

srand((unsigned int)poll_time);
tmxr_debug_trace (mp, "tmxr_poll_conn()");

mp->last_poll_time = poll_time;

/* Check for a pending Telnet/tcp connection */

if (mp->master) {
    newsock = sim_accept_conn (mp->master, &address);   /* poll connect */

    if (newsock != INVALID_SOCKET) {                    /* got a live one? */
        sprintf (msg, "tmxr_poll_conn() - Connection from %s", address);
        tmxr_debug_connect (mp, msg);
        op = mp->lnorder;                               /* get line connection order list pointer */
        i = mp->lines;                                  /* play it safe in case lines == 0 */
        ++mp->sessions;                                 /* count the new session */

        for (j = 0; j < mp->lines; j++, i++) {          /* find next avail line */
            if (op && (*op >= 0) && (*op < mp->lines))  /* order list present and valid? */
                i = *op++;                              /* get next line in list to try */
            else                                        /* no list or not used or range error */
                i = j;                                  /* get next sequential line */

            lp = mp->ldsc + i;                          /* get pointer to line descriptor */
            if ((lp->conn == FALSE) &&                  /* is the line available? */
                (lp->destination == NULL) &&
                (lp->master == 0))
                break;                                  /* yes, so stop search */
            }

        if (i >= mp->lines) {                           /* all busy? */
            tmxr_msg (newsock, "All connections busy\r\n");
            tmxr_debug_connect (mp, "tmxr_poll_conn() - All connections busy");
            sim_close_sock (newsock, 0);
            free (address);
            }
        else {
            lp = mp->ldsc + i;                          /* get line desc */
            tmxr_init_line (lp);                        /* init line */
            lp->conn = TRUE;                            /* record connection */
            lp->sock = newsock;                         /* save socket */
            lp->ipad = address;                         /* ip address */
            lp->notelnet = mp->notelnet;                /* apply mux default telnet setting */
            if (!lp->notelnet) {
                sim_write_sock (newsock, (char *)mantra, sizeof(mantra));
                tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                }
            tmxr_report_connection (mp, lp);
            lp->cnms = sim_os_msec ();                  /* time of connection */
            return i;
            }
        }                                               /* end if newsock */
    }

/* Look for per line listeners or outbound connecting sockets */
for (i = 0; i < mp->lines; i++) {                       /* check each line in sequence */
    int j, r = rand();
    lp = mp->ldsc + i;                                  /* get pointer to line descriptor */

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
                            lp->ipad = realloc (lp->ipad, 1+strlen (lp->destination));
                            strcpy (lp->ipad, lp->destination);
                            lp->cnms = sim_os_msec ();
                            sim_getnames_sock (lp->sock, &sockname, &peername);
                            sprintf (msg, "tmxr_poll_conn() - Outgoing Line Connection to %s (%s->%s) established", lp->destination, sockname, peername);
                            tmxr_debug_connect_line (lp, msg);
                            free (sockname);
                            free (peername);
                            break;
                        case -1:                                /* failed connection */
                            sprintf (msg, "tmxr_poll_conn() - Outgoing Line Connection to %s failed", lp->destination);
                            tmxr_debug_connect_line (lp, msg);
                            tmxr_reset_ln (lp);                 /* retry */
                            break;
                        }
                    }
                break;
            case 1:
                if (lp->master) {                                   /* Check for a pending Telnet/tcp connection */
                    while (INVALID_SOCKET != (newsock = sim_accept_conn (lp->master, &address))) {/* got a live one? */
                        char *sockname, *peername;

                        sim_getnames_sock (newsock, &sockname, &peername);
                        sprintf (msg, "tmxr_poll_conn() - Incoming Line Connection from %s (%s->%s)", address, peername, sockname);
                        tmxr_debug_connect_line (lp, msg);
                        free (sockname);
                        free (peername);
                        ++mp->sessions;                             /* count the new session */

                        if (lp->destination) {                      /* Virtual Null Modem Cable? */
                            char host[CBUFSIZE];

                            if (sim_parse_addr (lp->destination, host, sizeof(host), NULL, NULL, 0, NULL, address)) {
                                tmxr_msg (newsock, "Rejecting connection from unexpected source\r\n");
                                sprintf (msg, "tmxr_poll_conn() - Rejecting line connection from: %s, Expected: %s", address, host);
                                tmxr_debug_connect_line (lp, msg);
                                sim_close_sock (newsock, 0);
                                free (address);
                                continue;                           /* Try for another connection */
                                }
                            if (lp->connecting) {
                                sprintf (msg, "tmxr_poll_conn() - aborting outgoing line connection attempt to: %s", lp->destination);
                                tmxr_debug_connect_line (lp, msg);
                                sim_close_sock (lp->connecting, 0); /* abort our as yet unconnnected socket */
                                lp->connecting = 0;
                                }
                            }
                        if (lp->conn == FALSE) {                    /* is the line available? */
                            if ((!lp->modem_control) || (lp->modembits & TMXR_MDM_DTR)) {
                                tmxr_init_line (lp);                /* init line */
                                lp->conn = TRUE;                    /* record connection */
                                lp->sock = newsock;                 /* save socket */
                                lp->ipad = address;                 /* ip address */
                                if (!lp->notelnet) {
                                    sim_write_sock (newsock, (char *)mantra, sizeof(mantra));
                                    tmxr_debug (TMXR_DBG_XMT, lp, "Sending", (char *)mantra, sizeof(mantra));
                                    }
                                tmxr_report_connection (mp, lp);
                                lp->cnms = sim_os_msec ();          /* time of connection */
                                return i;
                                }
                            else {
                                tmxr_msg (newsock, "Line connection not available\r\n");
                                tmxr_debug_connect_line (lp, "tmxr_poll_conn() - Line connection not available");
                                sim_close_sock (newsock, 0);
                                free (address);
                                }
                            }
                        else {
                            tmxr_msg (newsock, "Line connection busy\r\n");
                            tmxr_debug_connect_line (lp, "tmxr_poll_conn() - Line connection busy");
                            sim_close_sock (newsock, 0);
                            free (address);
                            }
                        }
                    }
                break;
            }

    /* Check for pending serial port connection notification */
    
    if (lp->ser_connect_pending) {
        lp->ser_connect_pending = FALSE;
        lp->conn = TRUE;
        return i;
        }

    /* Check for needed outgoing connection initiation */

    if (lp->destination && (!lp->sock) && (!lp->connecting) && (!lp->serport) && 
        (!lp->modem_control || (lp->modembits & TMXR_MDM_DTR))) {
        sprintf (msg, "tmxr_poll_conn() - establishing outgoing connection to: %s", lp->destination);
        tmxr_debug_connect_line (lp, msg);
        lp->connecting = sim_connect_sock (lp->destination, "localhost", NULL);
        }

    }

return -1;                                              /* no new connections made */
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
        sim_close_sock (lp->sock, 0);                   /* close socket */
        lp->sock = 0;
        lp->conn = FALSE;
        lp->cnms = 0;
        lp->xmte = 1;
        }
free(lp->ipad);
lp->ipad = NULL;
if ((lp->destination) && (!lp->serport)) {
    if (lp->connecting) {
        sim_close_sock (lp->connecting, 0);
        lp->connecting = 0;
        }
    if ((!lp->modem_control) || (lp->modembits & TMXR_MDM_DTR)) {
        sprintf (msg, "tmxr_reset_ln_ex() - connecting to %s", lp->destination);
        tmxr_debug_connect_line (lp, msg);
        lp->connecting = sim_connect_sock (lp->destination, "localhost", NULL);
        }
    }
tmxr_init_line (lp);                                /* initialize line state */
if (lp->mp->uptr) {
    /* Revise the unit's connect string to reflect the current attachments */
    lp->mp->uptr->filename = _mux_attach_string (lp->mp->uptr->filename, lp->mp);
    /* No connections or listeners exist, then we're equivalent to being fully detached.  We should reflect that */
    if (lp->mp->uptr->filename == NULL)
        tmxr_detach (lp->mp, lp->mp->uptr);
    }
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
       outgoing connecctions associated with the specified multiplexer

*/
t_stat tmxr_clear_modem_control_passthru (TMXR *mp)
{
return tmxr_clear_modem_control_passthru_state (mp, FALSE);
}

/* Manipulate the modem control bits of a specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
        bits_to_set     TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired
        bits_to_clear   TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired
        
   Output:
        incoming_bits   if non NULL, returns the current stat of DCD, 
                        RNG, CTS and DSR

   Implementation note:

       If a line is connected to a serial port, then these valus affect 
       and reflect the state of the serial port.  If the line is connected
       to a network socket (or could be) then the network session state is
       set, cleared and/or returned.
*/
t_stat tmxr_set_get_modem_bits (TMLN *lp, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
int32 before_modem_bits, incoming_state;

tmxr_debug_trace_line (lp, "tmxr_set_get_modem_bits()");

if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;
before_modem_bits = lp->modembits;
lp->modembits |= bits_to_set;
lp->modembits &= ~(bits_to_clear | TMXR_MDM_INCOMING);
if ((lp->sock) || (lp->serport)) {
    if (lp->modembits & TMXR_MDM_DTR)
        incoming_state = TMXR_MDM_DCD | TMXR_MDM_CTS | TMXR_MDM_DSR;
    else
        incoming_state = TMXR_MDM_RNG | TMXR_MDM_DCD | TMXR_MDM_CTS | TMXR_MDM_DSR;
    }
else
    incoming_state = (lp->mp && lp->mp->master) ? (TMXR_MDM_CTS | TMXR_MDM_DSR) : 0;
lp->modembits |= incoming_state;
if (sim_deb && lp->mp && lp->mp->dptr) {
    sim_debug_bits (TMXR_DBG_MDM, lp->mp->dptr, tmxr_modem_bits, before_modem_bits, lp->modembits, FALSE);
    sim_debug (TMXR_DBG_MDM, lp->mp->dptr, " - Line %d - %p\n", (int)(lp-lp->mp->ldsc), lp->txb);
    }
if (incoming_bits)
    *incoming_bits = incoming_state;
if (lp->mp && lp->modem_control) {                  /* This API ONLY works on modem_control enabled multiplexer lines */
    if (bits_to_set | bits_to_clear) {              /* Anything to do? */
        if (lp->serport)
            return sim_control_serial (lp->serport, bits_to_set, bits_to_clear, incoming_bits);
        if ((lp->sock) || (lp->connecting)) {
            if (bits_to_clear&TMXR_MDM_DTR)             /* drop DTR? */
                tmxr_reset_ln (lp);
            }
        else {
            if ((lp->destination) &&                    /* Virtual Null Modem Cable */
                ((bits_to_set ^ before_modem_bits) &    /* and DTR being Raised */
                 TMXR_MDM_DTR)) {
                char msg[512];

                sprintf (msg, "tmxr_set_get_modem_bits() - establishing outgoing connection to: %s", lp->destination);
                tmxr_debug_connect_line (lp, msg);
                lp->connecting = sim_connect_sock (lp->destination, "localhost", NULL);
                }
            }
        }
    return SCPE_OK;
    }
if (lp->serport)
    sim_control_serial (lp->serport, 0, 0, incoming_bits);
return SCPE_IERR;
}

t_stat tmxr_set_config_line (TMLN *lp, char *config)
{
t_stat r;

tmxr_debug_trace_line (lp, "tmxr_set_config_line()");
if (!lp->modem_control)                             /* This API ONLY works on modem_control enabled multiplexer lines */
    return SCPE_IERR;
if (lp->serport)
    r = sim_config_serial (lp->serport, config);
else {
    lp->serconfig = (char *)realloc (lp->serconfig, 1 + strlen (config));
    strcpy (lp->serconfig, config);
    r = SCPE_OK;
    }
if (r == SCPE_OK)                                   /* Record port state for proper restore */
    lp->mp->uptr->filename = _mux_attach_string (lp->mp->uptr->filename, lp->mp);
return r;
}


/* Get character from specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
   Output:
        valid + char, 0 if line

   Implementation note:

    1. If a line break was detected coincident with the current character, the
       receive break status associated with the character is cleared, and
       SCPE_BREAK is ORed into the return value.
*/

int32 tmxr_getc_ln (TMLN *lp)
{
int32 j, val = 0;
uint32 tmp;

tmxr_debug_trace_line (lp, "tmxr_getc_ln()");
if (lp->conn && lp->rcve) {                             /* conn & enb? */
    j = lp->rxbpi - lp->rxbpr;                          /* # input chrs */
    if (j) {                                            /* any? */
        tmp = lp->rxb[lp->rxbpr];                       /* get char */
        val = TMXR_VALID | (tmp & 0377);                /* valid + chr */
        if (lp->rbr[lp->rxbpr]) {                       /* break? */
            lp->rbr[lp->rxbpr] = 0;                     /* clear status */
            val = val | SCPE_BREAK;                     /* indicate to caller */
            }
        lp->rxbpr = lp->rxbpr + 1;                      /* adv pointer */
        }
    }                                                   /* end if conn */
if (lp->rxbpi == lp->rxbpr)                             /* empty? zero ptrs */
    lp->rxbpi = lp->rxbpr = 0;
return val;
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
    if (!(lp->sock || lp->serport) || !lp->rcve)        /* skip if not connected */
        continue;

    nbytes = 0;
    if (lp->rxbpi == 0)                                 /* need input? */
        nbytes = tmxr_read (lp,                         /* yes, read */
            TMXR_MAXBUF - TMXR_GUARD);                  /* leave spc for Telnet cruft */
    else if (lp->tsta)                                  /* in Telnet seq? */
        nbytes = tmxr_read (lp,                         /* yes, read to end */
            TMXR_MAXBUF - lp->rxbpi);

    if (nbytes < 0) {                                   /* line error? */
        if (!lp->txbfd) 
            lp->txbpi = lp->txbpr = 0;                  /* Drop the data we already know we can't send */
        tmxr_close_ln (lp);                             /* disconnect line */
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

                case TNS_WILL: case TNS_WONT:           /* IAC+WILL/WONT prev */
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
                case TNS_SKIP: default:                 /* skip char */
                    tmxr_rmvrc (lp, j);                 /* remove char */
                    lp->tsta = TNS_NORM;                /* next normal */
                    break;
                    }                                   /* end case state */
                }                                       /* end for char */
            if (nbytes != (lp->rxbpi-lp->rxbpr)) {
                tmxr_debug (TMXR_DBG_RCV, lp, "Remaining", &(lp->rxb[lp->rxbpi]), lp->rxbpi-lp->rxbpr);
                }
            }
        }                                               /* end else nbytes */
    }                                                   /* end for lines */
for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (lp->rxbpi == lp->rxbpr)                         /* if buf empty, */
        lp->rxbpi = lp->rxbpr = 0;                      /* reset pointers */
    }                                                   /* end for */
return;
}


/* Return count of available characters for line */

int32 tmxr_rqln (TMLN *lp)
{
return (lp->rxbpi - lp->rxbpr + ((lp->rxbpi < lp->rxbpr)? TMXR_MAXBUF: 0));
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
if ((lp->conn == FALSE) &&                              /* no conn & not buffered? */
    (!lp->txbfd)) {
    ++lp->txdrp;                                        /* lost */
    return SCPE_LOST;
    }
tmxr_debug_trace_line (lp, "tmxr_putc_ln()");
#define TXBUF_AVAIL(lp) (lp->txbsz - tmxr_tqln (lp))
#define TXBUF_CHAR(lp, c) {                               \
    lp->txb[lp->txbpi++] = (char)(c);                     \
    lp->txbpi %= lp->txbsz;                               \
    if (lp->txbpi == lp->txbpr)                           \
        lp->txbpr = (1+lp->txbpr)%lp->txbsz, ++lp->txdrp; \
    }
if ((lp->txbfd) || (TXBUF_AVAIL(lp) > 1)) {             /* room for char (+ IAC)? */
    if ((TN_IAC == (u_char) chr) && (!lp->notelnet))      /* char == IAC in telnet session? */
        TXBUF_CHAR (lp, TN_IAC);                        /* stuff extra IAC char */
    TXBUF_CHAR (lp, chr);                               /* buffer char & adv pointer */
    if ((!lp->txbfd) && (TXBUF_AVAIL (lp) <= TMXR_GUARD))/* near full? */
        lp->xmte = 0;                                   /* disable line */
    if (lp->txlog)                                      /* log if available */
        fputc (chr, lp->txlog);
    return SCPE_OK;                                     /* char sent */
    }
++lp->txdrp; lp->xmte = 0;                              /* no room, dsbl line */
return SCPE_STALL;                                      /* char not sent */
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

tmxr_debug_trace (mp, "tmxr_poll_tx()");
for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (!lp->conn)                                      /* skip if !conn */
        continue;
    nbytes = tmxr_send_buffered_data (lp);              /* buffered bytes */
    if (nbytes == 0) {                                  /* buf empty? enab line */
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
        UNIT *ruptr = lp->uptr ? lp->uptr : lp->mp->uptr;
        if ((ruptr->dynflags & UNIT_TM_POLL) &&
            sim_asynch_enabled &&
            tmxr_rqln (lp))
            _sim_activate (ruptr, 0);
#endif
        lp->xmte = 1;                                   /* enable line transmit */
        }
    }                                                   /* end for */
return;
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
        }
    if (sbytes < 0) {                                   /* I/O Error? */
        lp->txbpi = lp->txbpr = 0;                      /* Drop the data we already know we can't send */
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
return nbytes;
}


/* Return count of buffered characters for line */

int32 tmxr_tqln (TMLN *lp)
{
return (lp->txbpi - lp->txbpr + ((lp->txbpi < lp->txbpr)? lp->txbsz: 0));
}

static void _mux_detach_line (TMLN *lp, t_bool close_listener, t_bool close_connecting)
{
if (close_listener && lp->master) {
    sim_close_sock (lp->master, 1);
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
    if (lp->connecting) {      /* if existing outgoing tcp, drop it */
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
}

t_stat tmxr_detach_ln (TMLN *lp)
{
tmxr_debug_trace_line (lp, "tmxr_detaach_ln()");
_mux_detach_line (lp, TRUE, TRUE);
return SCPE_OK;
}

/* Open a master listening socket (and all of the other variances of connections).

   A listening socket for the port number described by "cptr" is opened for the
   multiplexer associated with descriptor "mp".  If the open is successful, all
   lines not currently otherwise connected (via serial, outgoing or direct 
   listener) are initialized for Telnet connections.

   Initialization for all connection styles (MUX wide listener, per line serial, 
   listener, outgoing, logging, buffering) are handled by this routine.

*/

t_stat tmxr_open_master (TMXR *mp, char *cptr)
{
int32 i, line, nextline = -1;
char tbuf[CBUFSIZE], listen[CBUFSIZE], destination[CBUFSIZE], 
     logfiletmpl[CBUFSIZE], buffered[CBUFSIZE], hostport[CBUFSIZE], 
     port[CBUFSIZE], option[CBUFSIZE];
SOCKET sock;
SERHANDLE serport;
char *tptr = cptr;
t_bool nolog, notelnet, listennotelnet, unbuffered, modem_control;
TMLN *lp;
t_stat r = SCPE_ARG;

for (i = 0; i < mp->lines; i++) {               /* initialize lines */
    lp = mp->ldsc + i;
    lp->mp = mp;                                /* set the back pointer */
    lp->modem_control = mp->modem_control;
    }
tmxr_debug_trace (mp, "tmxr_open_master()");
while (*tptr) {
    line = nextline;
    memset(logfiletmpl, '\0', sizeof(logfiletmpl));
    memset(listen,      '\0', sizeof(listen));
    memset(destination, '\0', sizeof(destination));
    memset(buffered,    '\0', sizeof(buffered));
    memset(port,        '\0', sizeof(port));
    memset(option,      '\0', sizeof(option));
    nolog = notelnet = listennotelnet = unbuffered = FALSE;
    if (line != -1)
        notelnet = listennotelnet = mp->notelnet;
    modem_control = mp->modem_control;
    while (*tptr) {
        tptr = get_glyph_nc (tptr, tbuf, ',');
        if (!tbuf[0])
            break;
        cptr = tbuf;
        if (!isdigit(*cptr)) {
            char gbuf[CBUFSIZE];
            char *init_cptr = cptr;

            cptr = get_glyph (cptr, gbuf, '=');
            if (0 == MATCH_CMD (gbuf, "LINE")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return SCPE_ARG;
                nextline = (int32) get_uint (cptr, 10, mp->lines-1, &r);
                if (r != SCPE_OK)
                    return SCPE_ARG;
                break;
                }
            if (0 == MATCH_CMD (gbuf, "LOG")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return SCPE_2FARG;
                strncpy(logfiletmpl, cptr, sizeof(logfiletmpl)-1);
                continue;
                }
            if ((0 == MATCH_CMD (gbuf, "NOBUFFERED")) || 
                (0 == MATCH_CMD (gbuf, "UNBUFFERED"))) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return SCPE_2MARG;
                unbuffered = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "BUFFERED")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    strcpy(buffered, "32768");
                else {
                    i = (int32) get_uint (cptr, 10, 1024*1024, &r);
                    if ((r != SCPE_OK) || (i == 0))
                        return SCPE_ARG;
                    sprintf(buffered, "%d", i);
                    }
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "NOLOG")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return SCPE_2MARG;
                nolog = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "NOMODEM")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return SCPE_2MARG;
                modem_control = FALSE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "MODEM")) {
                if ((NULL != cptr) && ('\0' != *cptr))
                    return SCPE_2MARG;
                modem_control = TRUE;
                continue;
                }
            if (0 == MATCH_CMD (gbuf, "CONNECT")) {
                if ((NULL == cptr) || ('\0' == *cptr))
                    return SCPE_ARG;
                serport = sim_open_serial (cptr, NULL, &r);
                if (serport != INVALID_HANDLE) {
                    sim_close_serial (serport);
                    if (strchr (cptr, ';') && mp->modem_control)
                        return SCPE_ARG;
                    }
                else {
                    memset (hostport, '\0', sizeof(hostport));
                    strncpy (hostport, cptr, sizeof(hostport)-1);
                    if ((cptr = strchr (hostport, ';')))
                        *(cptr++) = '\0';
                    sock = sim_connect_sock (hostport, "localhost", NULL);
                    if (sock != INVALID_SOCKET)
                        sim_close_sock (sock, 0);
                    else
                        return SCPE_ARG;
                    if (cptr) {
                        get_glyph (cptr, cptr, 0);          /* upcase this string */
                        if (0 == MATCH_CMD (cptr, "NOTELNET"))
                            notelnet = TRUE;
                        else
                            if (0 == MATCH_CMD (cptr, "TELNET"))
                                notelnet = FALSE;
                            else
                                return SCPE_ARG;
                        }
                    cptr = hostport;
                    }
                strcpy(destination, cptr);
                continue;
                }
            cptr = get_glyph (gbuf, port, ';');
            if (SCPE_OK != sim_parse_addr (port, NULL, 0, NULL, NULL, 0, NULL, NULL))
                return SCPE_ARG;
            if (cptr) {
                get_glyph (cptr, cptr, 0);                  /* upcase this string */
                if (0 == MATCH_CMD (cptr, "NOTELNET"))
                    listennotelnet = TRUE;
                else
                    if (0 == MATCH_CMD (cptr, "TELNET"))
                        listennotelnet = FALSE;
                    else
                        return SCPE_ARG;
                }
            cptr = init_cptr;
            }
        cptr = get_glyph_nc (cptr, port, ';');
        sock = sim_master_sock (port, &r);                      /* make master socket */
        if (r != SCPE_OK)
            return r;
        if (sock == INVALID_SOCKET)                             /* open error */
            return SCPE_OPENERR;
        sim_close_sock (sock, 1);
        strcpy(listen, port);
        cptr = get_glyph (cptr, option, ';');
        if (option[0])
            if (0 == MATCH_CMD (option, "NOTELNET"))
                listennotelnet = TRUE;
            else
                if (0 == MATCH_CMD (option, "TELNET"))
                    listennotelnet = FALSE;
                else
                    return SCPE_ARG;
        }
    if (line == -1) {
        if (modem_control != mp->modem_control)
            return SCPE_ARG;
        if (logfiletmpl[0]) {
            strncpy(mp->logfiletmpl, logfiletmpl, sizeof(mp->logfiletmpl)-1);
            for (i = 0; i < mp->lines; i++) {
                lp = mp->ldsc + i;
                sim_close_logfile (&lp->txlogref);
                lp->txlog = NULL;
                lp->txlogname = (char *)realloc(lp->txlogname, CBUFSIZE);
                if (mp->lines > 1)
                    sprintf(lp->txlogname, "%s_%d", mp->logfiletmpl, i);
                else
                    strcpy(lp->txlogname, mp->logfiletmpl);
                r = sim_open_logfile (lp->txlogname, TRUE, &lp->txlog, &lp->txlogref);
                if (r == SCPE_OK)
                    setvbuf(lp->txlog, NULL, _IOFBF, 65536);
                else {
                    free (lp->txlogname);
                    lp->txlogname = NULL;
                    break;
                    }
                }
            }
        if (unbuffered) {
            if (mp->buffered) {
                mp->buffered = 0;
                for (i = 0; i < mp->lines; i++) { /* default line buffers */
                    lp = mp->ldsc + i;
                    lp->txbsz = TMXR_MAXBUF;
                    lp->txb = (char *)realloc(lp->txb, lp->txbsz);
                    lp->txbfd = lp->txbpi = lp->txbpr = 0;
                    }
                }
            }
        if (buffered[0]) {
            mp->buffered = atoi(buffered);
            for (i = 0; i < mp->lines; i++) { /* initialize line buffers */
                lp = mp->ldsc + i;
                lp->txbsz = mp->buffered;
                lp->txbfd = 1;
                lp->txb = (char *)realloc(lp->txb, lp->txbsz);
                lp->txbpi = lp->txbpr = 0;
                }
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
        if (listen[0]) {
            sock = sim_master_sock (listen, &r);            /* make master socket */
            if (r != SCPE_OK)
                return r;
            if (sock == INVALID_SOCKET)                     /* open error */
                return SCPE_OPENERR;
            if (mp->port) {                                 /* close prior listener */
                sim_close_sock (mp->master, 1);
                mp->master = 0;
                free (mp->port);
                mp->port = NULL;
                }
            printf ("Listening on port %s\n", listen);
            if (sim_log)
                fprintf (sim_log, "Listening on port %s\n", listen);
            mp->port = (char *)realloc (mp->port, 1 + strlen (listen));
            strcpy (mp->port, listen);                      /* save port */
            mp->master = sock;                              /* save master socket */
            mp->notelnet = listennotelnet;                  /* save desired telnet behavior flag */
            for (i = 0; i < mp->lines; i++) {               /* initialize lines */
                lp = mp->ldsc + i;
                lp->mp = mp;                                /* set the back pointer */

                if (lp->serport) {                          /* serial port attached? */
                    tmxr_reset_ln (lp);                     /* close current serial connection */
                    sim_control_serial (lp->serport, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);/* drop DTR and RTS */
                    sim_close_serial (lp->serport);
                    lp->serport = 0;
                    free (lp->serconfig);
                    lp->serconfig = NULL;
                    }
                tmxr_init_line (lp);                        /* initialize line state */
                lp->sock = 0;                               /* clear the socket */
                }
            }
        if (destination[0]) {
            if (mp->lines > 1)
                return SCPE_ARG;                            /* ambiguous */
            lp = &mp->ldsc[0];
            serport = sim_open_serial (destination, lp, &r);
            if (serport != INVALID_HANDLE) {
                _mux_detach_line (lp, TRUE, TRUE);
                if (lp->mp && lp->mp->master) {             /* if existing listener, close it */
                    sim_close_sock (lp->mp->master, 1);
                    lp->mp->master = 0;
                    free (lp->mp->port);
                    lp->mp->port = NULL;
                    }
                lp->destination = malloc(1+strlen(destination));
                strcpy (lp->destination, destination);
                lp->mp = mp;
                lp->serport = serport;
                lp->ser_connect_pending = TRUE;
                lp->notelnet = TRUE;
                tmxr_init_line (lp);                        /* init the line state */
                if (!lp->mp->modem_control)                 /* raise DTR and RTS for non modem control lines */
                    sim_control_serial (lp->serport, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                lp->cnms = sim_os_msec ();                  /* record time of connection */
                if (sim_switches & SWMASK ('V')) {          /* -V flag reports connection on port */
                    sim_os_ms_sleep (TMXR_DTR_DROP_TIME);
                    tmxr_report_connection (mp, lp);        /* report the connection to the line */
                    }
                }
            else {
                sock = sim_connect_sock (destination, "localhost", NULL);
                if (sock != INVALID_SOCKET) {
                    _mux_detach_line (lp, FALSE, TRUE);
                    lp->destination = malloc(1+strlen(destination));
                    strcpy (lp->destination, destination);
                    lp->mp = mp;
                    lp->connecting = sock;
                    lp->ipad = malloc (1 + strlen (lp->destination));
                    strcpy (lp->ipad, lp->destination);
                    lp->notelnet = notelnet;
                    lp->cnms = sim_os_msec ();              /* record time of connection */
                    tmxr_init_line (lp);                    /* init the line state */
                    return SCPE_OK;
                    }
                else
                    return SCPE_ARG;
                }
            }
        }
    else {                                                  /* line specific attach */
        lp = &mp->ldsc[line];
        lp->mp = mp;
        if (logfiletmpl[0]) {
            sim_close_logfile (&lp->txlogref);
            lp->txlog = NULL;
            lp->txlogname = (char *)realloc (lp->txlogname, 1 + strlen (logfiletmpl));
            strcpy(lp->txlogname, mp->logfiletmpl);
            r = sim_open_logfile (lp->txlogname, TRUE, &lp->txlog, &lp->txlogref);
            if (r == SCPE_OK)
                setvbuf(lp->txlog, NULL, _IOFBF, 65536);
            else {
                free (lp->txlogname);
                lp->txlogname = NULL;
                return r;
                }
            }
        if (unbuffered) {
            lp->txbsz = TMXR_MAXBUF;
            lp->txb = (char *)realloc (lp->txb, lp->txbsz);
            lp->txbfd = lp->txbpi = lp->txbpr = 0;
            }
        if (buffered[0]) {
            lp->txbsz = atoi(buffered);
            lp->txbfd = 1;
            lp->txb = (char *)realloc (lp->txb, lp->txbsz);
            lp->txbpi = lp->txbpr = 0;
            }
        if (nolog) {
            free(lp->txlogname);
            lp->txlogname = NULL;
            if (lp->txlog) {
                sim_close_logfile (&lp->txlogref);
                lp->txlog = NULL;
                }
            }
        if (listen[0]) {
            if ((mp->lines == 1) && (mp->master))           /* single line mux can have either line specific OR mux listener but NOT both */
                return SCPE_ARG;
            sock = sim_master_sock (listen, &r);            /* make master socket */
            if (r != SCPE_OK)
                return r;
            if (sock == INVALID_SOCKET)                     /* open error */
                return SCPE_OPENERR;
            _mux_detach_line (lp, TRUE, FALSE);
            printf ("Line %d Listening on port %s\n", line, listen);
            if (sim_log)
                fprintf (sim_log, "Line %d Listening on port %s\n", line, listen);
            lp->port = (char *)realloc (lp->port, 1 + strlen (listen));
            strcpy(lp->port, listen);                       /* save port */
            lp->master = sock;                              /* save master socket */
            if (listennotelnet != mp->notelnet)
                lp->notelnet = listennotelnet;
            else
                lp->notelnet = mp->notelnet;
            }
        if (destination[0]) {
            serport = sim_open_serial (destination, lp, &r);
            if (serport != INVALID_HANDLE) {
                _mux_detach_line (lp, TRUE, TRUE);
                lp->destination = malloc(1+strlen(destination));
                strcpy (lp->destination, destination);
                lp->serport = serport;
                lp->ser_connect_pending = TRUE;
                lp->notelnet = TRUE;
                tmxr_init_line (lp);                        /* init the line state */
                if (!lp->mp->modem_control)                 /* raise DTR and RTS for non modem control lines */
                    sim_control_serial (lp->serport, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                lp->cnms = sim_os_msec ();                  /* record time of connection */
                if (sim_switches & SWMASK ('V')) {          /* -V flag reports connection on port */
                    sim_os_ms_sleep (TMXR_DTR_DROP_TIME);
                    tmxr_report_connection (mp, lp);        /* report the connection to the line */
                    }
                }
            else {
                sock = sim_connect_sock (destination, "localhost", NULL);
                if (sock != INVALID_SOCKET) {
                    _mux_detach_line (lp, FALSE, TRUE);
                    lp->destination = malloc(1+strlen(destination));
                    strcpy (lp->destination, destination);
                    lp->connecting = sock;
                    lp->ipad = malloc (1 + strlen (lp->destination));
                    strcpy (lp->ipad, lp->destination);
                    lp->notelnet = notelnet;
                    lp->cnms = sim_os_msec ();              /* record time of connection */
                    tmxr_init_line (lp);                    /* init the line state */
                    }
                else
                    return SCPE_ARG;
                }
            }
        lp->modem_control = modem_control;
        r = SCPE_OK;
        }
    }
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

        Only devices which poll on a unit different from the unit provided
        at MUX attach time need call this function.  Calling this API is
        necessary for asynchronous multiplexer support and unnecessary 
        otherwise.

*/

t_stat tmxr_set_line_unit (TMXR *mp, int line, UNIT *uptr_poll)
{
if ((line < 0) || (line >= mp->lines))
    return SCPE_ARG;
mp->ldsc[line].uptr = uptr_poll;
return SCPE_OK;
}

/* Declare which unit polls for output 

   Inputs:
        *mp     =       the mux
        line    =       the line number
        *uptr_poll =    the unit which polls for output

   Outputs:
        none

   Implementation note:

        Only devices which poll on a unit different from the unit provided
        at MUX attach time need call this function ABD different from the
        unit which polls for input.  Calling this API is necessary for 
        asynchronous multiplexer support and unnecessary otherwise.

*/

t_stat tmxr_set_line_output_unit (TMXR *mp, int line, UNIT *uptr_poll)
{
if ((line < 0) || (line >= mp->lines))
    return SCPE_ARG;
mp->ldsc[line].o_uptr = uptr_poll;
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

tmxr_set_line_unit (&sim_con_tmxr, 0, rxuptr);
tmxr_set_line_output_unit (&sim_con_tmxr, 0, txuptr);
return SCPE_OK;
}


static TMXR **tmxr_open_devices = NULL;
static int tmxr_open_device_count = 0;

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_t           sim_tmxr_poll_thread;          /* Polling Thread Id */
#if defined(_WIN32) || defined(VMS)
pthread_t           sim_tmxr_serial_poll_thread;   /* Serial Polling Thread Id */
pthread_cond_t      sim_tmxr_serial_startup_cond;
#endif
pthread_mutex_t     sim_tmxr_poll_lock;
pthread_cond_t      sim_tmxr_poll_cond;
pthread_cond_t      sim_tmxr_startup_cond;
int32               sim_tmxr_poll_count = 0;
t_bool              sim_tmxr_poll_running = FALSE;

static void *
_tmxr_poll(void *arg)
{
int sched_policy;
struct sched_param sched_priority;
struct timeval timeout;
int timeout_usec;
DEVICE *dptr = tmxr_open_devices[0]->dptr;
UNIT **units = NULL;
UNIT **activated = NULL;
SOCKET *sockets = NULL;
int wait_count = 0;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_poll() - starting\n");

units = calloc(FD_SETSIZE, sizeof(*units));
activated = calloc(FD_SETSIZE, sizeof(*activated));
sockets = calloc(FD_SETSIZE, sizeof(*sockets));
timeout_usec = 1000000;
pthread_mutex_lock (&sim_tmxr_poll_lock);
pthread_cond_signal (&sim_tmxr_startup_cond);   /* Signal we're ready to go */
while (sim_asynch_enabled) {
    int i, j, status, select_errno;
    fd_set readfds, errorfds;
    int socket_count;
    SOCKET max_socket_fd;
    TMXR *mp;
    DEVICE *d;

    if ((tmxr_open_device_count == 0) || (!sim_is_running)) {
        for (j=0; j<wait_count; ++j) {
            d = find_dev_from_unit(activated[j]);
            sim_debug (TMXR_DBG_ASY, d, "_tmxr_poll() - Removing interest in %s. Other interest: %d\n", sim_uname(activated[j]), activated[j]->a_poll_waiter_count);
            --activated[j]->a_poll_waiter_count;
            --sim_tmxr_poll_count;
            }
        break;
        }
    /* If we started something we should wait for, let it finish before polling again */
    if (wait_count) {
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_poll() - waiting for %d units\n", wait_count);
        pthread_cond_wait (&sim_tmxr_poll_cond, &sim_tmxr_poll_lock);
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_poll() - continuing with timeout of %dms\n", timeout_usec/1000);
        }
    FD_ZERO (&readfds);
    FD_ZERO (&errorfds);
    for (i=max_socket_fd=socket_count=0; i<tmxr_open_device_count; ++i) {
        mp = tmxr_open_devices[i];
        if ((mp->master) && (mp->uptr->dynflags&UNIT_TM_POLL)) {
            units[socket_count] = mp->uptr;
            sockets[socket_count] = mp->master;
            FD_SET (mp->master, &readfds);
            FD_SET (mp->master, &errorfds);
            if (mp->master > max_socket_fd)
                max_socket_fd = mp->master;
            ++socket_count;
            }
        for (j=0; j<mp->lines; ++j) {
            if (mp->ldsc[j].sock) {
                units[socket_count] = mp->ldsc[j].uptr;
                if (units[socket_count] == NULL)
                    units[socket_count] = mp->uptr;
                sockets[socket_count] = mp->ldsc[j].sock;
                FD_SET (mp->ldsc[j].sock, &readfds);
                FD_SET (mp->ldsc[j].sock, &errorfds);
                if (mp->ldsc[j].sock > max_socket_fd)
                    max_socket_fd = mp->ldsc[j].sock;
                ++socket_count;
                }
#if !defined(_WIN32) && !defined(VMS)
            if (mp->ldsc[j].serport) {
                units[socket_count] = mp->ldsc[j].uptr;
                if (units[socket_count] == NULL)
                    units[socket_count] = mp->uptr;
                sockets[socket_count] = mp->ldsc[j].serport;
                FD_SET (mp->ldsc[j].serport, &readfds);
                FD_SET (mp->ldsc[j].serport, &errorfds);
                if (mp->ldsc[j].serport > max_socket_fd)
                    max_socket_fd = mp->ldsc[j].serport;
                ++socket_count;
                }
#endif
            if (mp->ldsc[j].connecting) {
                units[socket_count] = mp->uptr;
                sockets[socket_count] = mp->ldsc[j].connecting;
                FD_SET (mp->ldsc[j].connecting, &readfds);
                FD_SET (mp->ldsc[j].connecting, &errorfds);
                if (mp->ldsc[j].connecting > max_socket_fd)
                    max_socket_fd = mp->ldsc[j].connecting;
                ++socket_count;
                }
            if (mp->ldsc[j].master) {
                units[socket_count] = mp->uptr;
                sockets[socket_count] = mp->ldsc[j].master;
                FD_SET (mp->ldsc[j].master, &readfds);
                FD_SET (mp->ldsc[j].master, &errorfds);
                if (mp->ldsc[j].master > max_socket_fd)
                    max_socket_fd = mp->ldsc[j].master;
                ++socket_count;
                }
            }
        }
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    if (timeout_usec > 1000000)
        timeout_usec = 1000000;
    timeout.tv_sec = timeout_usec/1000000;
    timeout.tv_usec = timeout_usec%1000000;
    select_errno = 0;
    if (socket_count == 0) {
        sim_os_ms_sleep (timeout_usec/1000);
        status = 0;
        }
    else
        status = select (1+(int)max_socket_fd, &readfds, NULL, &errorfds, &timeout);
    select_errno = errno;
    wait_count=0;
    pthread_mutex_lock (&sim_tmxr_poll_lock);
    switch (status) {
        case 0:     /* timeout */
            for (i=max_socket_fd=socket_count=0; i<tmxr_open_device_count; ++i) {
                mp = tmxr_open_devices[i];
                if (mp->master) {
                    if (!mp->uptr->a_polling_now) {
                        mp->uptr->a_polling_now = TRUE;
                        mp->uptr->a_poll_waiter_count = 0;
                        d = find_dev_from_unit(mp->uptr);
                        sim_debug (TMXR_DBG_ASY, d, "_tmxr_poll() - Activating %s to poll connect\n", sim_uname(mp->uptr));
                        pthread_mutex_unlock (&sim_tmxr_poll_lock);
                        _sim_activate (mp->uptr, 0);
                        pthread_mutex_lock (&sim_tmxr_poll_lock);
                        }
                    if (mp->txcount) {
                        timeout_usec = 10000; /* Wait 10ms next time (this gets doubled below) */
                        mp->txcount = 0;
                        }
                    }
                for (j=0; j<mp->lines; ++j) {
                    if ((mp->ldsc[j].conn) && (mp->ldsc[j].uptr)) {
                        if (tmxr_tqln(&mp->ldsc[j]) || tmxr_rqln (&mp->ldsc[j])) {
                            timeout_usec = 10000; /* Wait 10ms next time (this gets doubled below) */
                            /* More than one socket can be associated with the 
                               same unit.  Make sure to only activate it one time */
                            if (!mp->ldsc[j].uptr->a_polling_now) {
                                mp->ldsc[j].uptr->a_polling_now = TRUE;
                                mp->ldsc[j].uptr->a_poll_waiter_count = 0;
                                d = find_dev_from_unit(mp->ldsc[j].uptr);
                                sim_debug (TMXR_DBG_ASY, d, "_tmxr_poll() - Line %d Activating %s to poll data: %d/%d\n", 
                                    j, sim_uname(mp->ldsc[j].uptr), tmxr_tqln(&mp->ldsc[j]), tmxr_rqln (&mp->ldsc[j]));
                                pthread_mutex_unlock (&sim_tmxr_poll_lock);
                                _sim_activate (mp->ldsc[j].uptr, 0);
                                pthread_mutex_lock (&sim_tmxr_poll_lock);
                                }
                            }
                        }
                    }
                }
            sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_poll() - Poll Timeout - %dms\n", timeout_usec/1000);
            timeout_usec *= 2;     /* Double timeout time */  
            break;
        case SOCKET_ERROR:
            wait_count = 0;
            if (select_errno == EINTR)
                break;
            fprintf (stderr, "select() returned -1, errno=%d - %s\r\n", select_errno, strerror(select_errno));
            abort();
            break;
        default:
            wait_count = 0;
            for (i=0; i<socket_count; ++i) {
                if (FD_ISSET(sockets[i], &readfds) || 
                    FD_ISSET(sockets[i], &errorfds)) {
                    /* More than one socket can be associated with the 
                       same unit.  Only activate one time */
                    for (j=0; j<wait_count; ++j)
                        if (activated[j] == units[i])
                            break;
                    if (j == wait_count) {
                        activated[j] = units[i];
                        ++wait_count;
                        if (!activated[j]->a_polling_now) {
                            activated[j]->a_polling_now = TRUE;
                            activated[j]->a_poll_waiter_count = 1;
                            d = find_dev_from_unit(activated[j]);
                            sim_debug (TMXR_DBG_ASY, d, "_tmxr_poll() - Activating for data %s\n", sim_uname(activated[j]));
                            pthread_mutex_unlock (&sim_tmxr_poll_lock);
                            _sim_activate (activated[j], 0);
                            pthread_mutex_lock (&sim_tmxr_poll_lock);
                            }
                        else {
                            d = find_dev_from_unit(activated[j]);
                            sim_debug (TMXR_DBG_ASY, d, "_tmxr_poll() - Already Activated %s%d %d times\n", sim_uname(activated[j]), activated[j]->a_poll_waiter_count);
                            ++activated[j]->a_poll_waiter_count;
                            }
                        }
                    }
                }
            if (wait_count)
                timeout_usec = 10000; /* Wait 10ms next time */
            break;
        }
    sim_tmxr_poll_count += wait_count;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);
free(units);
free(activated);
free(sockets);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_poll() - exiting\n");

return NULL;
}

#if defined(_WIN32)
static void *
_tmxr_serial_poll(void *arg)
{
int sched_policy;
struct sched_param sched_priority;
int timeout_usec;
DEVICE *dptr = tmxr_open_devices[0]->dptr;
UNIT **units = NULL;
UNIT **activated = NULL;
SERHANDLE *serports = NULL;
int wait_count = 0;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - starting\n");

units = calloc(MAXIMUM_WAIT_OBJECTS, sizeof(*units));
activated = calloc(MAXIMUM_WAIT_OBJECTS, sizeof(*activated));
serports = calloc(MAXIMUM_WAIT_OBJECTS, sizeof(*serports));
timeout_usec = 1000000;
pthread_mutex_lock (&sim_tmxr_poll_lock);
pthread_cond_signal (&sim_tmxr_serial_startup_cond);   /* Signal we're ready to go */
while (sim_asynch_enabled) {
    int i, j;
    DWORD status;
    int serport_count;
    TMXR *mp;
    DEVICE *d;

    if ((tmxr_open_device_count == 0) || (!sim_is_running)) {
        for (j=0; j<wait_count; ++j) {
            d = find_dev_from_unit(activated[j]);
            sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_poll() - Removing interest in %s. Other interest: %d\n", sim_uname(activated[j]), activated[j]->a_poll_waiter_count);
            --activated[j]->a_poll_waiter_count;
            --sim_tmxr_poll_count;
            }
        break;
        }
    /* If we started something we should wait for, let it finish before polling again */
    if (wait_count) {
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - waiting for %d units\n", wait_count);
        pthread_cond_wait (&sim_tmxr_poll_cond, &sim_tmxr_poll_lock);
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - continuing with timeout of %dms\n", timeout_usec/1000);
        }
    for (i=serport_count=0; i<tmxr_open_device_count; ++i) {
        mp = tmxr_open_devices[i];
        for (j=0; j<mp->lines; ++j) {
            if (mp->ldsc[j].serport) {
                units[serport_count] = mp->ldsc[j].uptr;
                if (units[serport_count] == NULL)
                    units[serport_count] = mp->uptr;
                serports[serport_count] = mp->ldsc[j].serport;
                ++serport_count;
                }
            }
        }
    if (serport_count == 0)                                 /* No open serial ports? */
        break;                                              /* We're done */
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    if (timeout_usec > 1000000)
        timeout_usec = 1000000;
    status = WaitForMultipleObjects (serport_count, serports, FALSE, timeout_usec/1000);
    wait_count=0;
    pthread_mutex_lock (&sim_tmxr_poll_lock);
    switch (status) {
        case WAIT_FAILED:
            fprintf (stderr, "WaitForMultipleObjects() Failed, LastError=%d\r\n", GetLastError());
            abort();
            break;
        case WAIT_TIMEOUT:
            sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - Poll Timeout - %dms\n", timeout_usec/1000);
            timeout_usec *= 2;     /* Double timeout time */  
            break;
        default:
            i = status - WAIT_OBJECT_0;
            wait_count = 0;
            j = wait_count;
            activated[j] = units[i];
            ++wait_count;
            if (!activated[j]->a_polling_now) {
                activated[j]->a_polling_now = TRUE;
                activated[j]->a_poll_waiter_count = 1;
                d = find_dev_from_unit(activated[j]);
                sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_poll() - Activating for data %s\n", sim_uname(activated[j]));
                pthread_mutex_unlock (&sim_tmxr_poll_lock);
                _sim_activate (activated[j], 0);
                pthread_mutex_lock (&sim_tmxr_poll_lock);
                }
            else {
                d = find_dev_from_unit(activated[j]);
                sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_poll() - Already Activated %s%d %d times\n", sim_uname(activated[j]), activated[j]->a_poll_waiter_count);
                ++activated[j]->a_poll_waiter_count;
                }
            if (wait_count)
                timeout_usec = 10000; /* Wait 10ms next time */
            break;
        }
    sim_tmxr_poll_count += wait_count;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);
free(units);
free(activated);
free(serports);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - exiting\n");

return NULL;
}
#endif /* _WIN32 */

#if defined(VMS)

#include <descrip.h>
#include <ttdef.h>
#include <tt2def.h>
#include <iodef.h>
#include <ssdef.h>
#include <starlet.h>
#include <unistd.h>

typedef struct {
    unsigned short status;
    unsigned short count;
    unsigned int dev_status; } IOSB;

#define MAXIMUM_WAIT_OBJECTS 64             /* Number of possible concurrently opened serial ports */

pthread_cond_t      sim_serial_line_startup_cond;


static void *
_tmxr_serial_line_poll(void *arg)
{
TMLN *lp = (TMLN *)arg;
int sched_policy;
struct sched_param sched_priority;
DEVICE *dptr = tmxr_open_devices[0]->dptr;
UNIT *uptr = (lp->uptr ? lp->uptr : lp->mp->uptr);
DEVICE *d = find_dev_from_unit(uptr);
int wait_count = 0;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_line_poll() - starting\n");

pthread_mutex_lock (&sim_tmxr_poll_lock);
pthread_cond_signal (&sim_serial_line_startup_cond);   /* Signal we're ready to go */
while (sim_asynch_enabled) {
    int i, j;
    int serport_count;
    TMXR *mp = lp->mp;
    unsigned int status, term[2];
    unsigned char buf[4];
    IOSB iosb;

    if ((tmxr_open_device_count == 0) || (!sim_is_running)) {
        if (wait_count) {
            sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_line_poll() - Removing interest in %s. Other interest: %d\n", sim_uname(uptr), uptr->a_poll_waiter_count);
            --uptr->a_poll_waiter_count;
            --sim_tmxr_poll_count;
            }
        break;
        }
    /* If we started something we should wait for, let it finish before polling again */
    if (wait_count) {
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_line_poll() - waiting for %d units\n", wait_count);
        pthread_cond_wait (&sim_tmxr_poll_cond, &sim_tmxr_poll_lock);
        sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_line_poll() - continuing with timeout of 1 sec\n");
        }
    lp->a_active = TRUE;
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    term[0] = term[1] = 0;
    status = sys$qiow (0, lp->serport, 
                       IO$_READLBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED | IO$M_TRMNOECHO,
                       &iosb, 0, 0, buf, 1, 1, term, 0, 0);
    if (status != SS$_NORMAL) {
        fprintf (stderr, "_tmxr_serial_line_poll() - QIO Failed, Status=%d\r\n", status);
        abort();
        }
    wait_count = 0;
    sys$synch (0, &iosb);
    pthread_mutex_lock (&sim_tmxr_poll_lock);
    lp->a_active = FALSE;
    if (iosb.count == 1) {
        lp->a_buffered_character = buf[0] | SCPE_KFLAG;
        wait_count = 1;
        if (!uptr->a_polling_now) {
            uptr->a_polling_now = TRUE;
            uptr->a_poll_waiter_count = 1;
            sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_line_poll() - Activating for data %s\n", sim_uname(uptr));
            pthread_mutex_unlock (&sim_tmxr_poll_lock);
            _sim_activate (uptr, 0);
            pthread_mutex_lock (&sim_tmxr_poll_lock);
            }
        else {
            sim_debug (TMXR_DBG_ASY, d, "_tmxr_serial_line_poll() - Already Activated %s%d %d times\n", sim_uname(uptr), uptr->a_poll_waiter_count);
            ++uptr->a_poll_waiter_count;
            }
        }
    sim_tmxr_poll_count += wait_count;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_line_poll() - exiting\n");

return NULL;
}

static void *
_tmxr_serial_poll(void *arg)
{
int sched_policy;
struct sched_param sched_priority;
int timeout_usec;
DEVICE *dptr = tmxr_open_devices[0]->dptr;
TMLN **lines = NULL;
pthread_t *threads = NULL;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor when 
   this thread needs to run */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - starting\n");

lines = calloc(MAXIMUM_WAIT_OBJECTS, sizeof(*lines));
threads = calloc(MAXIMUM_WAIT_OBJECTS, sizeof(*threads));
pthread_mutex_lock (&sim_tmxr_poll_lock);
pthread_cond_signal (&sim_tmxr_serial_startup_cond);   /* Signal we're ready to go */
pthread_cond_init (&sim_serial_line_startup_cond, NULL);
while (sim_asynch_enabled) {
    pthread_attr_t attr;
    int i, j;
    int serport_count;
    TMXR *mp;
    DEVICE *d;

    if ((tmxr_open_device_count == 0) || (!sim_is_running))
        break;
    pthread_attr_init (&attr);
    pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
    for (i=serport_count=0; i<tmxr_open_device_count; ++i) {
        mp = tmxr_open_devices[i];
        for (j=0; j<mp->lines; ++j) {
            if (mp->ldsc[j].serport) {
                lines[serport_count] = &mp->ldsc[j];
                pthread_create (&threads[serport_count], &attr, _tmxr_serial_line_poll, (void *)&mp->ldsc[j]);
                pthread_cond_wait (&sim_serial_line_startup_cond, &sim_tmxr_poll_lock); /* Wait for thread to stabilize */
                ++serport_count;
                }
            }
        }
    pthread_attr_destroy( &attr);
    if (serport_count == 0)                                 /* No open serial ports? */
        break;                                              /* We're done */
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    for (i=0; i<serport_count; i++)
        pthread_join (threads[i], NULL);
    pthread_mutex_lock (&sim_tmxr_poll_lock);
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);
pthread_cond_destroy (&sim_serial_line_startup_cond);
free(lines);
free(threads);

sim_debug (TMXR_DBG_ASY, dptr, "_tmxr_serial_poll() - exiting\n");

return NULL;
}
#endif /* VMS */

#endif /* defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX) */

t_stat tmxr_start_poll (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_lock (&sim_tmxr_poll_lock);
if ((tmxr_open_device_count > 0) && 
    sim_asynch_enabled           && 
    sim_is_running               && 
    !sim_tmxr_poll_running) {
    pthread_attr_t attr;

    pthread_cond_init (&sim_tmxr_startup_cond, NULL);
    pthread_attr_init (&attr);
    pthread_attr_setscope (&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create (&sim_tmxr_poll_thread, &attr, _tmxr_poll, NULL);
    pthread_attr_destroy( &attr);
    pthread_cond_wait (&sim_tmxr_startup_cond, &sim_tmxr_poll_lock); /* Wait for thread to stabilize */
    pthread_cond_destroy (&sim_tmxr_startup_cond);
    sim_tmxr_poll_running = TRUE;
    }
pthread_mutex_unlock (&sim_tmxr_poll_lock);
#endif
return SCPE_OK;
}

t_stat tmxr_stop_poll (void)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_lock (&sim_tmxr_poll_lock);
if (sim_tmxr_poll_running) {
    pthread_cond_signal (&sim_tmxr_poll_cond);
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
    pthread_join (sim_tmxr_poll_thread, NULL);
    sim_tmxr_poll_running = FALSE;
    /* Transitioning from asynch mode so kick all polling units onto the event queue */
    if (tmxr_open_device_count) {
        int i, j;

        for (i=0; i<tmxr_open_device_count; ++i) {
            TMXR *mp = tmxr_open_devices[i];

            if (mp->uptr)
                _sim_activate (mp->uptr, 0);
            for (j = 0; j < mp->lines; ++j)
                if (mp->ldsc[j].uptr)
                    _sim_activate (mp->ldsc[j].uptr, 0);
            }
        }
    }
else
    pthread_mutex_unlock (&sim_tmxr_poll_lock);
#endif
return SCPE_OK;
}

static void _tmxr_add_to_open_list (TMXR* mux)
{
int i;
t_bool found = FALSE;

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_lock (&sim_tmxr_poll_lock);
#endif
for (i=0; i<tmxr_open_device_count; ++i)
    if (tmxr_open_devices[i] == mux) {
        found = TRUE;
        break;
        }
if (!found) {
    tmxr_open_devices = (TMXR **)realloc(tmxr_open_devices, (tmxr_open_device_count+1)*sizeof(*tmxr_open_devices));
    tmxr_open_devices[tmxr_open_device_count++] = mux;
    }
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_unlock (&sim_tmxr_poll_lock);
if ((tmxr_open_device_count == 1) && (sim_asynch_enabled))
    tmxr_start_poll ();
#endif
}

static void _tmxr_remove_from_open_list (TMXR* mux)
{
int i, j;

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
tmxr_stop_poll ();
pthread_mutex_lock (&sim_tmxr_poll_lock);
#endif
for (i=0; i<tmxr_open_device_count; ++i)
    if (tmxr_open_devices[i] == mux) {
        for (j=i+1; j<tmxr_open_device_count; ++j)
            tmxr_open_devices[j-1] = tmxr_open_devices[j];
        --tmxr_open_device_count;
        break;
        }
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
pthread_mutex_unlock (&sim_tmxr_poll_lock);
#endif
}

t_stat tmxr_change_async (void)
{
#if defined(SIM_ASYNCH_IO)
if (sim_asynch_enabled)
    tmxr_start_poll ();
else
    tmxr_stop_poll ();
#endif
return SCPE_OK;
}


/* Attach unit to master socket */

t_stat tmxr_attach_ex (TMXR *mp, UNIT *uptr, char *cptr, t_bool async)
{
t_stat r;

r = tmxr_open_master (mp, cptr);                        /* open master socket */
if (r != SCPE_OK)                                       /* error? */
    return r;
mp->uptr = uptr;                                        /* save unit for polling */
uptr->filename = _mux_attach_string (uptr->filename, mp);/* save */
uptr->flags = uptr->flags | UNIT_ATT;                   /* no more errors */
if ((mp->lines > 1) ||
    ((mp->master == 0) &&
     (mp->ldsc[0].connecting == 0) &&
     (mp->ldsc[0].serport == 0)))
    uptr->dynflags = uptr->dynflags | UNIT_ATTMULT;     /* allow multiple attach commands */

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
if (!async || (uptr->flags & TMUF_NOASYNCH))            /* if asynch disabled */
    uptr->dynflags |= TMUF_NOASYNCH;                    /* tag as no asynch */
#else
uptr->dynflags |= TMUF_NOASYNCH;                        /* tag as no asynch */
#endif

if (mp->dptr == NULL)                                   /* has device been set? */
    mp->dptr = find_dev_from_unit (uptr);               /* no, so set device now */

_tmxr_add_to_open_list (mp);
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

t_stat tmxr_show_open_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc)
{
int i, j;

if (0 == tmxr_open_device_count)
    fprintf(st, "No Attached Multiplexer Devices\n");
else {
    for (i=0; i<tmxr_open_device_count; ++i) {
        TMXR *mp = tmxr_open_devices[i];
        TMLN *lp;

        fprintf(st, "Multiplexer device: %s", mp->dptr->name);
        fprintf(st, ", attached to %s, ", mp->uptr->filename);
        if (mp->lines > 1) {
            tmxr_show_lines(st, NULL, 0, mp);
            fprintf(st, ", ");
            }
        tmxr_show_summ(st, NULL, 0, mp);
        fprintf(st, ", sessions=%d", mp->sessions);
        if (mp->modem_control)
            fprintf(st, ", ModemControl=enabled");
        if (mp->notelnet)
            fprintf(st, ", Telnet=disabled");
        fprintf(st, "\n");
        for (j = 0; j < mp->lines; j++) {
            lp = mp->ldsc + j;
            if (mp->lines > 1) {
                fprintf (st, "Line: %d", j);
                if (mp->notelnet != lp->notelnet)
                    fprintf (st, " - %stelnet", lp->notelnet ? "no" : "");
                if (lp->uptr && (lp->uptr != lp->mp->uptr))
                    fprintf (st, " - Unit: %s", sim_uname (lp->uptr));
                if (mp->modem_control != lp->modem_control)
                    fprintf(st, ", ModemControl=%s", lp->modem_control ? "enabled" : "disabled");
                fprintf (st, "\n");
                }
            if ((!lp->sock) && (!lp->connecting) && (!lp->serport) && (!lp->master)) {
                if (lp->modem_control)
                    tmxr_fconns (st, lp, -1);
                continue;
                }
            tmxr_fconns (st, lp, -1);
            tmxr_fstats (st, lp, -1);
            }
        }
    }
return SCPE_OK;
}


/* Close a master listening socket.

   The listening socket associated with multiplexer descriptor "mp" is closed
   and deallocated.  In addition, all current Telnet sessions are disconnected.
   Serial and outgoing sessions are also disconnected.
*/

t_stat tmxr_close_master (TMXR *mp)
{
int32 i;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {  /* loop thru conn */
    lp = mp->ldsc + i;

    if (!lp->destination && lp->sock) {                 /* not serial and is connected? */
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
        free (lp->destination);
        lp->destination = NULL;
        if (lp->connecting) {
            lp->sock = lp->connecting;
            lp->connecting = 0;
            tmxr_reset_ln (lp);
            }
        lp->conn = FALSE;
        }
    if (lp->master) {
        sim_close_sock (lp->master, 1);                 /* close master socket */
        lp->master = 0;
        free (lp->port);
        lp->port = NULL;
        }
    free (lp->txb);
    lp->txb = NULL;
    lp->modembits = 0;
    }

if (mp->master)
    sim_close_sock (mp->master, 1);                     /* close master socket */
mp->master = 0;
free (mp->port);
mp->port = NULL;
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

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
tmxr_close_master (mp);                                 /* close master socket */
free (uptr->filename);                                  /* free setup string */
uptr->filename = NULL;
mp->last_poll_time = 0;
for (i=0; i < mp->lines; i++) {
    UNIT *uptr = mp->ldsc[i].uptr ? mp->ldsc[i].uptr : mp->uptr;
    UNIT *o_uptr = mp->ldsc[i].o_uptr ? mp->ldsc[i].o_uptr : mp->uptr;

    uptr->dynflags &= ~UNIT_TM_POLL;                    /* no polling */
    o_uptr->dynflags &= ~UNIT_TM_POLL;                  /* no polling */
    }
uptr->flags &= ~(UNIT_ATT);                             /* not attached */
uptr->dynflags &= ~(UNIT_TM_POLL|TMUF_NOASYNCH);        /* no polling, not asynch disabled  */
return SCPE_OK;
}


t_stat tmxr_activate (UNIT *uptr, int32 interval)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
if ((!(uptr->dynflags & UNIT_TM_POLL)) || 
    (!sim_asynch_enabled)) {
    return _sim_activate (uptr, interval);
    }
return SCPE_OK;
#else
return _sim_activate (uptr, interval);
#endif
}

t_stat tmxr_activate_after (UNIT *uptr, int32 usecs_walltime)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
if ((!(uptr->dynflags & UNIT_TM_POLL)) || 
    (!sim_asynch_enabled)) {
    return _sim_activate_after (uptr, usecs_walltime);
    }
return SCPE_OK;
#else
return _sim_activate_after (uptr, usecs_walltime);
#endif
}

t_stat tmxr_clock_coschedule (UNIT *uptr, int32 interval)
{
#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
if ((!(uptr->dynflags & UNIT_TM_POLL)) || 
    (!sim_asynch_enabled)) {
    return sim_clock_coschedule (uptr, interval);
    }
return SCPE_OK;
#else
return sim_clock_coschedule (uptr, interval);
#endif
}

/* Generic Multiplexer attach help */

t_stat tmxr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
TMXR *mux = (TMXR *)dptr->help_ctx;
t_bool single_line = FALSE;               /* default to Multi-Line help */

if (mux)
   single_line = (mux->lines == 1);

if (!flag)
    fprintf (st, "%s Multiplexer Attach Help\n\n", dptr->name);
if (single_line) {          /* Single Line Multiplexer */
    fprintf (st, "The %s multiplexer may be connected to terminal emulators supporting the\n", dptr->name);
    fprintf (st, "Telnet protocol via sockets, or to hardware terminals via host serial\n");
    fprintf (st, "ports.\n\n");
    if (mux->modem_control) {
        fprintf (st, "The %s device is a full modem control device and therefore is capable of\n", dptr->name);
        fprintf (st, "passing port configuration information and modem signals.\n");
        }
    fprintf (st, "A Telnet listening port can be configured with:\n\n");
    fprintf (st, "   sim> ATTACH %s {interface:}port\n\n", dptr->name);
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
    if (mux && mux->modem_control) {
        fprintf (st, "The %s device is a full modem control device and therefore is capable of\n", dptr->name);
        fprintf (st, "passing port configuration information and modem signals on all lines.\n");
        }
    fprintf (st, "Modem Control signalling behaviors can be enabled/disabled on a specific\n");
    fprintf (st, "multiplexer line with:\n\n");
    fprintf (st, "   sim> ATTACH %s Line=n,Modem\n", dptr->name);
    fprintf (st, "   sim> ATTACH %s Line=n,NoModem\n\n", dptr->name);
    fprintf (st, "A Telnet listening port can be configured with:\n\n");
    fprintf (st, "   sim> ATTACH %s {interface:}port\n\n", dptr->name);
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
    fprintf (st, "   sim> ATTACH %s Line=n,{interface:}port{;notelnet}\n\n", dptr->name);
    }
fprintf (st, "Direct computer to computer connections (Virutal Null Modem cables) may\n");
fprintf (st, "be established using the telnet protocol or via raw tcp sockets.\n\n");
fprintf (st, "   sim> ATTACH %s Line=n,Connect=host:port{;notelnet}\n\n", dptr->name);
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
            fprintf (st, "A line which is connected to a serial port can be manually closed by\n");
            fprintf (st, "adding the -C switch to a %s command.\n\n", mptr->mstring);
            fprintf (st, "   sim> SET -C %s %s=n\n\n", dptr->name, mptr->mstring);
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

void tmxr_msg (SOCKET sock, char *msg)
{
if ((sock) && (sock != INVALID_SOCKET))
    sim_write_sock (sock, msg, (int32)strlen (msg));
return;
}


/* Write a message to a line */

void tmxr_linemsg (TMLN *lp, char *msg)
{
int32 len;

for (len = (int32)strlen (msg); len > 0; --len)
    tmxr_putc_ln (lp, *msg++);
return;
}


/* Write a formatted message to a line */

void tmxr_linemsgf (TMLN *lp, const char *fmt, ...)
{
char stackbuf[STACKBUFSIZE];
int32 bufsize = sizeof(stackbuf);
char *buf = stackbuf;
va_list arglist;
int32 i, len;

buf[bufsize-1] = '\0';
while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
#if defined(NO_vsnprintf)
#if defined(HAS_vsprintf_void)

/* Note, this could blow beyond the buffer, and we couldn't tell */
/* That is a limitation of the C runtime library available on this platform */

    vsprintf (buf, fmt, arglist);
    for (len = 0; len < bufsize-1; len++)
        if (buf[len] == 0) break;
#else
    len = vsprintf (buf, fmt, arglist);
#endif                                                  /* HAS_vsprintf_void */
#else                                                   /* NO_vsnprintf */
#if defined(HAS_vsnprintf_void)
    vsnprintf (buf, bufsize-1, fmt, arglist);
    for (len = 0; len < bufsize-1; len++)
        if (buf[len] == 0) break;
#else
    len = vsnprintf (buf, bufsize-1, fmt, arglist);
#endif                                                  /* HAS_vsnprintf_void */
#endif                                                  /* NO_vsnprintf */
    va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || (len >= bufsize-1)) {
        if (buf != stackbuf)
            free (buf);
        bufsize = bufsize * 2;
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
    if ('\n' == buf[i]) {
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
return;
}


/* Print connections - used only in named SHOW command */

void tmxr_fconns (FILE *st, TMLN *lp, int32 ln)
{
int32 hr, mn, sc;
uint32 ctime;

if (ln >= 0)
    fprintf (st, "line %d: ", ln);

if ((lp->sock) || (lp->connecting)) {                   /* tcp connection? */
    if (lp->destination)                                /* remote connection? */
        fprintf (st, "Connection to remote port %s\n", lp->destination);/* print port name */
    else                                                /* incoming connection */
        fprintf (st, "Connection from IP address %s\n", lp->ipad);
    }
if (lp->sock) {
    char *sockname, *peername;

    sim_getnames_sock (lp->sock, &sockname, &peername);
    fprintf (st, "Connection %s->%s\n", sockname, peername);
    free (sockname);
    free (peername);
    }

if (lp->port)
    fprintf (st, "Listening on port %s\n", lp->port);   /* print port name */

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
    fprintf (st, " Line disconnected\n");

if (lp->modem_control) {
    fprintf (st, " Modem Bits: %s%s%s%s%s%s\n", (lp->modembits & TMXR_MDM_DTR) ? "DTR " : "",
                                                (lp->modembits & TMXR_MDM_RTS) ? "RTS " : "",
                                                (lp->modembits & TMXR_MDM_DCD) ? "DCD " : "",
                                                (lp->modembits & TMXR_MDM_RNG) ? "RNG " : "",
                                                (lp->modembits & TMXR_MDM_CTS) ? "CTS " : "",
                                                (lp->modembits & TMXR_MDM_DSR) ? "DSR " : "");
    }

if ((lp->serport == 0) && (lp->sock))
    fprintf (st, " %s\n", (lp->notelnet) ? "Telnet disabled (RAW data)" : "Telnet protocol");
if (lp->txlog)
    fprintf (st, " Logging to %s\n", lp->txlogname);
return;
}


/* Print statistics - used only in named SHOW command */

void tmxr_fstats (FILE *st, TMLN *lp, int32 ln)
{
static const char *enab = "on";
static const char *dsab = "off";

if (ln >= 0)
    fprintf (st, "Line %d:", ln);
if ((!lp->sock) && (!lp->connecting) && (!lp->serport))
    fprintf (st, " not connected\n");
else {
    if (ln >= 0)
        fprintf (st, "\n");
    fprintf (st, "  input (%s)", (lp->rcve? enab: dsab));
    if (lp->rxcnt)
        fprintf (st, " queued/total = %d/%d",
            tmxr_rqln (lp), lp->rxcnt);
    fprintf (st, "\n  output (%s)", (lp->xmte? enab: dsab));
    if (lp->txcnt || lp->txbpi)
        fprintf (st, " queued/total = %d/%d",
            tmxr_tqln (lp), lp->txcnt);
    fprintf (st, "\n");
    }
if (lp->txbfd)
    fprintf (st, "  output buffer size = %d\n", lp->txbsz);
if (lp->txcnt || lp->txbpi)
    fprintf (st, "  bytes in buffer = %d\n", 
               ((lp->txcnt > 0) && (lp->txcnt > lp->txbsz)) ? lp->txbsz : lp->txbpi);
if (lp->txdrp)
    fprintf (st, "  dropped = %d\n", lp->txdrp);
return;
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
   be raised again.  If the sim_switches -C flag is set, then a serial port 
   connection will be closed.

   Implementation notes:

    1. This function is usually called as an MTAB processing routine.
*/

t_stat tmxr_dscln (UNIT *uptr, int32 val, char *cptr, void *desc)
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
    if (!lp->notelnet)
        tmxr_linemsg (lp, "\r\nOperator disconnected line\r\n\n");/* report closure */
    tmxr_reset_ln_ex (lp, (sim_switches & SWMASK ('C')));       /* drop the line */
    }

return SCPE_OK;
}


/* Enable logging for line */

t_stat tmxr_set_log (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;

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
strncpy (lp->txlogname, cptr, CBUFSIZE);                /* save file name */
sim_open_logfile (cptr, TRUE, &lp->txlog, &lp->txlogref);/* open log */
if (lp->txlog == NULL) {                                /* error? */
    free (lp->txlogname);                               /* free buffer */
    return SCPE_OPENERR;
    }
lp->mp->uptr->filename = _mux_attach_string (lp->mp->uptr->filename, lp->mp);
return SCPE_OK;
}


/* Disable logging for line */

t_stat tmxr_set_nolog (UNIT *uptr, int32 val, char *cptr, void *desc)
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
lp->mp->uptr->filename = _mux_attach_string (lp->mp->uptr->filename, lp->mp);
return SCPE_OK;
}


/* Show logging status for line */

t_stat tmxr_show_log (FILE *st, UNIT *uptr, int32 val, void *desc)
{
TMXR *mp = (TMXR *) desc;
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

   Example command for eight-line multiplexer:

      SET <dev> LINEORDER=1;5;2-4;7

   Resulting connection order: 1,5,2,3,4,7,0,6.

   Parameters:
    - uptr = (not used)
    - val  = (not used)
    - cptr = pointer to first character of range specification
    - desc = pointer to multiplexer's TMXR structure

   On entry, cptr points to the value portion of the command string, which may
   be either a semicolon-separated list of line ranges or the keyword ALL.

   If a line connection order array is not defined in the multiplexer
   descriptor, the command is rejected.  If the specified range encompasses all
   of the lines, the first value of the connection order array is set to -1 to
   indicate sequential connection order.  Otherwise, the line values in the
   array are set to the order specified by the command string.  All values are
   populated, first with those explicitly specified in the command string, and
   then in ascending sequence with those not specified.

   If an error occurs, the original line order is not disturbed.
*/

t_stat tmxr_set_lnorder (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
char *tptr;
t_addr low, high, max = (t_addr) mp->lines - 1;
int32 *list;
t_bool *set;
uint32 line, idx = 0;
t_stat result = SCPE_OK;

if (mp->lnorder == NULL)                                /* line connection order undefined? */
    return SCPE_NXPAR;                                  /* "Non-existent parameter" error */

else if ((cptr == NULL) || (*cptr == '\0'))             /* line range not supplied? */
    return SCPE_MISVAL;                                 /* "Missing value" error */

list = (int32 *) calloc (mp->lines, sizeof (int32));    /* allocate new line order array */

if (list == NULL)                                       /* allocation failed? */
    return SCPE_MEM;                                    /* report it */

set = (t_bool *) calloc (mp->lines, sizeof (t_bool));   /* allocate line set tracking array */

if (set == NULL) {                                      /* allocation failed? */
    free (list);                                        /* free successful list allocation */
    return SCPE_MEM;                                    /* report it */
    }

tptr = cptr + strlen (cptr);                            /* append a semicolon */
*tptr++ = ';';                                          /*   to the command string */
*tptr = '\0';                                           /*   to make parsing easier for get_range */

while (*cptr) {                                                 /* parse command string */
    cptr = get_range (NULL, cptr, &low, &high, 10, max, ';');   /* get a line range */

    if (cptr == NULL) {                                 /* parsing error? */
        result = SCPE_ARG;                              /* "Invalid argument" error */
        break;
        }

    else if ((low > max) || (high > max)) {             /* line out of range? */
        result = SCPE_SUB;                              /* "Subscript out of range" error */
        break;
        }

    else if ((low == 0) && (high == max)) {             /* entire line range specified? */
        list [0] = -1;                                  /* set sequential order flag */
        idx = (uint32) max + 1;                         /* indicate no fill-in needed */
        break;
        }

    else
        for (line = (uint32) low; line <= (uint32) high; line++) /* see if previously specified */
            if (set [line] == FALSE) {                  /* not already specified? */
                set [line] = TRUE;                      /* now it is */
                list [idx] = line;                      /* add line to connection order */
                idx = idx + 1;                          /* bump "specified" count */
                }
    }

if (result == SCPE_OK) {                                /* assignment successful? */
    if (idx <= max)                                     /* any lines not specified? */
        for (line = 0; line <= max; line++)             /* fill them in sequentially */
            if (set [line] == FALSE) {                  /* specified? */
                list [idx] = line;                      /* no, so add it */
                idx = idx + 1;
                }

    memcpy (mp->lnorder, list, mp->lines * sizeof (int32)); /* copy working array to connection array */
    }

free (list);                                            /* free list allocation */
free (set);                                             /* free set allocation */

return result;
}


/* Show line connection order.

   Parameters:
    - st   = stream on which output is to be written
    - uptr = (not used)
    - val  = (not used)
    - desc = pointer to multiplexer's TMXR structure

   If a connection order array is not defined in the multiplexer descriptor, the
   command is rejected.  If the first value of the connection order array is set
   to -1, then the connection order is sequential.  Otherwise, the line values
   in the array are printed as a semicolon-separated list.  Ranges are printed
   where possible to shorten the output.
*/

t_stat tmxr_show_lnorder (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, j, low, last;
TMXR *mp = (TMXR *) desc;
int32 *iptr = mp->lnorder;
t_bool first = TRUE;

if (iptr == NULL)                                       /* connection order undefined? */
    return SCPE_NXPAR;                                  /* "Non-existent parameter" error */

if (*iptr < 0)                                          /* sequential order indicated? */
    fprintf (st, "Order=0-%d\n", mp->lines - 1);        /* print full line range */

else {
    low = last = *iptr++;                               /* set first line value */

    for (j = 1; j <= mp->lines; j++) {                  /* print remaining lines in order list */
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

t_stat tmxr_show_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
TMXR *mp = (TMXR *) desc;
int32 i, t;

if (mp == NULL)
    return SCPE_IERR;
for (i = t = 0; i < mp->lines; i++)
    if ((mp->ldsc[i].sock != 0) || (mp->ldsc[i].serport != 0))
        t = t + 1;
if (mp->lines > 1)
    fprintf (st, "%d connection%s", t, (t != 1) ? "s" : "");
else
    fprintf (st, "%s", (t == 1) ? "connected" : "disconnected");
return SCPE_OK;
}

/* Show conn/stat processor */

t_stat tmxr_show_cstat (FILE *st, UNIT *uptr, int32 val, void *desc)
{
TMXR *mp = (TMXR *) desc;
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

/* Show number of lines */

t_stat tmxr_show_lines (FILE *st, UNIT *uptr, int32 val, void *desc)
{
TMXR *mp = (TMXR *) desc;

if (mp == NULL)
    return SCPE_IERR;
fprintf (st, "lines=%d", mp->lines);
return SCPE_OK;
}


static struct {
    u_char value;
    char *name;
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
        {TN_NAOFFD, "TN_NAOFFD"},             /* Output Forfeed Disposition */
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
if ((lp->mp->dptr) && (dbits & lp->mp->dptr->dctrl)) {
    int i;

    tmxr_debug_buf_used = 0;
    if (tmxr_debug_buf)
        tmxr_debug_buf[tmxr_debug_buf_used] = '\0';

    if (!lp->notelnet) {
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
                    sim_debug (dbits, lp->mp->dptr, "Line:%d %04X thru %04X same as above\n", (int)(lp-lp->mp->ldsc), i-(16*same), i-1);
                else
                    sim_debug (dbits, lp->mp->dptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
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
                sim_debug (dbits, lp->mp->dptr, "Line:%d %04X%-48s %s\n", (int)(lp-lp->mp->ldsc), i, outbuf, strbuf);
            else
                sim_debug (dbits, lp->mp->dptr, "%04X%-48s %s\n", i, outbuf, strbuf);
            }
        if (same > 0) {
            if (lp->mp->lines > 1)
                sim_debug (dbits, lp->mp->dptr, "Line:%d %04X thru %04X same as above\n", (int)(lp-lp->mp->ldsc), i-(16*same), bufsize-1);
            else
                sim_debug (dbits, lp->mp->dptr, "%04X thru %04X same as above\n", i-(16*same), bufsize-1);
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
            sim_debug (dbits, lp->mp->dptr, "Line:%d %s %d bytes '%s'\n", (int)(lp-lp->mp->ldsc), msg, bufsize, tmxr_debug_buf);
        else
            sim_debug (dbits, lp->mp->dptr, "%s %d bytes '%s'\n", msg, bufsize, tmxr_debug_buf);
        }
    }
}
