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

   15-Oct-12    MP      Added definitions needed to detect possible tcp 
                        connect failures
   25-Sep-12    MP      Reworked for RFC3493 interfaces supporting IPv6 and IPv4
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

#ifndef SIM_SOCK_H_
#define SIM_SOCK_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

#if defined (_WIN32)                                    /* Windows */
#include <winsock2.h>
#include <winerror.h>

#elif !defined (__OS2__) || defined (__EMX__)           /* VMS, Mac, Unix, OS/2 EMX */
#include <sys/types.h>                                  /* for fcntl, getpid */
#include <sys/socket.h>                                 /* for sockets */
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>                                 /* for sockaddr_in */
#include <netinet/tcp.h>                                /* for TCP_NODELAY */
#include <arpa/inet.h>                                  /* for inet_addr and inet_ntoa */
#include <netdb.h>
#include <sys/time.h>                                   /* for EMX */

#define WSAGetLastError()       errno                   /* Windows macros */
#define WSASetLastError(err) errno = err
#define closesocket     close 
#define SOCKET          int
#if defined(__hpux)
#define WSAEWOULDBLOCK  EAGAIN
#else
#define WSAEWOULDBLOCK  EWOULDBLOCK
#endif
#define WSAENAMETOOLONG ENAMETOOLONG
#define WSAEINPROGRESS  EINPROGRESS
#define WSAETIMEDOUT    ETIMEDOUT
#define WSAEISCONN      EISCONN
#define WSAECONNRESET   ECONNRESET
#define WSAECONNREFUSED ECONNREFUSED
#define WSAECONNABORTED ECONNABORTED
#define WSAEHOSTUNREACH EHOSTUNREACH
#define WSAEADDRINUSE   EADDRINUSE
#if defined(EAFNOSUPPORT)
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#endif
#define WSAEACCES       EACCES
#define WSAEINTR        EINTR
#define INVALID_SOCKET  ((SOCKET)-1)
#if !defined(SOCKET_ERROR)
#define SOCKET_ERROR    (-1)
#endif
#endif

#if defined (VMS)                                       /* VMS unique */
#include <ioctl.h>                                      /* for ioctl */
#if !defined (AI_NUMERICHOST)
#define AI_NUMERICHOST 0
#endif
#if defined (__VAX)
#define sockaddr_storage sockaddr
#endif
#endif

#if !defined(CBUFSIZE)
#define CBUFSIZE 1024
#define sim_printf printf
#endif

int sim_parse_addr (const char *cptr, char *host, size_t hostlen, const char *default_host, char *port, size_t port_len, const char *default_port, const char *validate_addr);
int sim_parse_addr_ex (const char *cptr, char *host, size_t hostlen, const char *default_host, char *port, size_t port_len, char *localport, size_t local_port_len, const char *default_port);
#define SIM_SOCK_OPT_REUSEADDR      0x0001
#define SIM_SOCK_OPT_DATAGRAM       0x0002
#define SIM_SOCK_OPT_NODELAY        0x0004
#define SIM_SOCK_OPT_BLOCKING       0x0008
SOCKET sim_master_sock_ex (const char *hostport, int *parse_status, int opt_flags);
#define sim_master_sock(hostport, parse_status) sim_master_sock_ex(hostport, parse_status, ((sim_switches & SWMASK ('U')) ? SIM_SOCK_OPT_REUSEADDR : 0))
SOCKET sim_connect_sock_ex (const char *sourcehostport, const char *hostport, const char *default_host, const char *default_port, int opt_flags);
#define sim_connect_sock(hostport, default_host, default_port) sim_connect_sock_ex(NULL, hostport, default_host, default_port, 0)
SOCKET sim_accept_conn_ex (SOCKET master, char **connectaddr, int opt_flags);
#define sim_accept_conn(master, connectaddr) sim_accept_conn_ex(master, connectaddr, 0)
int sim_check_conn (SOCKET sock, int rd);
int sim_read_sock (SOCKET sock, char *buf, int nbytes);
int sim_write_sock (SOCKET sock, const char *msg, int nbytes);
void sim_close_sock (SOCKET sock);
const char *sim_get_err_sock (const char *emsg);
SOCKET sim_err_sock (SOCKET sock, const char *emsg);
int sim_getnames_sock (SOCKET sock, char **socknamebuf, char **peernamebuf);
void sim_init_sock (void);
void sim_cleanup_sock (void);

#ifdef  __cplusplus
}
#endif

#endif
