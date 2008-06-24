/* sim_tmxr.c: Telnet terminal multiplexor library

   Copyright (c) 2001-2007, Robert M Supnik

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
   tmxr_open_master -   open master connection
   tmxr_close_master -  close master connection
   tmxr_attach  -       attach terminal multiplexor
   tmxr_detach  -       detach terminal multiplexor
   tmxr_ex      -       (null) examine
   tmxr_dep     -       (null) deposit
   tmxr_msg     -       send message to socket
   tmxr_linemsg -       send message to line
   tmxr_fconns  -       output connection status
   tmxr_fstats  -       output connection statistics
   tmxr_dscln   -       disconnect line (SET routine)
   tmxr_rqln    -       number of available characters for line
   tmxr_tqln    -       number of buffered characters for line

   All routines are OS-independent.
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

/* Telnet protocol constants - negatives are for init'ing signed char data */

#define TN_IAC          -1                              /* protocol delim */
#define TN_DONT         -2                              /* dont */
#define TN_DO           -3                              /* do */
#define TN_WONT         -4                              /* wont */
#define TN_WILL         -5                              /* will */
#define TN_BRK          -13                             /* break */
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

void tmxr_rmvrc (TMLN *lp, int32 p);
int32 tmxr_send_buffered_data (TMLN *lp);
TMLN *tmxr_find_ldsc (UNIT *uptr, int32 val, TMXR *mp);

extern int32 sim_switches;
extern char sim_name[];
extern FILE *sim_log;
extern uint32 sim_os_msec (void);

/* Poll for new connection

   Called from unit service routine to test for new connection

   Inputs:
        *mp     =       pointer to terminal multiplexor descriptor
   Outputs:
        line number activated, -1 if none
*/

int32 tmxr_poll_conn (TMXR *mp)
{
SOCKET newsock;
TMLN *lp;
int32 i;
uint32 ipaddr;
static char mantra[] = {
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

newsock = sim_accept_conn (mp->master, &ipaddr);        /* poll connect */
if (newsock != INVALID_SOCKET) {                        /* got a live one? */
    for (i = 0; i < mp->lines; i++) {                   /* find avail line */
        lp = mp->ldsc + i;                              /* ptr to ln desc */
        if (lp->conn == 0) break;                       /* available? */
        }
    if (i >= mp->lines) {                               /* all busy? */
        tmxr_msg (newsock, "All connections busy\r\n");
        sim_close_sock (newsock, 0);
        }
    else {
        lp = mp->ldsc + i;                              /* get line desc */
        lp->conn = newsock;                             /* record connection */
        lp->ipad = ipaddr;                              /* ip address */
        lp->cnms = sim_os_msec ();                      /* time of conn */
        lp->rxbpr = lp->rxbpi = 0;                      /* init buf pointers */
        lp->txbpr = lp->txbpi = 0;
        lp->rxcnt = lp->txcnt = 0;                      /* init counters */
        lp->tsta = 0;                                   /* init telnet state */
        lp->xmte = 1;                                   /* enable transmit */
        lp->dstb = 0;                                   /* default bin mode */
        sim_write_sock (newsock, mantra, 15);
        tmxr_linemsg (lp, "\n\r\nConnected to the ");
        tmxr_linemsg (lp, sim_name);
        tmxr_linemsg (lp, " simulator\r\n\n");
        tmxr_poll_tx (mp);                              /* flush output */
        return i;
        }
    }                                                   /* end if newsock */
return -1;
}

/* Reset line */

void tmxr_reset_ln (TMLN *lp)
{
if (lp->txlog) fflush (lp->txlog);                      /* dump log */
tmxr_send_buffered_data (lp);                           /* send buffered data */
sim_close_sock (lp->conn, 0);                           /* reset conn */
lp->conn = lp->tsta = 0;                                /* reset state */
lp->rxbpr = lp->rxbpi = 0;
lp->txbpr = lp->txbpi = 0;
lp->xmte = 1;
lp->dstb = 0;
return;
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
        if (lp->rbr[lp->rxbpr]) val = val | SCPE_BREAK; /* break? */
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
    if (!lp->conn || !lp->rcve) continue;               /* skip if !conn */

    nbytes = 0;
    if (lp->rxbpi == 0)                                 /* need input? */
        nbytes = sim_read_sock (lp->conn,               /* yes, read */
            &(lp->rxb[lp->rxbpi]),                      /* leave spc for */
            TMXR_MAXBUF - TMXR_GUARD);                  /* Telnet cruft */
    else if (lp->tsta)                                  /* in Telnet seq? */
        nbytes = sim_read_sock (lp->conn,               /* yes, read to end */
            &(lp->rxb[lp->rxbpi]),
            TMXR_MAXBUF - lp->rxbpi);
    if (nbytes < 0) tmxr_reset_ln (lp);                 /* closed? reset ln */
    else if (nbytes > 0) {                              /* if data rcvd */
        j = lp->rxbpi;                                  /* start of data */
        memset (&lp->rbr[j], 0, nbytes);                /* clear status */
        lp->rxbpi = lp->rxbpi + nbytes;                 /* adv pointers */
        lp->rxcnt = lp->rxcnt + nbytes;

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
                if ((tmp == TN_IAC) & !lp->dstb) {      /* IAC + IAC, bin? */
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
                if (tmp == TN_WILL)                     /* IAC + WILL? */
                    lp->tsta = TNS_WILL;
                else if (tmp == TN_WONT)                /* IAC + WONT? */
                    lp->tsta = TNS_WONT;
                else lp->tsta = TNS_SKIP;               /* IAC + other */
                tmxr_rmvrc (lp, j);                     /* remove char */
                break;

            case TNS_WILL: case TNS_WONT:               /* IAC+WILL/WONT prev */
                if (tmp == TN_BIN) {                    /* BIN? */
                    if (lp->tsta == TNS_WILL) lp->dstb = 0;
                    else lp->dstb = 1;
                    }

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

            case TNS_SKIP: default:                     /* skip char */
                lp->tsta = TNS_NORM;                    /* next normal */
                tmxr_rmvrc (lp, j);                     /* remove char */
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
if (lp->txlog) fputc (chr, lp->txlog);                  /* log if available */
if (lp->conn == 0) return SCPE_LOST;                    /* no conn? lost */
if (tmxr_tqln (lp) < (TMXR_MAXBUF - 1)) {               /* room for char (+ IAC)? */
    lp->txb[lp->txbpi] = (char) chr;                    /* buffer char */
    lp->txbpi = lp->txbpi + 1;                          /* adv pointer */
    if (lp->txbpi >= TMXR_MAXBUF) lp->txbpi = 0;        /* wrap? */
    if ((char) chr == TN_IAC) {                         /* IAC? */
        lp->txb[lp->txbpi] = (char) chr;                /* IAC + IAC */
        lp->txbpi = lp->txbpi + 1;                      /* adv pointer */       
        if (lp->txbpi >= TMXR_MAXBUF) lp->txbpi = 0;    /* wrap? */
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
    if (lp->conn == 0) continue;                        /* skip if !conn */
        nbytes = tmxr_send_buffered_data (lp);          /* buffered bytes */
        if (nbytes == 0) lp->xmte = 1;                  /* buf empty? enab line */
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
        sbytes = sim_write_sock (lp->conn,              /* write all data */
            &(lp->txb[lp->txbpr]), nbytes);
    else sbytes = sim_write_sock (lp->conn,             /* write to end buf */
            &(lp->txb[lp->txbpr]), TMXR_MAXBUF - lp->txbpr);
    if (sbytes != SOCKET_ERROR) {                       /* ok? */
        lp->txbpr = (lp->txbpr + sbytes);               /* update remove ptr */
        if (lp->txbpr >= TMXR_MAXBUF) lp->txbpr = 0;    /* wrap? */
        lp->txcnt = lp->txcnt + sbytes;                 /* update counts */
        nbytes = nbytes - sbytes;
        }
    if (nbytes && (lp->txbpr == 0))     {               /* more data and wrap? */
        sbytes = sim_write_sock (lp->conn, lp->txb, nbytes);
        if (sbytes != SOCKET_ERROR) {                   /* ok */
            lp->txbpr = (lp->txbpr + sbytes);           /* update remove ptr */
            if (lp->txbpr >= TMXR_MAXBUF) lp->txbpr = 0;/* wrap? */
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
if ((r != SCPE_OK) || (port == 0)) return SCPE_ARG;
sock = sim_master_sock (port);                          /* make master socket */
if (sock == INVALID_SOCKET) return SCPE_OPENERR;        /* open error */
printf ("Listening on port %d (socket %d)\n", port, sock);
if (sim_log) fprintf (sim_log,
    "Listening on port %d (socket %d)\n", port, sock);
mp->port = port;                                        /* save port */
mp->master = sock;                                      /* save master socket */
for (i = 0; i < mp->lines; i++) {                       /* initialize lines */
    lp = mp->ldsc + i;
    lp->conn = lp->tsta = 0;
    lp->rxbpi = lp->rxbpr = 0;
    lp->txbpi = lp->txbpr = 0;
    lp->rxcnt = lp->txcnt = 0;
    lp->xmte = 1;
    lp->dstb = 0;
    }
return SCPE_OK;
}

/* Attach unit to master socket */

t_stat tmxr_attach (TMXR *mp, UNIT *uptr, char *cptr)
{
char* tptr;
t_stat r;

tptr = (char *) malloc (strlen (cptr) + 1);             /* get string buf */
if (tptr == NULL) return SCPE_MEM;                      /* no more mem? */
r = tmxr_open_master (mp, cptr);                        /* open master socket */
if (r != SCPE_OK) {                                     /* error? */
    free (tptr);                                        /* release buf */
    return SCPE_OPENERR;
    }
strcpy (tptr, cptr);                                    /* copy port */
uptr->filename = tptr;                                  /* save */
uptr->flags = uptr->flags | UNIT_ATT;                   /* no more errors */
return SCPE_OK;
}

/* Close master socket */

t_stat tmxr_close_master (TMXR *mp)
{
int32 i;
TMLN *lp;

for (i = 0; i < mp->lines; i++) {                       /* loop thru conn */
    lp = mp->ldsc + i;
    if (lp->conn) {
        tmxr_linemsg (lp, "\r\nDisconnected from the ");
        tmxr_linemsg (lp, sim_name);
        tmxr_linemsg (lp, " simulator\r\n\n");
        tmxr_reset_ln (lp);
        }                                               /* end if conn */
    }                                                   /* end for */
sim_close_sock (mp->master, 1);                         /* close master socket */
mp->master = 0;
return SCPE_OK;
}

/* Detach unit from master socket */

t_stat tmxr_detach (TMXR *mp, UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT)) return SCPE_OK;          /* attached? */
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

void tmxr_msg (SOCKET sock, char *msg)
{
if (sock) sim_write_sock (sock, msg, strlen (msg));
return;
}

void tmxr_linemsg (TMLN *lp, char *msg)
{
int32 len;

for (len = strlen (msg); len > 0; --len)
    tmxr_putc_ln (lp, *msg++);
return;
}

/* Print connections - used only in named SHOW command */

void tmxr_fconns (FILE *st, TMLN *lp, int32 ln)
{
if (ln >= 0) fprintf (st, "line %d: ", ln);
if (lp->conn) {
    int32 o1, o2, o3, o4, hr, mn, sc;
    uint32 ctime;

    o1 = (lp->ipad >> 24) & 0xFF;
    o2 = (lp->ipad >> 16) & 0xFF;
    o3 = (lp->ipad >> 8) & 0xFF;
    o4 = (lp->ipad) & 0xFF;
    ctime = (sim_os_msec () - lp->cnms) / 1000;
    hr = ctime / 3600;
    mn = (ctime / 60) % 60;
    sc = ctime % 60;
    fprintf (st, "IP address %d.%d.%d.%d", o1, o2, o3, o4);
    if (ctime) fprintf (st, ", connected %02d:%02d:%02d\n", hr, mn, sc);
    }
else fprintf (st, "line disconnected\n");
if (lp->txlog) fprintf (st, "Logging to %s\n", lp->txlogname);
return;
}

/* Print statistics - used only in named SHOW command */

void tmxr_fstats (FILE *st, TMLN *lp, int32 ln)
{
static const char *enab = "on";
static const char *dsab = "off";

if (ln >= 0) fprintf (st, "line %d: ", ln);
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

if (mp == NULL) return SCPE_IERR;
if (val) {                                              /* = n form */
    if (cptr == NULL) return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, mp->lines - 1, &r);
    if (r != SCPE_OK) return SCPE_ARG;
    lp = mp->ldsc + ln;
    }
else {
    lp = tmxr_find_ldsc (uptr, 0, mp);
    if (lp == NULL) return SCPE_IERR;
    }
if (lp->conn) {
    tmxr_linemsg (lp, "\r\nOperator disconnected line\r\n\n");
    tmxr_reset_ln (lp);
    }
return SCPE_OK;
}

/* Enable logging for line */

t_stat tmxr_set_log (UNIT *uptr, int32 val, char *cptr, void *desc)
{
TMXR *mp = (TMXR *) desc;
TMLN *lp;

if (cptr == NULL) return SCPE_2FARG;                    /* no file name? */
lp = tmxr_find_ldsc (uptr, val, mp);                    /* find line desc */
if (lp == NULL) return SCPE_IERR;
if (lp->txlog) tmxr_set_nolog (NULL, val, NULL, desc);  /* close existing log */
lp->txlogname = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc namebuf */
if (lp->txlogname == NULL) return SCPE_MEM;             /* can't? */
strncpy (lp->txlogname, cptr, CBUFSIZE);                /* save file name */
lp->txlog = fopen (cptr, "ab");                         /* open log */
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

if (cptr) return SCPE_2MARG;                            /* no arguments */
lp = tmxr_find_ldsc (uptr, val, mp);                    /* find line desc */
if (lp == NULL) return SCPE_IERR;
if (lp->txlog) {                                        /* logging? */
    fclose (lp->txlog);                                 /* close log */
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
if (lp == NULL) return SCPE_IERR;
if (lp->txlog) fprintf (st, "logging to %s", lp->txlogname);
else fprintf (st, "no logging");
return SCPE_OK;
}

/* Find line descriptor */

TMLN *tmxr_find_ldsc (UNIT *uptr, int32 val, TMXR *mp)
{
if (uptr) {                                             /* called from SET? */
    DEVICE *dptr = find_dev_from_unit (uptr);           /* find device */
    if (dptr == NULL) return NULL;                      /* what?? */
    val = (int32) (uptr - dptr->units);                 /* implicit line # */
    }
if ((val < 0) || (val >= mp->lines)) return NULL;       /* invalid line? */
return mp->ldsc + val;                                  /* line descriptor */
}
