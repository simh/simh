/* sim_tmxr.c: Telnet terminal multiplexor library

   Copyright (c) 2001-2021, Robert M Supnik

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

   31-Jan-21    JDB     Added a cast in "tmxr_set_lnorder" from t_addr to uint32
   26-Oct-20    JDB     Line order now supports partial connection lists
   23-Oct-20    JDB     Added tmxr_table and tmxr_post_logs
                        tmxr_set_log now takes -N switch for new file
   19-Dec-19    JDB     Added tmxr_is_extended global hook
   19-Mar-19    JDB     Added tmxr_read, tmxr_write, tmxr_show, tmxr_close
                        global hooks and associated local hook routines;
                        added tmxr_init_line, tmxr_report_connection,
                        and tmxr_disconnect_line global routines
   06-Mar-18    RMS     Revised for new IP address format in sim_sock
   08-Jul-17    JDB     Corrected misleading indentation in tmxr_poll_tx
   06-Aug-15    JDB     [4.0] Added modem control functions
   28-Mar-15    RMS     Revised to use sim_printf
   16-Jan-11    MP      Made option negotiation more reliable
   20-Nov-08    RMS     Added three new standardized SHOW routines
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

   tmxr_poll_conn -     poll for connection
   tmxr_reset_ln -      reset line
   tmxr_getc_ln -       get character for line
   tmxr_poll_rx -       poll receive
   tmxr_putc_ln -       put character for line
   tmxr_poll_tx -       poll transmit
   tmxr_set_modem_control_passthru -    enable modem control on a multiplexer
   tmxr_set_get_modem_bits -            set and/or get a line modem bits
   tmxr_open_master -   open master connection
   tmxr_close_master -  close master connection
   tmxr_attach  -       attach terminal multiplexor
   tmxr_detach  -       detach terminal multiplexor
   tmxr_ex      -       (null) examine
   tmxr_dep     -       (null) deposit
   tmxr_msg     -       send message to socket
   tmxr_linemsg -       send message to line
   tmxr_fconns  -       output line connection status
   tmxr_fstats  -       output line connection statistics
   tmxr_set_log -       open a log file for a terminal line
   tmxr_set_nolog -     close a log file for a terminal line
   tmxr_post_logs -     flush and optionally close all log files
   tmxr_show_log -      output log file status
   tmxr_dscln   -       disconnect line (SET routine)
   tmxr_rqln    -       number of available characters for line
   tmxr_tqln    -       number of buffered characters for line
   tmxr_set_lnorder -   set line connection order
   tmxr_show_lnorder -  show line connection order
   tmxr_show_summ -     output the number of connections
   tmxr_show_cstat -    output device connection status or statistics
   tmxr_show_lines -    output the number of lines
   tmxr_find_ldsc -     find a line descriptor
   tmxr_send_buffered_data -    write the line data
   tmxr_init_line -             initialize the line data
   tmxr_report_connection -     report a line connection to the port
   tmxr_disconnect_line -       disconnect a line

   All routines are OS-independent.
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include "scp.h"
#include <ctype.h>

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

/* Multiplexer-descriptor table.

   Each device multiplexer declares a multiplexer descriptor (TMXR) structure to
   identify the mux and its terminal lines.  The structure is local to the
   device simulator, and nothing in the DEVICE structure points to it, so there
   is no external way of accessing the line (TMLN) structures.  Access is needed
   if the associated terminal line logs are to be flushed when a simulator stop
   occurs and closed when the simulator exits.  This is provided by a global
   table of TMXR structures that is filled in when line logs are opened.
*/

#define TABLE_COUNT     10                              /* the number of table entries provided */

static TMXR *tmxr_table [TABLE_COUNT] = { NULL };       /* the table of multiplexer descriptors */

void tmxr_rmvrc (TMLN *lp, int32 p);

extern char sim_name[];
extern uint32 sim_os_msec (void);

static int32 tmxr_local_read  (TMLN *lp, int32 length);
static int32 tmxr_local_write (TMLN *lp, int32 length);
static void  tmxr_local_show  (TMLN *lp, FILE *stream);
static void  tmxr_local_close (TMLN *lp);

int32  (*tmxr_read)        (TMLN *lp, int32 length) = tmxr_local_read;
int32  (*tmxr_write)       (TMLN *lp, int32 length) = tmxr_local_write;
void   (*tmxr_show)        (TMLN *lp, FILE *stream) = tmxr_local_show;
void   (*tmxr_close)       (TMLN *lp)               = tmxr_local_close;
t_bool (*tmxr_is_extended) (TMLN *lp)               = NULL;

/* Poll for new connection

   Called from unit service routine to test for new connection

   Inputs:
        *mp     =       pointer to terminal multiplexor descriptor
   Outputs:
        line number activated, -1 if none

   If a connection order is defined for the descriptor, and the first value is
   not < 0 to indicate the default order, then the order array is used to find
   an open line.  Otherwise, a search is made of all lines in numerical
   sequence.

   If some valid lines are to be omitted from the connection order, a value < 0
   will appear after the last allowed line value.  For example, specifying
   connection order values of 1, 0, 2, and -1 will allow connections to lines 1,
   0, and 2 in that order.  Additional connection attempts will fail with "All
   connections busy," even though more lines are available in the device.
*/

int32 tmxr_poll_conn (TMXR *mp)
{
SOCKET newsock;
TMLN *lp;
int32 *op, *fop;
int32 i, j;
char *ipaddr = NULL;

static char mantra[] = {
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

newsock = sim_accept_conn (mp->master, &ipaddr);        /* poll connect */
if (newsock != INVALID_SOCKET) {                        /* got a live one? */
    fop = op = mp->lnorder;                             /* get line connection order list pointer */
    i = mp->lines;                                      /* play it safe in case lines == 0 */

    for (j = 0; j < mp->lines; j++, i++) {              /* find the next available line */
        if (fop == NULL || *fop < 0)                    /* if the first list entry is undefined or defaulted */
            i = j;                                      /*   then use the next sequential line */

        else if (*op >= 0 && *op < mp->lines)           /* otherwise if the line number is legal */
            i = *op++;                                  /*   then use the next listed line */

        else {                                          /* otherwise the list entry is invalid */
            i = mp->lines;                              /*   so abandon the search now */
            break;                                      /*     and report that no lines are free */
            }

        lp = mp->ldsc + i;                              /* get pointer to line descriptor */
        if (lp->conn == 0)                              /* is the line available? */
            break;                                      /* yes, so stop search */
        }

    if (i >= mp->lines) {                               /* all busy? */
        tmxr_msg (newsock, "All connections busy\r\n");
        sim_close_sock (newsock);
        }
    else {
        lp = mp->ldsc + i;                              /* get line desc */
        lp->conn = newsock;                             /* record connection */
        lp->ipad = ipaddr;                              /* ip address */
        lp->cnms = sim_os_msec ();                      /* time of conn */
        tmxr_init_line (lp);                            /* initialize the line */
        sim_write_sock (newsock, mantra, sizeof (mantra));
        tmxr_report_connection (mp, lp, i);             /* report the connection */
        return i;
        }
    }                                                   /* end if newsock */
return -1;
}

/* Reset line */

void tmxr_reset_ln (TMLN *lp)
{
if (lp->txlog)                                          /* dump log */
    fflush (lp->txlog);
tmxr_send_buffered_data (lp);                           /* send buffered data */
free (lp->ipad);
lp->ipad = NULL;
tmxr_close (lp);                                        /* reset the connection */
tmxr_init_line (lp);                                    /* initialize the line */
lp->conn = 0;                                           /*   and clear the connection */
return;
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
if (mp->master)
    return SCPE_ALATT;
else
    return SCPE_OK;
}

t_stat tmxr_set_modem_control_passthru (TMXR *mp)
{
return tmxr_clear_modem_control_passthru_state (mp, TRUE);
}

/* Manipulate the modem control bits of a specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
        bits_to_set     TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired
        bits_to_clear   TMXR_MDM_DTR and/or TMXR_MDM_RTS as desired

   Output:
        incoming_bits   if non NULL, returns the current stat of DCD,
                        RNG, CTS and DSR along with the current state
                        of DTR and RTS

   Implementation note:

       If a line is connected to a serial port, then these values affect
       and reflect the state of the serial port.  If the line is connected
       to a network socket (or could be) then the network session state is
       set, cleared and/or returned.
*/
t_stat tmxr_set_get_modem_bits (TMLN *lp, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
int32 incoming_state;

if ((bits_to_set & ~(TMXR_MDM_OUTGOING)) ||         /* Assure only settable bits */
    (bits_to_clear & ~(TMXR_MDM_OUTGOING)) ||
    (bits_to_set & bits_to_clear))                  /* and can't set and clear the same bits */
    return SCPE_ARG;

if (lp->conn)
    incoming_state = TMXR_MDM_DCD | TMXR_MDM_DSR;
else
    incoming_state = 0;

if (incoming_bits)
    *incoming_bits = incoming_state;

if (lp->conn && (bits_to_clear & TMXR_MDM_DTR)) {           /* drop DTR? */
    tmxr_linemsg (lp, "\r\nDisconnected from the ");
    tmxr_linemsg (lp, sim_name);
    tmxr_linemsg (lp, " simulator\r\n\n");

    tmxr_reset_ln (lp);
    }

return SCPE_OK;
}

/* Get character from specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
   Output:
        valid + char, 0 if line
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
            val = val | SCPE_BREAK;
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
    if (nbytes < 0)                                     /* closed? reset ln */
        tmxr_reset_ln (lp);
    else if (nbytes > 0) {                              /* if data rcvd */
        j = lp->rxbpi;                                  /* start of data */
        lp->rxbpi = lp->rxbpi + nbytes;                 /* adv pointers */
        lp->rxcnt = lp->rxcnt + nbytes;

        if (tmxr_is_extended != NULL                    /* if the line */
          && tmxr_is_extended (lp) == TRUE)             /*   is extended */
            continue;                                   /*     then skip the Telnet processing */

        memset (&lp->rbr[j], 0, nbytes);                /* clear status */

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
                if (tmp == TN_BIN) {                    /* reject all but binary mode */
                    char accept[] = {TN_IAC, TN_WILL, TN_BIN};
                    sim_write_sock (lp->conn, accept, sizeof(accept));
                    }
                tmxr_rmvrc (lp, j);                     /* remove it */
                lp->tsta = TNS_NORM;                    /* next normal */
                break;

            case TNS_SKIP: default:                     /* skip char */
                tmxr_rmvrc (lp, j);                     /* remove char */
                lp->tsta = TNS_NORM;                    /* next normal */
                break;
                }                                       /* end case state */
            }                                           /* end for char */
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
return (lp->rxbpi - lp->rxbpr);
}

/* Remove character p (and matching status) from line l input buffer */

void tmxr_rmvrc (TMLN *lp, int32 p)
{
for ( ; p < lp->rxbpi; p++) {
    lp->rxb[p] = lp->rxb[p + 1];
    lp->rbr[p] = lp->rbr[p + 1];
    }
lp->rbr[p] = 0;                                         /* clear potential break from vacated slot */
lp->rxbpi = lp->rxbpi - 1;
return;
}

/* Store character in line buffer

   Inputs:
        *lp     =       pointer to line descriptor
        chr     =       characters
   Outputs:
        status  =       ok, connection lost, or stall
*/

t_stat tmxr_putc_ln (TMLN *lp, int32 chr)
{
if (lp->txlog)                                          /* log if available */
    fputc (chr, lp->txlog);
if (lp->conn == 0)                                      /* no conn? lost */
    return SCPE_LOST;
if (tmxr_tqln (lp) < (TMXR_MAXBUF - 1)) {               /* room for char (+ IAC)? */
    lp->txb[lp->txbpi] = (char) chr;                    /* buffer char */
    lp->txbpi = lp->txbpi + 1;                          /* adv pointer */
    if (lp->txbpi >= TMXR_MAXBUF)                       /* wrap? */
        lp->txbpi = 0;
    if ((char) chr == TN_IAC) {                         /* IAC? */
        lp->txb[lp->txbpi] = (char) chr;                /* IAC + IAC */
        lp->txbpi = lp->txbpi + 1;                      /* adv pointer */
        if (lp->txbpi >= TMXR_MAXBUF)                   /* wrap? */
            lp->txbpi = 0;
        }
    if (tmxr_tqln (lp) > (TMXR_MAXBUF - TMXR_GUARD))    /* near full? */
        lp->xmte = 0;                                   /* disable line */
    return SCPE_OK;                                     /* char sent */
    }
lp->xmte = 0;                                           /* no room, dsbl line */
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
    nbytes = tmxr_send_buffered_data (lp);              /* buffered bytes */
    if (nbytes == 0)                                    /* buf empty? enab line */
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
        sbytes = tmxr_write (lp, TMXR_MAXBUF - lp->txbpr);  /* write to end buf */

    if (sbytes > 0) {                                   /* ok? */
        lp->txbpr = (lp->txbpr + sbytes);               /* update remove ptr */
        if (lp->txbpr >= TMXR_MAXBUF)                   /* wrap? */
            lp->txbpr = 0;
        lp->txcnt = lp->txcnt + sbytes;                 /* update counts */
        nbytes = nbytes - sbytes;
        }
    if (nbytes && (lp->txbpr == 0))     {               /* more data and wrap? */
        sbytes = tmxr_write (lp, nbytes);
        if (sbytes > 0) {                               /* ok */
            lp->txbpr = (lp->txbpr + sbytes);           /* update remove ptr */
            if (lp->txbpr >= TMXR_MAXBUF)               /* wrap? */
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
return (lp->txbpi - lp->txbpr + ((lp->txbpi < lp->txbpr)? TMXR_MAXBUF: 0));
}

/* Open master socket */

t_stat tmxr_open_master (TMXR *mp, char *cptr)
{
int32 i, port;
SOCKET sock;
TMLN *lp;
t_stat r;

port = (int32) get_uint (cptr, 10, 65535, &r);          /* get port */
if ((r != SCPE_OK) || (port == 0))
    return SCPE_ARG;
sock = sim_master_sock (cptr, &r);                      /* make master socket */
if (sock == INVALID_SOCKET)                             /* open error */
    return SCPE_OPENERR;
sim_printf ("Listening on port %d (socket %d)\n", port, sock);
mp->port = port;                                        /* save port */
mp->master = sock;                                      /* save master socket */
for (i = 0; i < mp->lines; i++) {                       /* initialize lines */
    lp = mp->ldsc + i;

    if (tmxr_is_extended == NULL                        /* if the line  */
      || tmxr_is_extended (lp) == FALSE) {              /*   is not extended */
        tmxr_init_line (lp);                            /*     then initialize the line */
        lp->conn = 0;                                   /*       and clear the connection */
        }
    }
return SCPE_OK;
}

/* Attach unit to master socket */

t_stat tmxr_attach (TMXR *mp, UNIT *uptr, char *cptr)
{
char* tptr;
t_stat r;

tptr = (char *) malloc (strlen (cptr) + 1);             /* get string buf */
if (tptr == NULL)                                       /* no more mem? */
    return SCPE_MEM;
r = tmxr_open_master (mp, cptr);                        /* open master socket */
if (r != SCPE_OK) {                                     /* error? */
    free (tptr);                                        /* release buf */
    return SCPE_OPENERR;
    }
strcpy (tptr, cptr);                                    /* copy port */
uptr->filename = tptr;                                  /* save */
uptr->flags = uptr->flags | UNIT_ATT;                   /* no more errors */

if (mp->dptr == NULL)                                   /* has device been set? */
    mp->dptr = find_dev_from_unit (uptr);               /* no, so set device now */

return SCPE_OK;
}

/* Close master socket */

t_stat tmxr_close_master (TMXR *mp)
{
int32 i;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {                       /* loop thru conn */
    lp = mp->ldsc + i;

    if (lp->conn                                        /* if the line is connected */
      && (tmxr_is_extended == NULL                      /*   and the line  */
      || tmxr_is_extended (lp) == FALSE))               /*     is not extended */
        tmxr_disconnect_line (lp);                      /*       then disconnect it */
    }                                                   /* end for */
sim_close_sock (mp->master);                            /* close master socket */
mp->master = 0;
return SCPE_OK;
}

/* Detach unit from master socket */

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

/* Stub examine and deposit */

t_stat tmxr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
return SCPE_NOFNC;
}

t_stat tmxr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
return SCPE_NOFNC;
}

/* Output message to socket or line descriptor */

void tmxr_msg (SOCKET sock, const char *msg)
{
if (sock)
    sim_write_sock (sock, msg, strlen (msg));
return;
}

void tmxr_linemsg (TMLN *lp, const char *msg)
{
int32 len;

for (len = strlen (msg); len > 0; --len)
    tmxr_putc_ln (lp, *msg++);
return;
}

/* Print connections - used only in named SHOW command */

void tmxr_fconns (FILE *st, TMLN *lp, int32 ln)
{
if (ln >= 0)
    fprintf (st, "line %d: ", ln);
if (lp->conn) {
    int32 hr, mn, sc;
    uint32 ctime;

    ctime = (sim_os_msec () - lp->cnms) / 1000;
    hr = ctime / 3600;
    mn = (ctime / 60) % 60;
    sc = ctime % 60;
    tmxr_show (lp, st);                                 /* display the port connection */
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
    fprintf (st, "line %d: ", ln);
if (lp->conn) {
    fprintf (st, "input (%s) queued/total = %d/%d, ",
        (lp->rcve? enab: dsab),
        lp->rxbpi - lp->rxbpr, lp->rxcnt);
    fprintf (st, "output (%s) queued/total = %d/%d\n",
        (lp->xmte? enab: dsab),
        lp->txbpi - lp->txbpr, lp->txcnt);
    }
else fprintf (st, "line disconnected\n");
return;
}

/* Disconnect line */

t_stat tmxr_dscln (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
int32 ln;
t_stat r;

if (mp == NULL)
    return SCPE_IERR;
if (val) {                                              /* = n form */
    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, mp->lines - 1, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    lp = mp->ldsc + ln;
    }
else {
    lp = tmxr_find_ldsc (uptr, 0, mp);
    if (lp == NULL)
        return SCPE_IERR;
    }
if (lp->conn) {
    tmxr_linemsg (lp, "\r\nOperator disconnected line\r\n\n");
    tmxr_reset_ln (lp);
    }
return SCPE_OK;
}

/* Enable logging for line.

   This routine opens the log file whose name is specified by "cptr" for the
   multiplexer line associated with "uptr" or, if NULL, for the line number
   specified by "val".  "desc" contains a pointer to the multiplexer descriptor.

   If opening is successful, the routine then adds the descriptor pointer to the
   table of descriptors, which is used to flush and eventually close the log
   files.


   Implementation notes:

    1. Table entries are not removed, so a NULL entry indicates both the end of
       the active list and the first available space.
*/

t_stat tmxr_set_log (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;
uint32 index;

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
if (sim_switches & SWMASK ('N'))                        /* if a new log file is requested */
    lp->txlog = fopen (cptr, "wb");                     /*   then open an empty file for writing */
else                                                    /* otherwise */
    lp->txlog = fopen (cptr, "ab");                     /*   open an existing file for appending */
if (lp->txlog == NULL) {                                /* error? */
    free (lp->txlogname);                               /* free buffer */
    return SCPE_OPENERR;
    }
else {                                                  /* otherwise if the open succeeded */
    for (index = 0; index < TABLE_COUNT; index++)       /* loop through the table entries */
        if (tmxr_table [index] == mp)                   /* if the mux descriptor is already present */
            break;                                      /*   then we're done */

        else if (tmxr_table [index] == NULL) {          /* otherwise if the entry is empty */
            tmxr_table [index] = mp;                    /*   then store the new descriptor */
            break;                                      /*     and we're done */
            }

    if (index == TABLE_COUNT)                           /* if there are no empty entries */
        return SCPE_IERR;                               /*   then report an internal error */
    else                                                /* otherwise the descriptor was */
        return SCPE_OK;                                 /*   either already present or added */
    }
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
    fclose (lp->txlog);                                 /* close log */
    free (lp->txlogname);                               /* free namebuf */
    lp->txlog = NULL;
    lp->txlogname = NULL;
    }
return SCPE_OK;
}

/* Flush and optionally close all log files.

   This routine flushes or closes all active terminal multiplexer line logs,
   depending on the value of the boolean parameter.  The routine uses the table
   of multiplexer descriptors constructed by the "tmxr_set_log" routine to
   identify the active lines.  The line structure array associated with each
   descriptor is examined in sequence for active logs.  Each one that is found
   is flushed to post the contents to disc and left open, or closed and marked
   inactive, as directed by the supplied parameter.


   Implementation notes:

    1. The table is organized so that a NULL entry indicates that no additional
       descriptors will be found.  That is, descriptors always appear at the
       beginning of the table.
*/

void tmxr_post_logs (t_bool close_logs)
{
uint32 index;
int32  line, line_count;
TMXR   *mptr;
TMLN   *lptr;

for (index = 0; index < TABLE_COUNT; index++)           /* look through the descriptor list */
    if (tmxr_table [index] == NULL)                     /*   until all entries */
        break;                                          /*     have been seen */

    else {                                              /* for each active entry */
        mptr = tmxr_table [index];                      /*   get the entry */
        lptr = mptr->ldsc;                              /*     and the line array */
        line_count = mptr->lines;                       /*       and the number of lines */

        for (line = 0; line < line_count; line++, lptr++)   /* for all lines in the array */
            if (lptr->txlog != NULL)                        /*   if the current line's log is active */
                if (close_logs)                             /*     then if closing is requested */
                    tmxr_set_nolog (NULL, line, NULL,       /*        then close the log */
                                    (void *) mptr);
                else                                        /* otherwise */
                    fflush (lptr->txlog);                   /*   flush the log and leave it open */
        }

return;
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

/* Find line descriptor.

   Note: This routine may be called with a UNIT that does not belong to the
   device indicated in the TMXR structure.  That is, the multiplexer lines may
   belong to a device other than the one attached to the socket (the HP 2100 MUX
   device is one example).  Therefore, we must look up the device from the unit
   at each call, rather than depending on the dptr stored in the TMXR.
*/

TMLN *tmxr_find_ldsc (UNIT *uptr, int32 val, TMXR *mp)
{
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

t_stat tmxr_set_lnorder (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR   *mp = (TMXR *) desc;
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

tptr = cptr + strlen (cptr);                            /* append a semicolon */
*tptr++ = ';';                                          /*   to the command string */
*tptr = '\0';                                           /*     to make parsing easier */

idx = 0;                                                /* initialize the index of ordered values */

while (*cptr != '\0') {                                     /* while characters remain in the command string */
    if (strncmp (cptr, "ALL;", 4) == 0) {                   /*   if the parameter is "ALL" */
        if (val != 0 || idx > 0 && idx <= max)              /*     then if some lines are restrictied or unspecified */
            for (line = (uint32) min; line <= max; line++)  /*       then fill them in sequentially */
                if (set [line] == FALSE)                    /*         setting each unspecified line */
                    list [idx++] = line;                    /*           into the line order */

        cptr = cptr + 4;                                /* advance past "ALL" and the trailing semicolon */

        if (*cptr != '\0')                              /* if "ALL" is not the last parameter */
            result = SCPE_2MARG;                        /*   then report extraneous items */

        break;                                          /* "ALL" terminates the order list */
        }

    cptr = get_range (NULL, cptr, &low, &high, 10, max, ';');   /* get a line range */

    if (cptr == NULL) {                                 /* if a parsing error occurred */
        result = SCPE_ARG;                              /*   then report an invalid argument */
        break;                                          /*     and terminate the parse */
        }

    else if (low < min || low > max || high > max) {    /* otherwise if the line number is invalid */
        result = SCPE_SUB;                              /*   then report the subscript is out of range */
        break;                                          /*     and terminate the parse */
        }

    else                                                /* otherwise it's a valid range */
        for (line = (uint32) low; line <= high; line++) /*   so add the line(s) to the order */
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



/* Global utility routines */


/* Initialize the line state.

   Reset the line state to represent an idle line.  Note that we do not clear
   all of the line structure members, so a connected line remains connected
   after this call.

   Because a line break is represented by a flag in the "receive break status"
   array, we must zero that array in order to clear any leftover break
   indications.
*/

void tmxr_init_line (TMLN *lp)
{
lp->tsta = 0;                                           /* clear the Telnet negotiation state */
lp->xmte = 1;                                           /* enable transmission */
lp->dstb = 0;                                           /* default to binary data mode */
lp->rxbpr = lp->rxbpi = lp->rxcnt = 0;                  /* clear the receive indexes */
lp->txbpr = lp->txbpi = lp->txcnt = 0;                  /* clear the transmit indexes */

memset (lp->rbr, 0, TMXR_MAXBUF);                       /* clear the break status array */

return;
}


/* Report a connection to a line.

   This routine sends a notification of the form:

      Connected to the <sim> simulator <dev> device, line <n>

   ...to the line number specified by the "line" parameter of the multiplexer
   associated with the line descriptor pointer "lp" and the mux descriptor
   pointer "mp".  If the device has only one line, the "line <n>" part is
   omitted.  If the device has not been defined, the "<dev> device" part is
   omitted.
*/

void tmxr_report_connection (TMXR *mp, TMLN *lp, int32 line)
{
int32 buffer_count;
char  line_number [20];

tmxr_linemsg (lp, "\n\r\nConnected to the ");           /* report the connection */
tmxr_linemsg (lp, sim_name);                            /*   to the simulator */
tmxr_linemsg (lp, " simulator ");

if (mp->dptr) {                                         /* if the device pointer has been set */
    tmxr_linemsg (lp, sim_dname (mp->dptr));            /*   then report the name */
    tmxr_linemsg (lp, " device");                       /*     of the connected device */

    if (mp->lines > 1) {                                /* if the multiplexer has more than one line */
        tmxr_linemsg (lp, ", line ");                   /*   then report the line number */
        sprintf (line_number, "%i", line);              /*     of the connection */
        tmxr_linemsg (lp, line_number);
        }
    }

tmxr_linemsg (lp, "\r\n\n");

buffer_count = tmxr_send_buffered_data (lp);            /* write the message */

if (buffer_count == 0)                                  /* if the write buffer is now empty */
    lp->xmte = 1;                                       /*   then reenable transmission if it was paused */

return;
}


/* Report a disconnection from a line.

   This routine sends a notification of the form:

      Disconnected from the <sim> simulator

   ...to the line number associated with the line descriptor pointer "lp" and
   then disconnects the line.


   Implementation notes:

    1. We do not write the buffer here, because the disconnect routine will do
       that for us.
*/

void tmxr_disconnect_line (TMLN *lp)
{
tmxr_linemsg (lp, "\r\nDisconnected from the ");        /* report the disconnection */
tmxr_linemsg (lp, sim_name);                            /*   from the simulator */
tmxr_linemsg (lp, " simulator\r\n\n");

tmxr_reset_ln (lp);                                     /* disconnect the line */

return;
}



/* Local hook routines */


/* Read from a line.

   This routine reads up to "length" bytes into the buffer associated with line
   "lp".  The actual number of bytes read is returned.  If no bytes are
   available, 0 is returned.  If an error occurred while reading, -1 is
   returned.
*/

static int32 tmxr_local_read (TMLN *lp, int32 length)
{
return sim_read_sock (lp->conn, &(lp->rxb [lp->rxbpi]), length);
}


/* Write to a line.

   This routine writes up to "length" bytes from the buffer associated with
   "lp".  The actual number of bytes written is returned.  If an error occurred
   while writing, -1 is returned.
*/

static int32 tmxr_local_write (TMLN *lp, int32 length)
{
int32 written;

written = sim_write_sock (lp->conn, &(lp->txb [lp->txbpr]), length);

if (written == SOCKET_ERROR)                        /* did an error occur? */
    return -1;                                      /* return error indication */
else
    return written;
}


/* Show a line.

   This routine writes the port description to the file indicated by the
   "stream" parameter.
*/

static void tmxr_local_show (TMLN *lp, FILE *stream)
{
fprintf (stream, "IP address %s", lp->ipad);
return;
}


/* Close a line.

   This routine closes the line indicated by the "lp" parameter.
*/

static void tmxr_local_close (TMLN *lp)
{
sim_close_sock (lp->conn);                              /* reset conn */
return;
}
