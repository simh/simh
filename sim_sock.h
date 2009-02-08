/* sim_sock.h: OS-dependent socket routines header file

   Copyright (c) 2001-2008, Robert M Supnik

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

   04-Jun-08    RMS     Addes sim_create_sock, for IBM 1130
   14-Apr-05    RMS     Added WSAEINPROGRESS (from Tim Riker)
   20-Aug-04    HV      Added missing definition for OS/2 (from Holger Veit)
   22-Oct-03    MP      Changed WIN32 winsock include to use winsock2.h to
                        avoid a conflict if sim_sock.h and sim_ether.h get
                        included by the same module.
   20-Mar-03    RMS     Added missing timerclear definition for VMS (from
                        Robert Alan Byer)
   15-Feb-03    RMS     Added time.h for EMX (from Holger Veit)
   17-Dec-02    RMS     Added sim_connect_sock
   08-Oct-02    RMS     Revised for .NET compatibility
   20-Aug-02    RMS     Changed calling sequence for sim_accept_conn
   30-Apr-02    RMS     Changed VMS stropts include to ioctl
   06-Feb-02    RMS     Added VMS support from Robert Alan Byer
   16-Sep-01    RMS     Added Macintosh support from Peter Schorn
*/

#ifndef _SIM_SOCK_H_
#define _SIM_SOCK_H_    0

#if defined (_WIN32)                                    /* Windows */
#undef INT_PTR                                          /* hack, hack */
#include <winsock2.h>

#elif !defined (__OS2__) || defined (__EMX__)           /* VMS, Mac, Unix, OS/2 EMX */
#define WSAGetLastError()       errno                   /* Windows macros */
#define SOCKET          int32
#define WSAEWOULDBLOCK  EWOULDBLOCK
#define WSAEINPROGRESS  EINPROGRESS
#define INVALID_SOCKET  -1 
#define SOCKET_ERROR    -1
#include <sys/types.h>                                  /* for fcntl, getpid */
#include <sys/socket.h>                                 /* for sockets */
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>                                 /* for sockaddr_in */
#include <netdb.h>
#include <sys/time.h>                                   /* for EMX */
#endif

#if defined (VMS)                                       /* VMS unique */
#include <ioctl.h>                                      /* for ioctl */
#if !defined (timerclear)
#define timerclear(tvp)         (tvp)->tv_sec = (tvp)->tv_usec = 0
#endif
#endif
#if defined(__EMX__)                                    /* OS/2 unique */
#if !defined (timerclear)
#define timerclear(tvp)         (tvp)->tv_sec = (tvp)->tv_usec = 0
#endif
#endif

SOCKET sim_master_sock (int32 port);
SOCKET sim_connect_sock (int32 ip, int32 port);
SOCKET sim_create_sock (void);
SOCKET sim_accept_conn (SOCKET master, uint32 *ipaddr);
int32 sim_check_conn (SOCKET sock, t_bool rd);
int32 sim_read_sock (SOCKET sock, char *buf, int32 nbytes);
int32 sim_write_sock (SOCKET sock, char *msg, int32 nbytes);
void sim_close_sock (SOCKET sock, t_bool master);
SOCKET sim_setnonblock (SOCKET sock);

#endif
