/* scp_sock.c: OS-dependent socket routines

   Copyright (c) 2001, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   22-May-02	RMS	Added OS2 EMX support from Holger Veit
   06-Feb-02	RMS	Added VMS support from Robert Alan Byer
   16-Sep-01	RMS	Added Macintosh support from Peter Schorn
   02-Sep-01	RMS	Fixed UNIX bugs found by Mirian Lennox and Tom Markson
*/

#include "sim_defs.h"
#include "sim_sock.h"
#include <signal.h>

/* OS dependent routines

   sim_master_sock	create master socket
   sim_accept_conn	accept connection
   sim_read_sock	read from socket
   sim_write_sock	write from socket
   sim_close_sock	close socket
   sim_setnonblock	set socket non-blocking
   sim_msg_sock		send message to socket
*/

/* First, all the non-implemented versions */

#if defined (__OS2__) && !defined (__EMX__)

SOCKET sim_master_sock (int32 port)
{
return INVALID_SOCKET;
}

SOCKET sim_accept_conn (SOCKET master, UNIT *uptr)
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

void sim_close_sock (SOCKET sock)
{
return;
}

SOCKET sim_setnonblock (SOCKET sock)
{
return SOCKET_ERROR;
}

#else							/* endif unimpl */

/* UNIX, Win32, Macintosh, VMS, OS2 (Berkeley socket) routines */

SOCKET sim_master_sock (int32 port)
{
SOCKET newsock;
struct sockaddr_in name;
int32 sta;

#if defined (_WIN32)
WORD wVersionRequested; 
WSADATA wsaData; 
wVersionRequested = MAKEWORD(1, 1); 
 
sta = WSAStartup(wVersionRequested, &wsaData);		/* start Winsock */ 
if (sta != 0) {
	printf ("Winsock: startup error %d\n", sta);
	return sta;  }
#endif							/* endif Win32 */

newsock = socket (AF_INET, SOCK_STREAM, 0);		/* create socket */
if (newsock == INVALID_SOCKET) {			/* socket error? */
	perror ("Sockets: socket error");
	return INVALID_SOCKET;  }

name.sin_family = AF_INET;				/* name socket */
name.sin_port = htons ((unsigned short) port);		/* insert port */
name.sin_addr.s_addr = htonl (INADDR_ANY);
sta = bind (newsock, (struct sockaddr *) &name, sizeof (name));
if (sta == SOCKET_ERROR) {				/* bind error? */
	perror ("Sockets: bind error");
	sim_close_sock (newsock, 1);
	return INVALID_SOCKET;  }

sta = sim_setnonblock (newsock);			/* set nonblocking */
if (sta == SOCKET_ERROR) {				/* fcntl error? */
	perror ("Sockets: fcntl error");
	sim_close_sock (newsock, 1);
	return INVALID_SOCKET;  }

sta = listen (newsock, 1);				/* listen on socket */
if (sta == SOCKET_ERROR) {				/* listen error? */
	perror ("Sockets: listen error");
	sim_close_sock (newsock, 1);
	return INVALID_SOCKET;  }
return newsock;						/* got it! */
}

SOCKET sim_accept_conn (SOCKET master, UNIT *uptr, uint32 *ipaddr)
{
int32 sta;
#if defined (macintosh) 
socklen_t size;
#elif defined (__EMX__)
int size;
#else 
size_t size; 
#endif
SOCKET newsock;
struct sockaddr_in clientname;

if ((uptr -> flags & UNIT_ATT) == 0)			/* not attached? */
	 return INVALID_SOCKET;
size = sizeof (clientname);
newsock = accept (master, (struct sockaddr *) &clientname, &size);
if (newsock == INVALID_SOCKET) {			/* error? */
	if (WSAGetLastError () != WSAEWOULDBLOCK)
		perror ("Sockets: accept error");
	return INVALID_SOCKET;  }
if (ipaddr != NULL) *ipaddr = ntohl (clientname.sin_addr.s_addr);

sta = sim_setnonblock (newsock);			/* set nonblocking */
if (sta == SOCKET_ERROR) {				/* fcntl error? */
	perror ("Sockets: fcntl error");
	sim_close_sock (newsock, 0);
	return INVALID_SOCKET;  }
return newsock;
}

int32 sim_read_sock (SOCKET sock, char *buf, int32 nbytes)
{
int32 rbytes;

rbytes = recv (sock, buf, nbytes, 0);
if (rbytes == 0) return -1;				/* disconnect */
if (rbytes == SOCKET_ERROR) {
	if (WSAGetLastError () == WSAEWOULDBLOCK) return 0;	/* no data */
	perror("Sockets: read error");
      	return -1;  }
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
if (master) WSACleanup ();
#else
close (sock);
#endif
return;
}

#if defined (_WIN32)
SOCKET sim_setnonblock (SOCKET sock)
{
unsigned long non_block = 1;

return ioctlsocket (sock, FIONBIO, &non_block);		/* set nonblocking */
}
#elif defined (VMS)
SOCKET sim_setnonblock (SOCKET sock)
{
int non_block = 1;

return ioctl (sock, FIONBIO, &non_block);		/* set nonblocking */
}
#else
int32 sim_setnonblock (SOCKET sock)
{
int32 fl, sta;

fl = fcntl (sock, F_GETFL,0);				/* get flags */
if (fl == -1) return SOCKET_ERROR;
sta = fcntl (sock, F_SETFL, fl | O_NONBLOCK);		/* set nonblock */
if (sta == -1) return SOCKET_ERROR;
#if !defined (macintosh) && !defined (__EMX__)
sta = fcntl (sock, F_SETOWN, getpid());			/* set ownership */
if (sta == -1) return SOCKET_ERROR;
#endif
return 0;
}
#endif							/* endif !Win32 */

#endif							/* endif Win32/UNIX/Mac/VMS */
