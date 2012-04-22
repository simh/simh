/* sim_tmxr.c: Telnet terminal multiplexor library

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

   02-Jun-11    MP      Fixed telnet option negotiation loop with some clients
                        Added Option Negotiation and Debugging Support
   17-Jan-11    MP      Added Buffered line capabilities
   16-Jan-11    MP      Made option negotiation more reliable
   20-Nov-08    RMS     Added three new standardized SHOW routines
   05-Nov-08    JDB     [bugfix] Moved logging call after connection check in tmxr_putc_ln
   03-Nov-08    JDB     [bugfix] Added TMXR null check to tmxr_find_ldsc
   07-Oct-08    JDB     [serial] Added serial port support
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

   tmxr_poll_conn -             poll for connection
   tmxr_reset_ln -              reset line (drops Telnet connections only)
   tmxr_clear_ln -              clear line (drops Telnet and serial connections)
   tmxr_getc_ln -               get character for line
   tmxr_poll_rx -               poll receive
   tmxr_putc_ln -               put character for line
   tmxr_poll_tx -               poll transmit
   tmxr_send_buffered_data -    transmit buffered data
   tmxr_open_master -           open master connection
   tmxr_close_master -          close master connection
   tmxr_attach  -               attach terminal multiplexor to listening port
   tmxr_attach_line -           attach line to serial port
   tmxr_detach  -               detach terminal multiplexor to listening port
   tmxr_detach_line -           detach line from serial port
   tmxr_line_free -             return TRUE if line is disconnected
   tmxr_mux_free -              return TRUE if mux is disconnected
   tmxr_ex      -               (null) examine
   tmxr_dep     -               (null) deposit
   tmxr_msg     -               send message to socket
   tmxr_linemsg -               send message to line
   tmxr_fconns  -               output connection status
   tmxr_fstats  -               output connection statistics
   tmxr_set_log -               enable logging for line
   tmxr_set_nolog -             disable logging for line
   tmxr_show_log -              show logging status for line
   tmxr_dscln   -               disconnect line (SET routine)
   tmxr_rqln    -               number of available characters for line
   tmxr_tqln    -               number of buffered characters for line
   tmxr_set_lnorder -           set line connection order
   tmxr_show_lnorder -          show line connection order
   tmxr_show_summ -             show connection summary
   tmxr_show_cstat -            show line connections or status
   tmxr_show_lines -            show number of lines

   All routines are OS-independent.


   This library supports the simulation of multiple-line terminal multiplexers.
   It may also be used to create single-line "multiplexers" to provide
   additional terminals beyond the simulation console.  Multiplexer lines may be
   connected to terminal emulators supporting the Telnet protocol via sockets,
   or to hardware terminals via host serial ports.  Concurrent Telnet and serial
   connections may be mixed on a given multiplexer.

   When connecting via sockets, the simulated multiplexer is attached to a
   listening port on the host system:

     sim> attach MUX 23
     Listening on port 23 (socket nnn)

   Once attached, the listening port must be polled for incoming connections.
   When a connection attempt is received, it will be associated with the next
   multiplexer line in the user-specified line order, or with the next line in
   sequence if no order has been specified.  Individual lines may be forcibly
   disconnected either by:

     sim> set MUX2 disconnect

   or:

     sim> set MUX disconnect=2

   or the listening port and all Telnet sessions may be detached:

     sim> detach MUX


   When connecting via serial ports, individual multiplexer lines are attached
   to specific host ports using port names appropriate for the host system:

     sim> attach MUX2 com1      (or /dev/ttyS0)

   or:

     sim> set MUX connect=2:com1

   Serial port parameters may be optionally specified:

     sim> attach MUX2 com1;9600-8n1

   If the port parameters are omitted, then the host system defaults for the
   specified port are used.  The port is allocated during the attach call, but
   the actual connection is deferred until the multiplexer is polled for
   connections.

   Individual lines may be disconnected either with:

     sim> detach MUX2

   or:

     sim> set MUX2 disconnect

   or:

     sim> set MUX disconnect=2


   This library supports multiplexer device simulators that are modelled in
   three possible ways:

     1. as single-line devices (e.g., a second TTY)

     2. as multi-line devices with a unit per line and a separate scanner unit

     3. as multi-line devices with only a scanner unit

   Single-line devices may be attached either to a Telnet listening port or to a
   serial port.  The device attach routine may be passed either a port number or
   a serial port name.  This routine should call "tmxr_attach" first.  If the
   return value is SCPE_OK, then a port number was passed and was opened.  If
   the return value is SCPE_ARG, then a port number was not passed, and
   "tmxr_attach_line" should be called.  If that return value is SCPE_OK, then a
   serial port name was passed and was opened.  Otherwise, the attachment
   failed, and the returned status code value should be reported.

   The device detach routine should call "tmxr_detach_line" first, passing 0 for
   the "val" parameter.  If the return value is SCPE_OK, then the attached
   serial port was closed.  If the return value is SCPE_UNATT, then a serial
   port was not attached, and "tmxr_detach" should be called to close the Telnet
   listening port.  To maintain compatibility with earlier versions of this
   library, "tmxr_detach" always returns SCPE_OK, regardless of whether a
   listening port was attached.

   The system ATTACH and DETACH commands specify the device name, although unit
   0 is actually passed to the device attach and detach routines.  The in-use
   status of the multiplexer -- and therefore whether the multiplexer must be
   polled for input -- may be determined by checking whether the UNIT_ATT flag
   is present on unit 0.


   Multi-line devices with a unit per line and a separate scanner unit attach
   serial ports to the former and a Telnet listening port to the latter.  Both
   types of attachments may be made concurrently.  The system ATTACH and DETACH
   commands are used.

   The programmer may elect to use separate device attach routines for the lines
   and the scanner or a common attach routine for both.  In the latter case, if
   the scanner unit is passed, "tmxr_attach" should be called.  Otherwise,
   "tmxr_attach_line" should be called, passing 0 as the "val" parameter.

   Similarly, either separate or common detach routines may be used.  When a
   line detach is intended, the detach routine should call "tmxr_detach_line"
   for the specified unit.  Reception on the specified line should then be
   inhibited by clearing the "rcve" field.  Finally, "tmxr_mux_free" should be
   called to determine if the multiplexer is now free (listening port is
   detached and no other serial connections exist).  If it is, then the input
   poll may be stopped.

   To detach the scanner, the detach routine should call "tmxr_detach".  Then
   "tmxr_line_free" should be called for each line, and reception on the line
   should be inhibited if the routine returns TRUE.  Finally, the multiplexer
   poll should be stopped if the multiplexer is now free.

   The in-use status of the multiplexer cannot be determined solely by examining
   the UNIT_ATT flag of the scanner unit, as that reflects only Telnet
   connections.  Each line must also be checked for serial connections.  The
   "tmxr_line_free" and "tmxr_mux_free" routines indicate respectively whether a
   given line or the entire multiplexer is free.


   Multi-line devices with only a scanner unit use the system ATTACH and DETACH
   commands for the Telnet listening port.  For serial ports, SET <dev> CONNECT
   and SET <dev> DISCONNECT commands are used.  These latter commands are
   specified in the device MTAB structure and call "tmxr_attach_line" and
   "tmxr_detach_line", respectively.  Because MTAB processing passes the scanner
   unit to these routines, the invocations pass a non-zero "val" parameter to
   indicate that the unit should not be used, and that the line number should be
   parsed from the command string.  In this mode, "tmxr_detach_line" also serves
   to disconnect Telnet sessions from lines, so no special processing or calls
   to "tmxr_dscln" are required.

   In-use status of the multiplexer is determined in the same manner as the
   unit-per-line case.


   Implementation notes:

    1. The system RESTORE command does not restore devices having the DEV_NET
       flag.  This flag indicates that the device employs host-specific port
       names that are non-transportable across RESTOREs.

       If a multiplexer specifies DEV_NET, the device connection state will not
       be altered when a RESTORE is done.  That is, all current connections,
       including Telnet sessions, will remain untouched, and connections
       specified at the time of the SAVE will not be reestablished during the
       RESTORE.  If DEV_NET is not specified, then the system will attempt to
       restore the attachment state present at the time of the SAVE, including
       Telnet listening and serial ports.  Telnet client sessions on individual
       multiplexer lines cannot be reestablished by RESTORE and must be
       reestablished manually.

    2. Single-line multiplexers should have UNIT_ATTABLE on the unit
       representing the line, and multi-line unit-per-line multiplexers should
       not have UNIT_ATTABLE on the units representing the lines.  UNIT_ATTABLE
       does not affect the attachability when VM-specific attach routines are
       employed.  UNIT_ATTABLE does control the reporting of attached units for
       the SHOW <dev> command.

       A single-line device will be either detached, attached to a listening
       socket, or attached to a serial port.  With UNIT_ATTABLE, the device will
       be reported as "not attached," "attached to 23" (e.g.), or "attached to
       COM1" (e.g.), which is desirable.

       A unit-per-line device will report the listening socket as attached to
       the device (or to a separate device).  The units representing lines
       either will be connected to a Telnet session or attached to a serial
       port.  Telnet sessions are not reported by SHOW <dev>, so having
       UNIT_ATTABLE present will cause each non-serial line to be reported as
       "not attached," even if there may be a current Telnet connection.  This
       will be confusing to users.  Without UNIT_ATTABLE, attachment status will
       be reported only if the line is attached to a serial port, which is
       preferable.

    3. For devices without a unit per line, the MTAB entry that calls
       "sim_attach_line" (e.g., CONNECT) should use the MTAB_NC flag to avoid
       upper-casing the device name.  Device names may be case-sensitive,
       depending on the host system.
*/


#include <ctype.h>

#include "sim_defs.h"
#include "sim_serial.h"
#include "sim_sock.h"
#include "sim_timer.h"
#include "sim_tmxr.h"
#include "scp.h"

/* Telnet protocol constants - negatives are for init'ing signed char data */

/* Commands */
#define TN_IAC          -1                              /* protocol delim */
#define TN_DONT         -2                              /* dont */
#define TN_DO           -3                              /* do */
#define TN_WONT         -4                              /* wont */
#define TN_WILL         -5                              /* will */
#define TN_SB           -6                              /* sub-option negotiation */
#define TN_GA           -7                              /* go ahead */
#define TN_EL           -8                              /* erase line */
#define TN_EC           -9                              /* erase character */
#define TN_AYT          -10                             /* are you there */
#define TN_AO           -11                             /* abort output */
#define TN_IP           -12                             /* interrupt process */
#define TN_BRK          -13                             /* break */
#define TN_DATAMK       -14                             /* data mark */
#define TN_NOP          -15                             /* no operation */
#define TN_SE           -16                             /* end sub-option negot */

/* Options */

#define TN_BIN          0                               /* bin */
#define TN_ECHO         1                               /* echo */
#define TN_SGA          3                               /* sga */
#define TN_LINE         34                              /* line mode */
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



/* External variables */

extern int32 sim_switches;
extern char sim_name[];
extern FILE *sim_log;



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
memset (lp->rbr, 0, TMXR_MAXBUF);                       /* clear break status array */
lp->txdrp = 0;
if (!lp->mp->buffered) {
    lp->txbfd = 0;
    lp->txbsz = TMXR_MAXBUF;
    lp->txb = (char *)realloc(lp->txb, lp->txbsz);
    }
return;
}


/* Report a connection to a line.

   A notification of the form:

      Connected to the <sim> simulator <dev> device, line <n>

   is sent to the newly connected line.  If the device has only one line, the
   "line <n>" part is omitted.  If the device has not been defined, the "<dev>
   device" part is omitted.
*/

static void tmxr_report_connection (TMXR *mp, TMLN *lp, int32 i)
{
int32 written, psave;
char cmsg[80];
char dmsg[80] = "";
char lmsg[80] = "";
char msgbuf[256];

sprintf (cmsg, "\n\r\nConnected to the %s simulator ", sim_name);

if (mp->dptr) {                                         /* device defined? */
    sprintf (dmsg, "%s device",                         /* report device name */
                   sim_dname (mp->dptr));

    if (mp->lines > 1)                                  /* more than one line? */
        sprintf (lmsg, ", line %d", i);                 /* report the line number */
    }

sprintf (msgbuf, "%s%s%s\r\n\n", cmsg, dmsg, lmsg);

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

written = tmxr_send_buffered_data (lp);                 /* send the message */

if (written == 0)                                       /* buffer now empty? */
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
tmxr_linemsg (lp, "\r\nDisconnected from the ");        /* report disconnection */
tmxr_linemsg (lp, sim_name);
tmxr_linemsg (lp, " simulator\r\n\n");
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
    return sim_read_sock (lp->conn, &(lp->rxb[i]), length);
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
    written = sim_write_sock (lp->conn, &(lp->txb[i]), length);

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
    if (dptr == NULL) return NULL;                      /* what?? */
    val = (int32) (uptr - dptr->units);                 /* implicit line # */
    }
if ((val < 0) || (val >= mp->lines)) return NULL;       /* invalid line? */
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



/* Global routines */


/* Poll for new connection

   Called from unit service routine to test for new connection

   Inputs:
        *mp     =       pointer to terminal multiplexor descriptor
   Outputs:
        line number activated, -1 if none

   If a connection order is defined for the descriptor, and the first value is
   not -1 (indicating default order), then the order array is used to find an
   open line.  Otherwise, a search is made of all lines in numerical sequence.

   Implementation notes:

    1. When a serial port is attached to a line, the connection is made pending
       until we are called to poll for new connections.  This is because
       multiplexer service routines recognize new connections only as a result
       of calls to this routine.

    2. A pending serial (re)connection may also be deferred.  This is needed
       when a line clear drops DTR, as DTR must remain low for a period of time
       in order to be recognized by the serial device.  If the "cnms" value
       specifies a time in the future, the connection is deferred until that
       time is reached.  This leaves DTR low for the necessary time.
*/

int32 tmxr_poll_conn (TMXR *mp)
{
SOCKET newsock;
TMLN *lp;
int32 *op;
int32 i, j;
uint32 ipaddr, current_time;
t_bool deferrals;
static char mantra[] = {
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

/* Check for a pending serial connection */

if (mp->pending) {                                      /* is there a pending serial connection? */
    current_time = sim_os_msec ();                      /* get the current time */
    deferrals = FALSE;                                  /* assume no deferrals */

    for (i = 0; i < mp->lines; i++) {                   /* check each line in sequence */
        lp = mp->ldsc + i;                              /* get pointer to line descriptor */

        if ((lp->serport != 0) && (lp->conn == 0))      /* have handle but no connection? */
            if (current_time < lp->cnms)                /* time to connect hasn't arrived? */
                deferrals = TRUE;                       /* note the deferral */

            else {                                      /* line is ready to connect */
                tmxr_init_line (lp);                    /* init the line state */
                sim_control_serial (lp->serport, TRUE); /* connect line by raising DTR */
                lp->conn = 1;                           /* mark as connected */
                lp->cnms = current_time;                /* record time of connection */
                tmxr_report_connection (mp, lp, i);     /* report the connection to the line */
                mp->pending = mp->pending - 1;          /* drop the pending count */
                return i;                               /* return the line number */
                }
        }

    if (deferrals == FALSE)                             /* any deferred connections? */
        mp->pending = 0;                                /* no, and none pending, so correct count */
    }

/* Check for a pending Telnet connection */

newsock = sim_accept_conn (mp->master, &ipaddr);        /* poll connect */

if (newsock != INVALID_SOCKET) {                        /* got a live one? */
    op = mp->lnorder;                                   /* get line connection order list pointer */
    i = mp->lines;                                      /* play it safe in case lines == 0 */

    for (j = 0; j < mp->lines; j++, i++) {              /* find next avail line */
        if (op && (*op >= 0) && (*op < mp->lines))      /* order list present and valid? */
            i = *op++;                                  /* get next line in list to try */
        else                                            /* no list or not used or range error */
            i = j;                                      /* get next sequential line */

        lp = mp->ldsc + i;                              /* get pointer to line descriptor */
        if (lp->conn == 0)                              /* is the line available? */
            break;                                      /* yes, so stop search */
        }

    if (i >= mp->lines) {                               /* all busy? */
        tmxr_msg (newsock, "All connections busy\r\n");
        sim_close_sock (newsock, 0);
        }
    else {
        lp = mp->ldsc + i;                              /* get line desc */
        tmxr_init_line (lp);                            /* init line */
        lp->conn = newsock;                             /* record connection */
        lp->ipad = ipaddr;                              /* ip address */
        sim_write_sock (newsock, mantra, sizeof(mantra));
        tmxr_debug (TMXR_DBG_XMT, lp, "Sending", mantra, sizeof(mantra));
        tmxr_report_connection (mp, lp, i);
        lp->cnms = sim_os_msec ();                      /* time of conn */
        return i;
        }
    }                                                   /* end if newsock */
return -1;                                              /* no new connections made */
}

/* Reset a line.

   A Telnet session associated with line descriptor "lp" is disconnected, and
   the socket is deallocated.  If the line has a serial connection instead, then
   no action is taken.

   This routine is provided for backward compatibility.  Use "tmxr_clear_ln" in
   new code to disconnect both Telnet and serial connections.
*/

void tmxr_reset_ln (TMLN *lp)
{
if (lp->txlog)                                          /* dump log */
    fflush (lp->txlog);
tmxr_send_buffered_data (lp);                           /* send buffered data */

if (!lp->serport) {                                     /* Telnet connection? */
    sim_close_sock (lp->conn, 0);                       /* close socket */
    tmxr_init_line (lp);                                /* initialize line state */
    lp->conn = 0;                                       /* remove socket */
    }
return;
}

/* Clear a line connection.

   The Telnet or serial session associated with multiplexer descriptor "mp" and
   line descriptor "lp" is disconnected.  An associated Telnet socket is
   deallocated; a serial port is not, although DTR is dropped to disconnect the
   attached serial device.  Serial lines will be scheduled for reconnection
   after a short delay for DTR recognition.
*/

t_stat tmxr_clear_ln (TMXR *mp, TMLN *lp)
{
if ((mp == NULL) || (lp == NULL))                       /* no multiplexer or line descriptors? */
    return SCPE_IERR;                                   /* programming error! */

if (lp->txlog)                                          /* logging? */
    fflush (lp->txlog);                                 /* flush log */

tmxr_send_buffered_data (lp);                           /* send any buffered data */

if (lp->serport) {                                      /* serial connection? */
    sim_control_serial (lp->serport, FALSE);            /* disconnect line by dropping DTR */
    lp->cnms = sim_os_msec () + 500;                    /* reconnect 500 msec from now */
    mp->pending = mp->pending + 1;                      /* mark line reconnection as pending */
    }
else                                                    /* Telnet connection */
    sim_close_sock (lp->conn, 0);                       /* close socket */

tmxr_init_line (lp);                                    /* initialize line state */
lp->conn = 0;                                           /* remove socket or connection flag */
return SCPE_OK;
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
        *mp     =       pointer to terminal multiplexor descriptor
   Outputs:     none
*/

void tmxr_poll_rx (TMXR *mp)
{
int32 i, nbytes, j;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (!lp->conn || !lp->rcve)                         /* skip if !conn */
        continue;

    nbytes = 0;
    if (lp->rxbpi == 0)                                 /* need input? */
        nbytes = tmxr_read (lp,                         /* yes, read */
            TMXR_MAXBUF - TMXR_GUARD);                  /* leave spc for Telnet cruft */
    else if (lp->tsta)                                  /* in Telnet seq? */
        nbytes = tmxr_read (lp,                         /* yes, read to end */
            TMXR_MAXBUF - lp->rxbpi);

    if (nbytes < 0)                                     /* line error? */
        tmxr_clear_ln (mp, lp);                         /* disconnect line */

    else if (nbytes > 0) {                              /* if data rcvd */

        tmxr_debug (TMXR_DBG_RCV, lp, "Received", &(lp->rxb[lp->rxbpi]), nbytes);

        j = lp->rxbpi;                                  /* start of data */
        lp->rxbpi = lp->rxbpi + nbytes;                 /* adv pointers */
        lp->rxcnt = lp->rxcnt + nbytes;

        if (lp->serport)                                /* is this a serial reception? */
            continue;                                   /* yes, so no further processing needed */

/* Examine new data, remove TELNET cruft before making input available */

        for (; j < lp->rxbpi; ) {                       /* loop thru char */
            signed char tmp = lp->rxb[j];               /* get char */
            switch (lp->tsta) {                         /* case tlnt state */

            case TNS_NORM:                              /* normal */
                if (tmp == TN_IAC) {                    /* IAC? */
                    lp->tsta = TNS_IAC;                 /* change state */
                    tmxr_rmvrc (lp, j);                 /* remove char */
                    break;
                    }
                if ((tmp == TN_CR) && lp->dstb)         /* CR, no bin */
                    lp->tsta = TNS_CRPAD;               /* skip pad char */
                j = j + 1;                              /* advance j */
                break;

            case TNS_IAC:                               /* IAC prev */
                if (tmp == TN_IAC) {                    /* IAC + IAC */
                    lp->tsta = TNS_NORM;                /* treat as normal */
                    j = j + 1;                          /* advance j */
                    break;                              /* keep IAC */
                    }
                if (tmp == TN_BRK) {                    /* IAC + BRK? */
                    lp->tsta = TNS_NORM;                /* treat as normal */
                    lp->rxb[j] = 0;                     /* char is null */
                    lp->rbr[j] = 1;                     /* flag break */
                    j = j + 1;                          /* advance j */
                    break;
                    }
                switch (tmp) {
                case TN_WILL:                           /* IAC + WILL? */
                    lp->tsta = TNS_WILL;
                    break;
                case TN_WONT:                           /* IAC + WONT? */
                    lp->tsta = TNS_WONT;
                    break;
                case TN_DO:                             /* IAC + DO? */
                    lp->tsta = TNS_DO;
                    break;
                case TN_DONT:                           /* IAC + DONT? */
                    lp->tsta = TNS_SKIP;                /* IAC + other */
                    break;
                case TN_GA: case TN_EL:                 /* IAC + other 2 byte types */
                case TN_EC: case TN_AYT:                
                case TN_AO: case TN_IP:
                case TN_NOP: 
                    lp->tsta = TNS_NORM;                /* ignore */
                    break;
                case TN_SB:                             /* IAC + SB sub-opt negotiation */
                case TN_DATAMK:                         /* IAC + data mark */
                case TN_SE:                             /* IAC + SE sub-opt end */
                    lp->tsta = TNS_NORM;                /* ignore */
                    break;
                    }
                tmxr_rmvrc (lp, j);                     /* remove char */
                break;

            case TNS_WILL: case TNS_WONT:               /* IAC+WILL/WONT prev */
                if (tmp == TN_BIN) {                    /* BIN? */
                    if (lp->tsta == TNS_WILL)
                        lp->dstb = 0;
                    else lp->dstb = 1;
                    }
                tmxr_rmvrc (lp, j);                     /* remove it */
                lp->tsta = TNS_NORM;                    /* next normal */
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

            case TNS_CRPAD:                             /* only LF or NUL should follow CR */
                lp->tsta = TNS_NORM;                    /* next normal */
                if ((tmp == TN_LF) ||                   /* CR + LF ? */
                    (tmp == TN_NUL))                    /* CR + NUL? */
                    tmxr_rmvrc (lp, j);                 /* remove it */
                break;

            case TNS_DO:                                /* pending DO request */
            case TNS_SKIP: default:                     /* skip char */
                tmxr_rmvrc (lp, j);                     /* remove char */
                lp->tsta = TNS_NORM;                    /* next normal */
                break;
                }                                       /* end case state */
            }                                           /* end for char */
            if (nbytes != (lp->rxbpi-lp->rxbpr))
                tmxr_debug (TMXR_DBG_RCV, lp, "Remaining", &(lp->rxb[lp->rxbpi]), lp->rxbpi-lp->rxbpr);
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
        chr     =       characters
   Outputs:
        status  =       ok, connection lost, or stall

   Implementation note:

    1. If the line is not connected, SCPE_LOST is returned.  For serial
       connections, this may also occur when the connection is pending, either
       before the first "tmxr_poll_conn" call, or during a DTR drop deferral.
*/

t_stat tmxr_putc_ln (TMLN *lp, int32 chr)
{
if (lp->txlog)                                          /* log if available */
    fputc (chr, lp->txlog);
if ((lp->conn == 0) && (!lp->txbfd))                    /* no conn & not buffered? */
    if (lp->txlog)                                      /* if it was logged, we got it */           
        return SCPE_OK;
    else {
        ++lp->txdrp;                                    /* lost */
        return SCPE_LOST;
        }
#define TXBUF_AVAIL(lp) (lp->txbsz - tmxr_tqln (lp))
#define TXBUF_CHAR(lp, c) {                               \
    lp->txb[lp->txbpi++] = (char)(c);                     \
    lp->txbpi %= lp->txbsz;                               \
    if (lp->txbpi == lp->txbpr)                           \
        lp->txbpr = (1+lp->txbpr)%lp->txbsz, ++lp->txdrp; \
    }
if ((lp->txbfd) || (TXBUF_AVAIL(lp) > 1)) {             /* room for char (+ IAC)? */
    if (TN_IAC == (char) chr)                           /* char == IAC ? */
        TXBUF_CHAR (lp, TN_IAC);                        /* stuff extra IAC char */
    TXBUF_CHAR (lp, chr);                               /* buffer char & adv pointer */
    if ((!lp->txbfd) && (TXBUF_AVAIL (lp) <= TMXR_GUARD))/* near full? */
        lp->xmte = 0;                                   /* disable line */
    return SCPE_OK;                                     /* char sent */
    }
++lp->txdrp; lp->xmte = 0;                              /* no room, dsbl line */
return SCPE_STALL;                                      /* char not sent */
}


/* Poll for output

   Inputs:
        *mp     =       pointer to terminal multiplexor descriptor
   Outputs:
        none
*/

void tmxr_poll_tx (TMXR *mp)
{
int32 i, nbytes;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {                       /* loop thru lines */
    lp = mp->ldsc + i;                                  /* get line desc */
    if (lp->conn == 0)                                  /* skip if !conn */
        continue;
        nbytes = tmxr_send_buffered_data (lp);          /* buffered bytes */
        if (nbytes == 0)                                /* buf empty? enab line */
            lp->xmte = 1;
        }                                               /* end for */
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

nbytes = tmxr_tqln(lp);                                 /* avail bytes */
if (nbytes) {                                           /* >0? write */
    if (lp->txbpr < lp->txbpi)                          /* no wrap? */
        sbytes = tmxr_write (lp, nbytes);               /* write all data */
    else
        sbytes = tmxr_write (lp, lp->txbsz - lp->txbpr);/* write to end buf */

    if (sbytes > 0) {                                   /* ok? */
        tmxr_debug (TMXR_DBG_XMT, lp, "Sent", &(lp->txb[lp->txbpr]), sbytes);
        lp->txbpr = (lp->txbpr + sbytes);               /* update remove ptr */
        if (lp->txbpr >= lp->txbsz)                     /* wrap? */
            lp->txbpr = 0;
        lp->txcnt = lp->txcnt + sbytes;                 /* update counts */
        nbytes = nbytes - sbytes;
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


/* Open a master listening socket.

   A listening socket for the port number described by "cptr" is opened for the
   multiplexer associated with descriptor "mp".  If the open is successful, all
   lines not currently possessing serial connections are initialized for Telnet
   connections.
*/

t_stat tmxr_open_master (TMXR *mp, char *cptr)
{
int32 i, port;
SOCKET sock;
TMLN *lp;
t_stat r;

if (!isdigit(*cptr)) {
    char gbuf[CBUFSIZE];
    cptr = get_glyph (cptr, gbuf, '=');
    if (0 == MATCH_CMD (gbuf, "LOG")) {
        if ((NULL == cptr) || ('\0' == *cptr))
            return SCPE_2FARG;
        strncpy(mp->logfiletmpl, cptr, sizeof(mp->logfiletmpl)-1);
        for (i = 0; i < mp->lines; i++) {
            lp = mp->ldsc + i;
            sim_close_logfile (&lp->txlogref);
            lp->txlog = NULL;
            lp->txlogname = realloc(lp->txlogname, CBUFSIZE);
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
        return r;
        }
    if ((0 == MATCH_CMD (gbuf, "NOBUFFERED")) || 
        (0 == MATCH_CMD (gbuf, "UNBUFFERED"))) {
        if (mp->buffered) {
            mp->buffered = 0;
            for (i = 0; i < mp->lines; i++) { /* default line buffers */
                lp = mp->ldsc + i;
                lp->txbsz = TMXR_MAXBUF;
                lp->txb = (char *)realloc(lp->txb, lp->txbsz);
                lp->txbfd = lp->txbpi = lp->txbpr = 0;
                }
            }
        return SCPE_OK;
        }
    if (0 == MATCH_CMD (gbuf, "BUFFERED")) {
        if ((NULL == cptr) || ('\0' == *cptr))
            mp->buffered = 32768;
        else {
            i = (int32) get_uint (cptr, 10, 1024*1024, &r);
            if ((r != SCPE_OK) || (i == 0))
                return SCPE_ARG;
            mp->buffered = i;
            }
        for (i = 0; i < mp->lines; i++) { /* initialize line buffers */
            lp = mp->ldsc + i;
            lp->txbsz = mp->buffered;
            lp->txbfd = 1;
            lp->txb = (char *)realloc(lp->txb, lp->txbsz);
            lp->txbpi = lp->txbpr = 0;
            }
        return SCPE_OK;
        }
    if (0 == MATCH_CMD (gbuf, "NOLOG")) {
        if ((NULL != cptr) && ('\0' != *cptr))
            return SCPE_2MARG;
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
        return SCPE_OK;
        }
    return SCPE_ARG;
    }
port = (int32) get_uint (cptr, 10, 65535, &r);          /* get port */
if ((r != SCPE_OK) || (port == 0))
    return SCPE_ARG;
sock = sim_master_sock (port);                          /* make master socket */
if (sock == INVALID_SOCKET)                             /* open error */
    return SCPE_OPENERR;
printf ("Listening on port %d (socket %d)\n", port, sock);
if (sim_log)
    fprintf (sim_log, "Listening on port %d (socket %d)\n", port, sock);
mp->port = port;                                        /* save port */
mp->master = sock;                                      /* save master socket */
for (i = 0; i < mp->lines; i++) {                       /* initialize lines */
    lp = mp->ldsc + i;

    if (lp->serport == 0) {                             /* no serial port attached? */
        lp->mp = mp;                                    /* set the back pointer */
        tmxr_init_line (lp);                            /* initialize line state */
        lp->conn = 0;                                   /* clear the socket */
        }
    }
return SCPE_OK;
}

/* Attach unit to master socket */

t_stat tmxr_attach (TMXR *mp, UNIT *uptr, char *cptr)
{
char* tptr;
t_stat r;
char pmsg[20], bmsg[32] = "", lmsg[64+PATH_MAX] = "";

tptr = (char *) malloc (strlen (cptr) +                 /* get string buf */
                        sizeof(pmsg) + 
                        sizeof(bmsg) + sizeof(lmsg));
if (tptr == NULL)                                       /* no more mem? */
    return SCPE_MEM;
r = tmxr_open_master (mp, cptr);                        /* open master socket */
if (r != SCPE_OK) {                                     /* error? */
    free (tptr);                                        /* release buf */
    return r;
    }
sprintf (pmsg, "%d", mp->port);                         /* copy port */
if (mp->buffered)
    sprintf (bmsg, ", buffered=%d", mp->buffered);      /* buffer info */
if (mp->logfiletmpl[0])
    sprintf (lmsg, ", log=%s", mp->logfiletmpl);        /* logfile info */
sprintf (tptr, "%s%s%s", pmsg, bmsg, lmsg);             /* assemble all */
uptr->filename = tptr;                                  /* save */
uptr->flags = uptr->flags | UNIT_ATT;                   /* no more errors */

if (mp->dptr == NULL)                                   /* has device been set? */
    mp->dptr = find_dev_from_unit (uptr);               /* no, so set device now */

return SCPE_OK;
}


/* Attach a line to a serial port.

   Attach a line of the multiplexer associated with descriptor "desc" to a
   serial port.  Two calling sequences are supported:

    1. If "val" is zero, then "uptr" is implicitly associated with the line
       number corresponding to the position of the unit in the zero-based array
       of units belonging to the associated device, and "cptr" points to the
       serial port name.  For example, if "uptr" points to unit 3 in a given
       device, and "cptr" points to the string "COM1", then line 3 will be
       attached to serial port "COM1".

    2. If "val" is non-zero, then "cptr" points to a string that is parsed for
       an explicit line number and serial port name, and "uptr" is ignored.  The
       number and name are delimited by the character represented by "val".  For
       example, if "val" is 58 (':'), and "cptr" points to the string "3:COM1",
       then line 3 will be attached to serial port "COM1".

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

   The supported rates, sizes, and parity options are host-specific.  If a
   configuration string is not supplied, then host system defaults are used.

   If the serial port allocation is successful, then if "val" is zero, then the
   port name is stored in the UNIT structure, and the UNIT_ATT flag is set.  If
   "val" is non-zero, then this is not done, as there is no unit corresponding
   to the attached line.

   Implementation notes:

    1. If the device associated with the unit referenced by "uptr" does not have
       the DEV_NET flag set, then the optional configuration string is saved
       with the port name in the UNIT structure.  This allows a RESTORE to
       reconfigure the attached serial port during reattachment.  The combined
       string will be displayed when the unit is SHOWed.

       If the unit has the DEV_NET flag, the optional configuration string is
       removed before the attached port name is saved in the UNIT structure, as
       RESTORE will not reattach the port, and so reconfiguration is not needed.

    2. This function may be called as an MTAB processing routine.
*/

t_stat tmxr_attach_line (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
DEVICE *dptr;
char *pptr, *sptr, *tptr;
SERHANDLE serport;
SERCONFIG config = { 0 };
t_stat status;
char portname [1024];
t_bool arg_error = FALSE;

if (val) {                                              /* explicit line? */
    if (cptr == NULL)                                   /* arguments supplied? */
        return SCPE_ARG;                                /* report bad argument */
    uptr = NULL;                                        /* indicate to get routine */
    tptr = strchr (cptr, (char) val);                   /* search for separator */

    if (tptr == NULL)                                   /* not found? */
        return SCPE_ARG;                                /* report bad argument */
    else                                                /* found */
        *tptr = '\0';                                   /* terminate for get_uint */
    }

lp = tmxr_get_ldsc (uptr, cptr, mp, &status);           /* get referenced line */

if (lp == NULL)                                         /* bad line number? */
    return status;                                      /* report it */

if (lp->conn)                                           /* line connected via Telnet? */
    return SCPE_NOFNC;                                  /* command not allowed */

if (val)                                                /* named line form? */
    cptr = tptr + 1;                                    /* point at port name */

if ((cptr == NULL) || (!*cptr))                         /* port name missing? */
    return SCPE_ARG;                                    /* report it */

pptr = get_glyph_nc (cptr, portname, ';');              /* separate port name from optional params */

if (*pptr) {                                                /* parameter string present? */
    config.baudrate = (uint32) strtotv (pptr, &sptr, 10);   /* parse baud rate */
    arg_error = (pptr == sptr);                             /* check for bad argument */

    if (*sptr)                                              /* separator present? */
        sptr++;                                             /* skip it */

    config.charsize = (uint32) strtotv (sptr, &tptr, 10);   /* parse character size */
    arg_error = arg_error || (sptr == tptr);                /* check for bad argument */

    if (*tptr)                                              /* parity character present? */
        config.parity = toupper (*tptr++);                  /* save parity character */

    config.stopbits = (uint32) strtotv (tptr, &sptr, 10);   /* parse number of stop bits */
    arg_error = arg_error || (tptr == sptr);                /* check for bad argument */

    if (arg_error)                                          /* bad conversions? */
        return SCPE_ARG;                                    /* report argument error */

    else if (strcmp (sptr, ".5") == 0)                      /* 1.5 stop bits requested? */
        config.stopbits = 0;                                /* code request */
    }

serport = sim_open_serial (portname);                   /* open the serial port */

if (serport == INVALID_HANDLE)                          /* not a valid port name */
    return SCPE_OPENERR;                                /* cannot attach */

else {                                                  /* good serial port */
    if (*pptr) {                                        /* parameter string specified? */
        status = sim_config_serial (serport, config);   /* set serial configuration */

        if (status != SCPE_OK) {                        /* port configuration error? */
            sim_close_serial (serport);                 /* close the port */
            return status;                              /* report error */
            }
        }

    if (val == 0) {                                     /* unit implies line? */
        dptr = find_dev_from_unit (uptr);               /* find associated device */

        if (dptr && (dptr->flags & DEV_NET))            /* will RESTORE be inhibited? */
            cptr = portname;                            /* yes, so save just port name */

        if (mp->dptr == NULL)                           /* has device been set? */
            mp->dptr = dptr;                            /* no, so set device now */
        }

    tptr = (char *) malloc (strlen (cptr) + 1);         /* get buffer for port name and maybe params */

    if (tptr == NULL) {                                 /* allocation problem? */
        sim_close_serial (serport);                     /* close the port */
        return SCPE_MEM;                                /* report allocation failure */
        }

    strcpy (tptr, cptr);                                /* copy the port name into the buffer */

    if (val == 0) {                                     /* unit implies line? */
        uptr->filename = tptr;                          /* save buffer pointer in UNIT */
        uptr->flags = uptr->flags | UNIT_ATT;           /* mark unit as attached */
        }

    lp->mp = mp;                                        /* set the back pointer */
    tmxr_init_line (lp);                                /* initialize the line state */
    lp->serport = serport;                              /* set serial port handle */
    lp->sername = tptr;                                 /* set serial port name */
    lp->cnms = 0;                                       /* schedule for immediate connection */
    lp->conn = 0;                                       /* indicate no connection yet */
    mp->pending = mp->pending + 1;                      /*   but connection is pending */
    }

return SCPE_OK;                                         /* line has been connected */
}


/* Close a master listening socket.

   The listening socket associated with multiplexer descriptor "mp" is closed
   and deallocated.  In addition, all current Telnet sessions are disconnected.
   Serial sessions are not affected.
*/

t_stat tmxr_close_master (TMXR *mp)
{
int32 i;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {                       /* loop thru conn */
    lp = mp->ldsc + i;

    if (!lp->serport && lp->conn) {                     /* not serial and is connected? */
        tmxr_report_disconnection (lp);                 /* report disconnection */
        tmxr_reset_ln (lp);                             /* disconnect line */
        }
    }

sim_close_sock (mp->master, 1);                         /* close master socket */
mp->master = 0;
return SCPE_OK;
}


/* Detach unit from master socket.

   Note that we return SCPE_OK, regardless of whether a listening socket was
   attached.  For single-line multiplexers that may be attached either to a
   listening socket or to a serial port, call "tmxr_detach_line" first.  If that
   routine returns SCPE_UNATT, then call "tmxr_detach".
*/

t_stat tmxr_detach (TMXR *mp, UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
tmxr_close_master (mp);                                 /* close master socket */
free (uptr->filename);                                  /* free port string */
uptr->filename = NULL;
uptr->flags = uptr->flags & ~UNIT_ATT;                  /* not attached */
return SCPE_OK;
}


/* Detach a line from serial port.

   Disconnect and detach a line of the multiplexer associated with descriptor
   "desc" from a serial port.  Two calling sequences are supported:

    1. If "val" is zero, then "uptr" is implicitly associated with the line
       number corresponding to the position of the unit in the zero-based array
       of units belonging to the associated device, and "cptr" points to the
       serial port name.  For example, if "uptr" points to unit 3 in a given
       device, then line 3 will be detached from the associated serial port.

    2. If "val" is non-zero, then "cptr" points to a string that is parsed for
       an explicit line number, and "uptr" is ignored.  For example, if "cptr"
       points to the string "3", then line 3 will be detached from the
       associated serial port.

   Calling sequence 2 allows serial ports to be used with multiplexers that do
   not implement unit-per-line.  In this configuration, there is no unit
   associated with a given line, so the ATTACH and DETACH commands cannot be
   used.  Instead, SET commands directed to the device must specify the line
   number and port name to open (e.g., "SET <dev> CONNECT=<n>:<port>") or close
   (e.g., "SET <dev> DISCONNECT=<n>").  These commands call "tmxr_attach_line"
   and "tmxr_detach_line", respectively, with non-zero "val" parameters.

   As an aid for this configuration, we do not verify serial port connections
   when "val" is non-zero.  That is, we will disconnect the line without regard
   to whether a serial or Telnet connection is present.  Then, if a serial
   connection was present, the serial port is closed.  This allows the SET
   DISCONNECT command to be used to disconnect (close) both Telnet and serial
   sessions.

   Implementation notes:

    1. If "val" is zero, and the specified line is not connected to a serial
       port, then SCPE_UNATT is returned.  This allows a common VM-provided
       detach routine in a single-line device to attempt to detach a serial port
       first, and then, if that fails, to detach a Telnet session via
       "tmxr_detach".  Note that the latter will always succeed, even if the
       unit is not attached, and so cannot be called first.

    2. If the serial connection was completed, we drop the line to ensure that a
       modem will disconnect.  This increments the pending connection count in
       preparation for reconnecting.  If the connection was not completed, it
       will still be pending.  In either case, we drop the pending connection
       count, as we will be closing the serial port.

    3. This function may be called as an MTAB processing routine.
*/

t_stat tmxr_detach_line (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
t_stat status;

if (val)                                                /* explicit line? */
    uptr = NULL;                                        /* indicate to get routine */

lp = tmxr_get_ldsc (uptr, cptr, mp, &status);           /* get referenced line */

if (lp == NULL)                                         /* bad line number? */
    return status;                                      /* report it */

if (uptr && lp->serport == 0)                           /* serial port attached to unit? */
    return SCPE_UNATT;                                  /* no, so report status to caller */

if (lp->conn) {                                         /* was connection made? */
    tmxr_report_disconnection (lp);                     /* report disconnection */
    tmxr_clear_ln (mp, lp);                             /* close line */
    }

if (lp->serport) {                                      /* serial port attached? */
    mp->pending = mp->pending - 1;                      /* drop pending connection count */

    sim_close_serial (lp->serport);                     /* close serial port */
    lp->serport = 0;                                    /* clear handle */
    free (lp->sername);                                 /* free port name */
    lp->sername = 0;                                    /* clear pointer */

    if (uptr) {                                         /* unit implies line? */
        uptr->filename = NULL;                          /* clear attached name pointer */
        uptr->flags = uptr->flags & ~UNIT_ATT;          /* no longer attached */
        }
    }

return SCPE_OK;
}


/* Check if a line is free.

   A line is free if it is not connected to a Telnet session or a serial port.
*/

t_bool tmxr_line_free (TMLN *lp)
{
return lp && (lp->conn == 0) && (lp->serport == 0);     /* free if no connection */
}


/* Check if a multiplexer is free.

   A multiplexer is free if it is not listening for new Telnet connections and
   no lines are connected to serial ports.  Note that if the listening socket is
   detached, then no Telnet sessions can exist, so we only need to check for
   serial connections on the lines.
*/

t_bool tmxr_mux_free (TMXR *mp)
{
TMLN* lp;
int32 ln;

if (mp == NULL || mp->master || mp->pending)            /* listening for Telnet or serial connection? */
    return FALSE;                                       /* not free */

for (ln = 0; ln < mp->lines; ln++) {                    /* check each line for serial connection */
    lp = mp->ldsc + ln;                                 /* get pointer to line descriptor */

    if (lp->serport)                                    /* serial connection? */
        return FALSE;                                   /* not free */
    }

return TRUE;                                            /* no connections, so mux is free */
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
if (sock)
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


/* Print connections - used only in named SHOW command */

void tmxr_fconns (FILE *st, TMLN *lp, int32 ln)
{
if (ln >= 0)
    fprintf (st, "line %d: ", ln);

if (lp->conn) {
    int32 o1, o2, o3, o4, hr, mn, sc;
    uint32 ctime;

    if (lp->serport)                                    /* serial connection? */
        fprintf (st, "Serial port %s", lp->sername);    /* print port name */

    else {                                              /* socket connection */
        o1 = (lp->ipad >> 24) & 0xFF;
        o2 = (lp->ipad >> 16) & 0xFF;
        o3 = (lp->ipad >> 8) & 0xFF;
        o4 = (lp->ipad) & 0xFF;
        fprintf (st, "IP address %d.%d.%d.%d", o1, o2, o3, o4);
        }

    ctime = (sim_os_msec () - lp->cnms) / 1000;
    hr = ctime / 3600;
    mn = (ctime / 60) % 60;
    sc = ctime % 60;
    if (ctime)
        fprintf (st, ", connected %02d:%02d:%02d\n", hr, mn, sc);
    }
else fprintf (st, "line disconnected\n");
if (lp->txlog)
    fprintf (st, "Logging to %s\n", lp->txlogname);
return;
}


/* Print statistics - used only in named SHOW command */

void tmxr_fstats (FILE *st, TMLN *lp, int32 ln)
{
static const char *enab = "on";
static const char *dsab = "off";

if (ln >= 0)
    fprintf (st, "line %d:\b", ln);
if (!lp->conn)
    fprintf (st, "line disconnected\n");
if (lp->rxcnt)
    fprintf (st, "  input (%s) queued/total = %d/%d\n",
        (lp->rcve? enab: dsab),
        tmxr_rqln (lp), lp->rxcnt);
if (lp->txcnt || lp->txbpi)
    fprintf (st, "  output (%s) queued/total = %d/%d\n",
        (lp->xmte? enab: dsab),
        tmxr_tqln (lp), lp->txcnt);
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
   Telnet session or a serial port.  Two calling sequences are supported:

    1. If "val" is zero, then "uptr" is implicitly associated with the line
       number corresponding to the position of the unit in the zero-based array
       of units belonging to the associated device, and "cptr" is ignored.  For
       example, if "uptr" points to unit 3 in a given device, then line 3 will
       be disconnected.

    2. If "val" is non-zero, then "cptr" points to a string that is parsed for
       an explicit line number, and "uptr" is ignored.  For example, if "cptr"
       points to the string "3", then line 3 will be disconnected.

   If the line was connected to a Telnet session, the socket associated with the
   line will be closed.  If the line was connected to a serial port, the port
   will NOT be closed, but DTR will be dropped.  The line will be reconnected
   after a short delay to allow the serial device to recognize the DTR state
   change.

   Implementation notes:

    1. This function may be called as an MTAB processing routine.
*/

t_stat tmxr_dscln (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
t_stat status;

if (val)                                                        /* explicit line? */
    uptr = NULL;                                                /* indicate to get routine */

lp = tmxr_get_ldsc (uptr, cptr, mp, &status);                   /* get referenced line */

if (lp == NULL)                                                 /* bad line number? */
    return status;                                              /* report it */

if (lp->conn) {                                                 /* connection active? */
    tmxr_linemsg (lp, "\r\nOperator disconnected line\r\n\n");  /* report closure */
    tmxr_clear_ln (mp, lp);                                     /* drop the line */
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
    t = t + (mp->ldsc[i].conn != 0);
if (t == 1)
    fprintf (st, "1 connection");
else fprintf (st, "%d connections", t);
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
    if (mp->ldsc[i].conn) {
        any++;
        if (val)
            tmxr_fconns (st, &mp->ldsc[i], i);
        else tmxr_fstats (st, &mp->ldsc[i], i);
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
    char value;
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
        {TN_LINE,   "TN_LINE"},               /* line mode */
        {TN_CR,     "TN_CR"},                 /* carriage return */
        {TN_LF,     "TN_LF"},                 /* line feed */
        {0, NULL}};

static char *tmxr_debug_buf = NULL;
static size_t tmxr_debug_buf_used = 0;
static size_t tmxr_debug_buf_size = 0;

static void tmxr_buf_debug_char (char value)
{
if (tmxr_debug_buf_used+2 > tmxr_debug_buf_size) {
    tmxr_debug_buf_size += 1024;
    tmxr_debug_buf = realloc(tmxr_debug_buf, tmxr_debug_buf_size);
    }
tmxr_debug_buf[tmxr_debug_buf_used++] = value;
tmxr_debug_buf[tmxr_debug_buf_used] = '\0';
}

static void tmxr_buf_debug_string (const char *string)
{
while (*string)
    tmxr_buf_debug_char (*string++);
}

void tmxr_debug (uint32 dbits, TMLN *lp, const char *msg, char *buf, int bufsize)
{
if ((lp->mp->dptr) && (dbits & lp->mp->dptr->dctrl)) {
    int i, j;

    tmxr_debug_buf_used = 0;
    if (tmxr_debug_buf)
        tmxr_debug_buf[tmxr_debug_buf_used] = '\0';
    for (i=0; i<bufsize; ++i) {
        for (j=0; 1; ++j) {
            if (NULL == tn_chars[j].name) {
                tmxr_buf_debug_char (buf[i]);
                break;
                }
            if (buf[i] == tn_chars[j].value) {
                tmxr_buf_debug_char ('_');
                tmxr_buf_debug_string (tn_chars[j].name);
                tmxr_buf_debug_char ('_');
                break;
                }
            }
        }
    sim_debug (dbits, lp->mp->dptr, "%s %d bytes '%s'\n", msg, bufsize, tmxr_debug_buf);
    }
}
