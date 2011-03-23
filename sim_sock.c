/* sim_sock.c: OS-dependent socket routines

   Copyright (c) 2001-2010, Robert M Supnik

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

   22-Jun-10    RMS     Fixed types in sim_accept_conn (from Mark Pizzolato)
   19-Nov-05    RMS     Added conditional for OpenBSD (from Federico G. Schwindt)
   16-Aug-05    RMS     Fixed spurious SIGPIPE signal error in Unix
   14-Apr-05    RMS     Added WSAEINPROGRESS test (from Tim Riker)
   09-Jan-04    RMS     Fixed typing problem in Alpha Unix (found by Tim Chapman)
   17-Apr-03    RMS     Fixed non-implemented version of sim_close_sock
                        (found by Mark Pizzolato)
   17-Dec-02    RMS     Added sim_connect_socket, sim_create_socket
   08-Oct-02    RMS     Revised for .NET compatibility
   22-Aug-02    RMS     Changed calling sequence for sim_accept_conn
   22-May-02    RMS     Added OS2 EMX support from Holger Veit
   06-Feb-02    RMS     Added VMS support from Robert Alan Byer
   16-Sep-01    RMS     Added Macintosh support from Peter Schorn
   02-Sep-01    RMS     Fixed UNIX bugs found by Mirian Lennox and Tom Markson
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include <signal.h>

/* OS dependent routines

   sim_master_sock      create master socket
   sim_accept_conn      accept connection
   sim_read_sock        read from socket
   sim_write_sock       write from socket
   sim_close_sock       close socket
   sim_setnonblock      set socket non-blocking
   sim_msg_sock         send message to socket
*/

int32 sim_sock_cnt = 0;

/* First, all the non-implemented versions */

#if defined (__OS2__) && !defined (__EMX__)

SOCKET sim_master_sock (int32 port)
{
return INVALID_SOCKET;
}

SOCKET sim_connect_sock (int32 ip, int32 port)
{
return INVALID_SOCKET;
}

SOCKET sim_accept_conn (SOCKET master, uint32 *ipaddr)
{
return INVALID_SOCKET;
}

int32 sim_read_sock (SOCKET sock, char *buf, int32 nbytes)
{
return -1;
}

int32 sim_write_sock (SOCKET sock, char *msg, int32 nbytes)
{
return 0;
}

void sim_close_sock (SOCKET sock, t_bool master)
{
return;
}

SOCKET sim_setnonblock (SOCKET sock)
{
return SOCKET_ERROR;
}

#else                                                   /* endif unimpl */

/* UNIX, Win32, Macintosh, VMS, OS2 (Berkeley socket) routines */

SOCKET sim_err_sock (SOCKET s, char *emsg, int32 flg)
{
int32 err = WSAGetLastError ();

printf ("Sockets: %s error %d\n", emsg, err);
sim_close_sock (s, flg);
return INVALID_SOCKET;
}

SOCKET sim_create_sock (void)
{
SOCKET newsock;
int32 err;

#if defined (_WIN32)
WORD wVersionRequested; 
WSADATA wsaData; 
wVersionRequested = MAKEWORD (1, 1); 

if (sim_sock_cnt == 0) {
    err = WSAStartup (wVersionRequested, &wsaData);     /* start Winsock */ 
    if (err != 0) {
        printf ("Winsock: startup error %d\n", err);
        return INVALID_SOCKET;
        }
    }
sim_sock_cnt = sim_sock_cnt + 1;
#endif                                                  /* endif Win32 */
#if defined (SIGPIPE)
signal (SIGPIPE, SIG_IGN);                              /* no pipe signals */
#endif

newsock = socket (AF_INET, SOCK_STREAM, 0);             /* create socket */
if (newsock == INVALID_SOCKET) {                        /* socket error? */
    err = WSAGetLastError ();
    printf ("Sockets: socket error %d\n", err);
    return INVALID_SOCKET;
    }
return newsock;
}

SOCKET sim_master_sock (int32 port)
{
SOCKET newsock;
struct sockaddr_in name;
int32 sta;

newsock = sim_create_sock ();                           /* create socket */
if (newsock == INVALID_SOCKET)                          /* socket error? */
    return newsock;

name.sin_family = AF_INET;                              /* name socket */
name.sin_port = htons ((unsigned short) port);          /* insert port */
name.sin_addr.s_addr = htonl (INADDR_ANY);              /* insert addr */

sta = bind (newsock, (struct sockaddr *) &name, sizeof (name));
if (sta == SOCKET_ERROR)                                /* bind error? */
    return sim_err_sock (newsock, "bind", 1);
sta = sim_setnonblock (newsock);                        /* set nonblocking */
if (sta == SOCKET_ERROR)                                /* fcntl error? */
    return sim_err_sock (newsock, "fcntl", 1);
sta = listen (newsock, 1);                              /* listen on socket */
if (sta == SOCKET_ERROR)                                /* listen error? */
    return sim_err_sock (newsock, "listen", 1);
return newsock;                                         /* got it! */
}

SOCKET sim_connect_sock (int32 ip, int32 port)
{
SOCKET newsock;
struct sockaddr_in name;
int32 sta;

newsock = sim_create_sock ();                           /* create socket */
if (newsock == INVALID_SOCKET)                          /* socket error? */
    return newsock;

name.sin_family = AF_INET;                              /* name socket */
name.sin_port = htons ((unsigned short) port);          /* insert port */
name.sin_addr.s_addr = htonl (ip);                      /* insert addr */

sta = sim_setnonblock (newsock);                        /* set nonblocking */
if (sta == SOCKET_ERROR)                                /* fcntl error? */
    return sim_err_sock (newsock, "fcntl", 1);
sta = connect (newsock, (struct sockaddr *) &name, sizeof (name));
if ((sta == SOCKET_ERROR) && 
    (WSAGetLastError () != WSAEWOULDBLOCK) &&
    (WSAGetLastError () != WSAEINPROGRESS))
    return sim_err_sock (newsock, "connect", 1);

return newsock;                                         /* got it! */
}

SOCKET sim_accept_conn (SOCKET master, uint32 *ipaddr)
{
int32 sta, err;
#if defined (macintosh) || defined (__linux) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__)
socklen_t size;
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__))
int size;
#else 
size_t size; 
#endif
SOCKET newsock;
struct sockaddr_in clientname;

if (master == 0)                                        /* not attached? */
    return INVALID_SOCKET;
size = sizeof (clientname);
newsock = accept (master, (struct sockaddr *) &clientname, &size);
if (newsock == INVALID_SOCKET) {                        /* error? */
    err = WSAGetLastError ();
    if (err != WSAEWOULDBLOCK)
        printf ("Sockets: accept error %d\n", err);
    return INVALID_SOCKET;
    }
if (ipaddr != NULL)
    *ipaddr = ntohl (clientname.sin_addr.s_addr);

sta = sim_setnonblock (newsock);                        /* set nonblocking */
if (sta == SOCKET_ERROR)                                /* fcntl error? */
    return sim_err_sock (newsock, "fcntl", 0);
return newsock;
}

int32 sim_check_conn (SOCKET sock, t_bool rd)
{
fd_set rw_set, er_set;
fd_set *rw_p = &rw_set;
fd_set *er_p = &er_set;
struct timeval tz;

timerclear (&tz);
FD_ZERO (rw_p);
FD_ZERO (er_p);
FD_SET (sock, rw_p);
FD_SET (sock, er_p);
if (rd)
    select ((int) sock + 1, rw_p, NULL, er_p, &tz);
else select ((int) sock + 1, NULL, rw_p, er_p, &tz);
if (FD_ISSET (sock, rw_p))
    return 1;
if (FD_ISSET (sock, er_p))
    return -1;
return 0;
}

int32 sim_read_sock (SOCKET sock, char *buf, int32 nbytes)
{
int32 rbytes, err;

rbytes = recv (sock, buf, nbytes, 0);
if (rbytes == 0)                                        /* disconnect */
    return -1;
if (rbytes == SOCKET_ERROR) {
    err = WSAGetLastError ();
    if (err == WSAEWOULDBLOCK)                          /* no data */
        return 0;
    printf ("Sockets: read error %d\n", err);
    return -1;
    }
return rbytes;
}

int32 sim_write_sock (SOCKET sock, char *msg, int32 nbytes)
{
return send (sock, msg, nbytes, 0);
}

void sim_close_sock (SOCKET sock, t_bool master)
{
#if defined (_WIN32)
closesocket (sock);
if (master) {
    sim_sock_cnt = sim_sock_cnt - 1;
    if (sim_sock_cnt <= 0) {
        WSACleanup ();
        sim_sock_cnt = 0;
        }
    }
#else
close (sock);
#endif
return;
}

#if defined (_WIN32)                                    /* Windows */
SOCKET sim_setnonblock (SOCKET sock)
{
unsigned long non_block = 1;

return ioctlsocket (sock, FIONBIO, &non_block);         /* set nonblocking */
}

#elif defined (VMS)                                     /* VMS */
SOCKET sim_setnonblock (SOCKET sock)
{
int non_block = 1;

return ioctl (sock, FIONBIO, &non_block);               /* set nonblocking */
}

#else                                                   /* Mac, Unix, OS/2 */
int32 sim_setnonblock (SOCKET sock)
{
int32 fl, sta;

fl = fcntl (sock, F_GETFL,0);                           /* get flags */
if (fl == -1)
    return SOCKET_ERROR;
sta = fcntl (sock, F_SETFL, fl | O_NONBLOCK);           /* set nonblock */
if (sta == -1)
    return SOCKET_ERROR;
#if !defined (macintosh) && !defined (__EMX__)          /* Unix only */
sta = fcntl (sock, F_SETOWN, getpid());                 /* set ownership */
if (sta == -1)
    return SOCKET_ERROR;
#endif
return 0;
}

#endif                                                  /* endif !Win32 && !VMS */

#endif                                                  /* end else !implemented */
