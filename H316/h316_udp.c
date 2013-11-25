/* h316_udp.c: IMP/TIP Modem and Host Interface socket routines using UDP

   Copyright (c) 2013 Robert Armstrong, bob@jfcl.com

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
   ROBERT ARMSTRONG BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert Armstrong shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert Armstrong.


   REVISION HISTORY

   udp         socket routines

   26-Jun-13    RLA     Rewritten from TCP version


   OVERVIEW

   This module emulates low level communications between two virtual modems
   using UDP packets over the modern network connections.  It's used by both 
   the IMP modem interface and the host interface modules to implement IMP to 
   IMP and IMP to HOST connections.

   TCP vs UDP

   Why UDP and not TCP?  TCP has a couple of advantages after all - it's
   stream oriented, which is intrinsically like a modem, and it handles all
   the network "funny stuff" for us.  TCP has a couple of problems too - first,
   it's inherently asymmetrical.  There's a "server" end which opens a master
   socket and passively listens for connections, and a "client" end which
   actively attempts to connect.  That's annoying, but it can be worked around.

   The big problem with TCP is that even though it treats the data like a stream
   it's internally buffering it, and you really have absolutely no control over
   when TCP will decide to send its buffer.  Google "nagle algorithm" to get an
   idea.  Yes, you can set TCP_NODELAY to disable Nagle, but the data's still
   buffered and it doesn't fix the problem.  What's the issue with buffering?
   It introduces completely unpredictable delays into the message traffic.  A
   transmitting IMP could send two or three (or ten or twenty!) messages before
   TCP actually decides to try to deliver them to the destination.

   And it turns out that IMPs are extraordinarily sensitive to line speed.  The
   IMP firmware actually goes to the trouble of measuring the effective line
   speed by using the RTC to figure out how long it takes to send a message.
   One thing that screws up the IMP to no end is variation in the effective
   line speed.  I guess they had a lot of trouble with AT&T Long Lines back in
   the Old Days, and the IMP has quite a bit of code to monitor line quality.
   Even fairly minor variations in speed will cause it to mark the line as
   "down" and sent a trouble report back to BBN. And no, I'm not making this up!

   UDP gives us a few advantages.  First, it's inherently packet oriented so
   we can simply grab the entire packet from the transmitting IMP's memory, wrap
   a little extra information around it, and ship it off in one datagram.  The
   receiving IMP gets the whole packet at once and it can simply BLT it into
   the receiving IMP's memory.  No fuss, no muss, no need convert the packet
   into a stream, add word counts, wait for complete packets, etc.  And UDP is
   symmetrical - both ends listen and send in the same way. There's no need for
   master sockets, passive (server) and active (client) ends, or any of that.

   Also UDP has no buffering - the packet goes out on the wire when we send it.
   The data doesn't wait around in some buffer for TCP to decide when it wants
   to let it go.  The latency and delay for UDP is much more predictable and
   consistent, at least for local networks.  If you're actually sending the
   packets out on the big, wide, Internet then all bets are off on that.

   UDP has a few problems that we have to worry about.  First, it's not
   guaranteed delivery so just because one IMP sends a packet doesn't mean that
   the other end will ever see it.  Surprisingly that's not a problem for us.
   Phone lines have noise and dropouts, and real modems lose packets too.  The
   IMP code is completely happy and able to deal with that, and generally we
   don't worry about dropped packets at all.

   There are other issues with UDP - it doesn't guarantee packet order, so the
   sending IMP might transmit packets 1, 2 and 3 and the receiving IMP will get
   1, 3 then 2.  THAT would never happen with a real modem and we have to shield
   the IMP code from any such eventuality.  Also, with UDP packets can be 
   duplicated so the receiving IMP might see 1, 2, 2, 3 (or even 1, 3, 2, 1!).
   Again, a modem would never do that and we have to prevent it from happening.
   Both cases are easily dealt with by adding a sequence number to the header
   we wrap around the IMP's packet.  Out of sequence or duplicate packets can
   be detected and are simply dropped.  If necessary, the IMP will deal with
   retransmitting them in its own time.

   One more thing about UDP - there is no way to tell whether a connection is
   established or not and for that matter there is no "connection" at all 
   (that's why it's a "connectionless" protocol, after all!).  We simply send
   packets out and there's no way to know whether anybody is hearing them. The
   real IMP modem hardware had no carrier detect or other dataset control
   functions, so it was identical in that respect. An IMP sent messages out the
   modem and, unless it received a message back, it had no way to know whether
   the IMP on the other end was hearing them.  


   INTERFACE

   This module provides a simplified UDP socket interface.  These functions are
   implemented -

        udp_create      define a connection to the remote IMP
        udp_release     release a connection
        udp_send        send an IMP message to the other end
        udp_receive     receive (w/o blocking!) a message if available

   Note that each connection is assigned a unique "handle", a small integer,
   which is used as an index into our internal connection data table.  There
   is a limit on the maximum number of connections available, as set my the
   MAXLINKS parameter.  Also, notice that all links are intrinsically full
   duplex and bidirectional - data can be sent and received in both directions
   independently.  Real modems and host cards were exactly the same.

   One last comment - there's a nice sim_sock module which provides platform
   independent TCP functions. Unfortunately there is no UDP equivalent, and this
   module doesn't use sim_sock.  Sorry.  Even more unfortunate is that the
   current implementation is WIN32/WINSOCK specific.  Sorry again.  There's no
   reason why it couldn't be ported to other platforms, but somebody will have
   to write the missing code.
*/
#ifdef VM_IMPTIP
#include "sim_defs.h"           // simh machine independent definitions
#ifdef _WIN32                   // WINSOCK definitions
#include <winsock2.h>           //   at least Windows puts it all in one file!
#elif defined(__linux__)        // Linux definitions
#include <sys/socket.h>         // struct socketaddr_in, et al
#include <netinet/in.h>         // INADDR_NONE, et al
#include <netdb.h>              // gethostbyname()
#include <fcntl.h>              // fcntl() (what else??)
#include <unistd.h>             // getpid(), more?
#endif
#include "h316_defs.h"          // H316 emulator definitions
#include "h316_imp.h"           // ARPAnet IMP/TIP definitions

// Local constants ...
#define MAXLINKS        10      // maximum number of simultaneous connections
//   This constant determines the longest possible IMP data payload that can be
// sent. Most IMP messages are trivially small - 68 words or so - but, when one
// IMP asks for a reload the neighbor IMP sends the entire memory image in a
// single message!  That message is about 14K words long.
//   The next thing you should worry about is whether the underlying IP network
// can actually send a UDP packet of this size.  It turns out that there's no
// simple answer to that - it'll be fragmented for sure, but as long as all
// the fragments arrive intact then the destination should reassemble them.
#define MAXDATA      16384      // longest possible IMP packet (in H316 words)

// Compatibility hacks for WINSOCK vs Linux ...
#ifdef __linux__
#define WSAGetLastError()       errno
#define closesocket             close
#define SOCKET                  int32
#define SOCKADDR                struct sockaddr
#define WSAEWOULDBLOCK          EWOULDBLOCK
#define INVALID_SOCKET          ((SOCKET) (-1))
#define SOCKET_ERROR            (-1)
#endif

// UDP connection data structure ...
//   One of these blocks is allocated for every simulated modem link. 
struct _UDP_LINK {
  t_bool  used;                 // TRUE if this UDP_LINK is in use
  uint32  ipremote;             // IP address of the remote system
  uint16  rxport;               // OUR receiving port number
  uint16  txport;               // HIS receiving port number (on ipremote)
  struct sockaddr_in rxaddr;    // OUR receiving address (goes with rxsock!)
  struct sockaddr_in txaddr;    // HIS transmitting address (pairs with txsock!)
  SOCKET  rxsock;               // socket for receiving incoming packets
  SOCKET  txsock;               // socket for sending outgoing packets
  uint32  rxsequence;           // next message sequence number for receive
  uint32  txsequence;           // next message sequence number for transmit
};
typedef struct _UDP_LINK UDP_LINK;

//   This magic number is stored at the beginning of every UDP message and is
// checked on receive.  It's hardly foolproof, but its a simple attempt to
// guard against other applications dumping unsolicited UDP messages into our
// receiver socket...
#define MAGIC   ((uint32) (((((('H' << 8) | '3') << 8) | '1') << 8) | '6'))

// UDP wrapper data structure ...
//   This is the UDP packet which is actually transmitted or received.  It
// contains the actual IMP packet, plus whatever additional information we
// need to keep track of things.  NOTE THAT ALL DATA IN THIS PACKET, INCLUDING
// THE H316 MEMORY WORDS, ARE SENT AND RECEIVED WITH NETWORK BYTE ORDER!
struct _UDP_PACKET {
  uint32  magic;                // UDP "magic number" (see above)
  uint32  sequence;             // UDP packet sequence number
  uint16  count;                // number of H316 words to follow
  uint16  data[MAXDATA];        // and the actual H316 data words/IMP packet
};
typedef struct _UDP_PACKET UDP_PACKET;
#define UDP_HEADER_LEN  (2*sizeof(uint32) + sizeof(uint16))

// Locals ...
t_bool udp_wsa_started = FALSE;         // TRUE if WSAStartup() has been called
UDP_LINK udp_links[MAXLINKS] = {0};     // data for every active connection


t_stat udp_startup (DEVICE *dptr)
{
  //   WINSOCK requires that WSAStartup be called exactly once before any other
  // network calls are made.  That's a bit inconvenient, but this routine deals
  // with it by using a static variable to call WSAStartup the first time thru
  // and then never again.
#ifdef _WIN32
  WORD wVersionRequested = MAKEWORD(2,2);
  WSADATA wsaData;  int32 ret;
  if (!udp_wsa_started) {
    ret = WSAStartup (wVersionRequested, &wsaData);
    if (ret != 0) {
      fprintf(stderr,"UDP - WINSOCK startup error %d\n", ret);
      return SCPE_IERR;
    } else
      sim_debug(IMP_DBG_UDP, dptr, "WSAStartup() called\n");
    udp_wsa_started = TRUE;
  }
#endif
  return SCPE_OK;
}

t_stat udp_shutdown (DEVICE *dptr)
{
  //   This routine calls WSACleanup() after the last socket has been closed.
  // It's essentially the opposite of udp_startup() ...
#ifdef _WIN32
  if (udp_wsa_started) {
    WSACleanup();  udp_wsa_started = FALSE;
    sim_debug(IMP_DBG_UDP, dptr, "WSACleanup() called\n");
  }
#endif
  return SCPE_OK;
}

int32 udp_find_free_link (void)
{
  //   Find a free UDP_LINK block, initialize it and return its index.  If none
  // are free, then return -1 ...
  int32 i;
  for (i = 0;  i < MAXLINKS;  ++i) {
    if (udp_links[i].used == 0) {
      memset(&udp_links[i], 0, sizeof(UDP_LINK));
      // Just in case these values aren't zero!
      udp_links[i].rxsock = udp_links[i].txsock = INVALID_SOCKET;
      return i;
    }
  }
  return NOLINK;
}

char *udp_format_remote (int32 link)
{
  //   Format the remote address and port in the format "w.x.y.z:pppp" .  It's
  // a bit ugly (OK, it's a lot ugly!) but it's just for error messages...
  static char buf[64];
  sprintf(buf, "%d.%d.%d.%d:%d",
    (udp_links[link].ipremote >> 24) & 0xFF,
    (udp_links[link].ipremote >> 16) & 0xFF,
    (udp_links[link].ipremote >>  8) & 0xFF,
     udp_links[link].ipremote        & 0xFF,
     udp_links[link].txport);
  return buf;
}

/* get_ipaddr           IP address:port

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        ipa     =       pointer to IP address (may be NULL), 0 = none
        ipp     =       pointer to IP port (may be NULL), 0 = none
        result  =       status
*/

static t_stat get_ipaddr (char *cptr, uint32 *ipa, uint32 *ipp)
{
char gbuf[CBUFSIZE];
char *addrp, *portp, *octetp;
uint32 i, addr, port, octet;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
strncpy (gbuf, cptr, CBUFSIZE);
addrp = gbuf;                                           /* default addr */
if ((portp = strchr (gbuf, ':')))                       /* x:y? split */
    *portp++ = 0;
else if (strchr (gbuf, '.'))                            /* x.y...? */
    portp = NULL;
else {
    portp = gbuf;                                       /* port only */
    addrp = NULL;                                       /* no addr */
    }
if (portp) {                                            /* port string? */
    if (ipp == NULL)                                    /* not wanted? */
        return SCPE_ARG;
    port = (int32) get_uint (portp, 10, 65535, &r);
    if ((r != SCPE_OK) || (port == 0))
        return SCPE_ARG;
    }
else port = 0;
if (addrp) {                                            /* addr string? */
    if (ipa == NULL)                                    /* not wanted? */
        return SCPE_ARG;
    for (i = addr = 0; i < 4; i++) {                    /* four octets */
        octetp = strchr (addrp, '.');                   /* find octet end */
        if (octetp != NULL)                             /* split string */
            *octetp++ = 0;
        else if (i < 3)                                 /* except last */
            return SCPE_ARG;
        octet = (int32) get_uint (addrp, 10, 255, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        addr = (addr << 8) | octet;
        addrp = octetp;
        }
    if (((addr & 0377) == 0) || ((addr & 0377) == 255))
        return SCPE_ARG;
    }
else addr = 0;
if (ipp)                                                /* return req values */
    *ipp = port;
if (ipa)
    *ipa = addr;
return SCPE_OK;   
}

t_stat udp_parse_remote (int32 link, char *premote)
{
  // This routine will parse a remote address string in any of these forms -
  //
  //            llll:w.x.y.z:rrrr
  //            llll:name.domain.com:rrrr
  //            llll::rrrr
  //            w.x.y.z:rrrr
  //            name.domain.com:rrrr
  //
  // In all examples, "llll" is the local port number that we use for listening,
  // and "rrrr" is the remote port number that we use for transmitting.  The
  // local port is optional and may be omitted, in which case it defaults to the
  // same as the remote port.  This works fine if the other IMP is actually on
  // a different host, but don't try that with localhost - you'll be talking to
  // yourself!!  In both cases, "w.x.y.z" is a dotted IP for the remote machine
  // and "name.domain.com" is its name (which will be looked up to get the IP).
  // If the host name/IP is omitted then it defaults to "localhost".
  char *end, *colon;  int32 port;  struct hostent *he;
  if (*premote == '\0') return SCPE_2FARG;
  // Look for the local port number. If it's not there, set rxport to zero for now.
  port = strtoul(premote, &end, 10);  udp_links[link].rxport = 0;
  if ((*end == ':') && (port > 0)) {
    udp_links[link].rxport = port;  premote = end+1;
  }

  // Look for "name:port" and extract the remote port...
  if ((colon = strchr(premote, ':')) == NULL) return SCPE_ARG;
  *colon++ = '\0';  port = strtoul(colon, &end, 10);
  if ((*end != '\0') || (port == 0)) return SCPE_ARG;
  udp_links[link].txport = port;
  if (udp_links[link].rxport == 0) udp_links[link].rxport = port;

  // Now try to parse the host as a dotted IP address ...
  if (get_ipaddr(premote, &udp_links[link].ipremote, NULL) == SCPE_OK) return SCPE_OK;

  // Special kludge - allow just ":port" to mean "localhost:port" ...
  if(*premote == '\0') {
    if (udp_links[link].rxport == udp_links[link].txport)
      fprintf(stderr,"WARNING - use different transmit and receive ports!\n");
    premote = "localhost";
  }

  // Not a dotted IP - try to lookup a host name ...
  if ((he = gethostbyname(premote)) == NULL) return SCPE_OPENERR;
  udp_links[link].ipremote = * (unsigned long *) he->h_addr_list[0];
  if (udp_links[link].ipremote == INADDR_NONE) {
    fprintf(stderr,"WARNING - unable to resolve \"%s\"\n", premote);
    return SCPE_OPENERR;
  }
  udp_links[link].ipremote = ntohl(udp_links[link].ipremote);
  return SCPE_OK;
}

t_stat udp_socket_error (int32 link, const char *msg)
{
  // This routine is called whenever a SOCKET_ERROR is returned for any I/O.
  fprintf(stderr,"UDP%d - %s failed with error %d\n", link, msg, WSAGetLastError());
  return SCPE_IOERR;
}

t_stat udp_create_rx_socket (int32 link)
{
  //   This routine will create the receiver socket for the virtual modem.
  // Sockets are always UDP and, in the case of the receiver, bound to the port
  // specified.  Receiving sockets are also always set to NON BLOCKING mode.
  int32 iret;  uint32 flags = 1;

  // Creating the socket works on both Windows and Linux ...
  udp_links[link].rxsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_links[link].rxsock == INVALID_SOCKET)
    return udp_socket_error(link, "RX socket()");
  udp_links[link].rxaddr.sin_family = AF_INET;
  udp_links[link].rxaddr.sin_port = htons(udp_links[link].rxport);
  udp_links[link].rxaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  iret = bind(udp_links[link].rxsock, (SOCKADDR *) &udp_links[link].rxaddr, sizeof(struct sockaddr_in));
  if (iret != 0)
    return udp_socket_error(link, "bind()");

  // But making it non-blocking is a problem ...
#ifdef _WIN32
  iret = ioctlsocket(udp_links[link].rxsock, FIONBIO, (u_long *) &flags);
  if (iret != 0)
    return udp_socket_error(link, "ioctlsocket()");
#elif defined(__linux__)
  flags = fcntl(udp_links[link].rxsock, F_GETFL, 0);
  if (flags == -1) return udp_socket_error(link, "fcntl(F_GETFL)");
  iret = fcntl(udp_links[link].rxsock, F_SETFL, flags | O_NONBLOCK);
  if (iret == -1) return udp_socket_error(link, "fcntl(F_SETFL)");
  iret = fcntl(udp_links[link].rxsock, F_SETOWN, getpid());
  if (iret == -1) return udp_socket_error(link, "fcntl(F_SETOWN)");
#endif
  return SCPE_OK;
}

t_stat udp_create_tx_socket (int32 link)
{
  //   This routine will create the transmitter socket for the virtual modem.
  // In the case of the transmitter, we don't bind the socket at this time -
  // WINSOCK will automatically bind it for us to a free port on the first IO.
  // Also note that transmitting sockets are blocking; we don't have code (yet!)
  // to allow them to be nonblocking.
  udp_links[link].txsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_links[link].txsock == INVALID_SOCKET)
    return udp_socket_error(link, "TX socket()");

  //  Initialize the txaddr structure too - note that this isn't used now; it's
  // the sockaddr we will use when we later do a sendto() the remote host!
  udp_links[link].txaddr.sin_family = AF_INET;
  udp_links[link].txaddr.sin_port = htons(udp_links[link].txport);
  udp_links[link].txaddr.sin_addr.s_addr = htonl(udp_links[link].ipremote);
  return SCPE_OK;
}

t_stat udp_create (DEVICE *dptr, char *premote, int32 *pln)
{
  //   Create a logical UDP link to the specified remote system.  The "remote"
  // string specifies both the remote host name or IP and a port number.  The
  // port number is both the port we send datagrams to, and also the port we
  // listen on for incoming datagrams.  UDP doesn't have any real concept of a
  // "connection" of course, and this routine simply creates the necessary
  // sockets in this host. We have no way of knowing whether the remote host is
  // listening or even if it exists.
  //
  //   We return SCPE_OK if we're successful and an error code if we aren't. If
  // we are successful, then the ln parameter is assigned the link number,
  // which is a handle used to identify this connection to all future udp_xyz()
  //  calls.
  t_stat ret;
  int32 link = udp_find_free_link();
  if (link < 0) return SCPE_MEM;

  // Make sure WINSOCK is initialized ...
  if ((ret = udp_startup(dptr)) != SCPE_OK) return ret;

  // Parse the remote name and set up the ipaddr and port ...
  if ((ret = udp_parse_remote(link, premote)) != SCPE_OK) return ret;

  // Create the sockets for the transmitter and receiver ...
  if ((ret = udp_create_rx_socket(link)) != SCPE_OK) return ret;
  if ((ret = udp_create_tx_socket(link)) != SCPE_OK) return ret;

  // All done - mark the TCP_LINK data as "used" and return the index.
  udp_links[link].used = TRUE;  *pln = link;
  sim_debug(IMP_DBG_UDP, dptr, "link %d - listening on port %d and sending to %s\n", link, udp_links[link].rxport, udp_format_remote(link));
  return SCPE_OK;
}

t_stat udp_release (DEVICE *dptr, int32 link)
{
  //   Close a link that was created by udp_create() and release any resources
  // allocated to it.  We always return SCPE_OK unless the link specified is
  // already unused.
  int32 iret, i;
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;

  // Close the sockets associated with this connection - that's easy ...
  iret = closesocket(udp_links[link].rxsock);
  if (iret != 0) udp_socket_error(link, "closesocket()");
  iret = closesocket(udp_links[link].txsock);
  if (iret != 0) udp_socket_error(link, "closesocket()");
  udp_links[link].used = FALSE;
  sim_debug(IMP_DBG_UDP, dptr, "link %d - closed\n", link);

  // If we just closed the last link, then call udp_shutdown() ...
  for (i = 0;  i < MAXLINKS;  ++i) {
    if (udp_links[i].used) return SCPE_OK;
  }
  return udp_shutdown(dptr);
}

t_stat udp_send_to (DEVICE *dptr, int32 link, uint16 *pdata, uint16 count, SOCKADDR *pdest)
{
  //   This routine does all the work of sending an IMP data packet.  pdata
  // is a pointer (usually into H316 simulated memory) to the IMP packet data,
  // count is the length of the data (in H316 words, not bytes!), and pdest is
  // the destination socket.  There are two things worthy of note here - first,
  // notice that the H316 words are sent in network order, so the remote simh
  // doesn't necessarily need to have the same endian-ness as this machine.
  // Second, notice that transmitting sockets are NOT set to non blocking so
  // this routine might wait, but we assume the wait will never be too long.
  UDP_PACKET pkt;  int pktlen;  uint16 i;  int32 iret;
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;
  if ((pdata == NULL) || (count == 0) || (count > MAXDATA)) return SCPE_IERR;

  //   Build the UDP packet, filling in our own header information and copying
  // the H316 words from memory.  REMEMBER THAT EVERYTHING IS IN NETWORK ORDER!
  pkt.magic = htonl(MAGIC);
  pkt.sequence = htonl(udp_links[link].txsequence++);
  pkt.count = htons(count);
  for (i = 0;  i < count;  ++i)  pkt.data[i] = htons(*pdata++);
  pktlen = UDP_HEADER_LEN + count*sizeof(uint16);

  // Send it and we're outta here ...
  iret = sendto(udp_links[link].txsock, (const char *) &pkt, pktlen, 0, pdest, sizeof (struct sockaddr_in));
  if (iret == SOCKET_ERROR) return udp_socket_error(link, "sendto()");
  sim_debug(IMP_DBG_UDP, dptr, "link %d - packet sent (sequence=%d, length=%d)\n", link, ntohl(pkt.sequence), ntohs(pkt.count));
  return SCPE_OK;
}

t_stat udp_send (DEVICE *dptr, int32 link, uint16 *pdata, uint16 count)
{
  //   Send an IMP packet to the remote simh.  This is the usual case - the only
  // reason there's any other options at all is so we can emulate loopback.
  return udp_send_to (dptr, link, pdata, count, (SOCKADDR *) &(udp_links[link].txaddr));
}

t_stat udp_send_self (DEVICE *dptr, int32 link, uint16 *pdata, uint16 count)
{
  //   Send an IMP packet to our own receiving socket.  This might seem silly,
  // but it's used to emulate the line loopback function...
  struct sockaddr_in self;
  self.sin_family = AF_INET;
  self.sin_port = htons(udp_links[link].rxport);
  self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return udp_send_to (dptr, link, pdata, count, (SOCKADDR *) &self);
}

int32 udp_receive_packet (int32 link, UDP_PACKET *ppkt)
{
  //   This routine will do the hard part of receiving a UDP packet.  If it's
  // successful the packet length, in bytes, is returned.  The receiver socket
  // is non-blocking, so if no packet is available then zero will be returned
  // instead.  Lastly, if a fatal socket I/O error occurs, -1 is returned.
  //
  //   Note that this routine only receives the packet - it doesn't handle any
  // of the checking for valid packets, unexpected packets, duplicate or out of
  // sequence packets.  That's strictly the caller's problem!
  int32 pktsiz;
  struct sockaddr_in sender;
#if defined (macintosh) || defined (__linux) || defined (__linux__) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__) || \
    (defined(__hpux) && defined(_XOPEN_SOURCE_EXTENDED))
socklen_t sndsiz = (socklen_t)sizeof(sender);
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__)) || \
     defined (__hpux)
int sndsiz = (int)sizeof(sender);
#else 
size_t sndsiz = sizeof(sender); 
#endif

  pktsiz = recvfrom(udp_links[link].rxsock, (char *) ppkt, sizeof(UDP_PACKET),
    0, (SOCKADDR *) &sender, &sndsiz);
  if (pktsiz >= 0) return pktsiz;
  if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
  udp_socket_error(link, "recvfrom()");
  return NOLINK;
}

int32 udp_receive (DEVICE *dptr, int32 link, uint16 *pdata, uint16 maxbuf)
{
  //   Receive an IMP packet from the virtual modem. pdata is a pointer usually
  // directly into H316 simulated memory) to where the IMP packet data should
  // be stored, and maxbuf is the maximum length of that buffer in H316 words
  // (not bytes!).  If a message is successfully received then this routine
  // returns the length, again in H316 words, of the IMP packet.  The caller
  // can detect buffer overflows by comparing this result to maxbuf.  If no
  // packets are waiting right now then zero is returned, and -1 is returned
  // in the event of any fatal socket I/O error.
  //
  //  This routine also handles checking for unsolicited messages and duplicate
  // or out of sequence messages.  All of these are unceremoniously discarded.
  //
  //   One final note - it's explicitly allowed for pdata to be null and/or
  // maxbuf to be zero.  In either case the received package is discarded, but
  // the actual length of the discarded package is still returned.
  UDP_PACKET pkt;  int32 pktlen, explen, implen, i;  uint32 magic, pktseq;
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;

  while ((pktlen = udp_receive_packet(link, &pkt)) > 0) {
    // First do some header checks for a valid UDP packet ...
    if (pktlen < UDP_HEADER_LEN) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet w/o header (length=%d)\n", link, pktlen);
      continue;
    }
    magic = ntohl(pkt.magic);
    if (magic != MAGIC) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet w/bad magic number (magic=%08x)\n", link, magic);
      continue;
    }
    implen = ntohs(pkt.count);
    explen = UDP_HEADER_LEN + implen*sizeof(uint16);
    if (explen != pktlen) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet length wrong (expected=%d received=%d)\n", link, explen, pktlen);
      continue;
    }

    //  Now the hard part = check the sequence number.  The rxsequence value is
    // the number of the next packet we expect to receive - that's the number
    // this packet should have.  If this packet's sequence is less than that,
    // then this packet is out of order or a duplicate and we discard it.  If
    // this packet is greater than that, then we must have missed one or two
    // packets.  In that case we MUST update rxsequence to match this one;
    // otherwise the two ends could never resynchronize after a lost packet.
    pktseq = ntohl(pkt.sequence);
    if (pktseq < udp_links[link].rxsequence) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet out of sequence 1 (expected=%d received=%d\n", link, udp_links[link].rxsequence, pktseq);
      continue;
    }
    if (pktseq != udp_links[link].rxsequence) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet out of sequence 2 (expected=%d received=%d\n", link, udp_links[link].rxsequence, pktseq);
    }
    udp_links[link].rxsequence = pktseq+1;

    // It's a valid packet - if there's no buffer then just discard it.
    if ((pdata == NULL) || (maxbuf == 0)) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet discarded (no buffer available)\n", link);
      return implen;
    }

    // Copy the data to the H316 memory and we're done!
    sim_debug(IMP_DBG_UDP, dptr, "link %d - packet received (sequence=%d, length=%d)\n", link, pktseq, pktlen);
    for (i = 0;  i < (implen < maxbuf ? implen : maxbuf);  ++i)
      *pdata++ = ntohs(pkt.data[i]);
    return implen;
  }

  // Here if pktlen <= 0 ...
  return pktlen;
}

#endif // ifdef VM_IMPTIP
