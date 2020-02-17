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
   26-Nov-13    MP      Rewritten to use TMXR layer packet semantics thus
                        allowing portability to all simh hosts.
    2-Dec-13    RLA     Improve error recovery if the other simh is restarted

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

*/
#ifdef VM_IMPTIP
#include "sim_defs.h"           // simh machine independent definitions
#include "sim_tmxr.h"           // The MUX layer exposes packet send and receive semantics
#include "h316_defs.h"          // H316 emulator definitions
#include "h316_imp.h"           // ARPAnet IMP/TIP definitions

// Local constants ...
#define MAXLINKS        10      // maximum number of simultaneous connections
// UDP connection data structure ...
//   One of these blocks is allocated for every simulated modem link. 
struct _UDP_LINK {
  t_bool  used;                 // TRUE if this UDP_LINK is in use
  char    rhostport[90];        // Remote host:port
  char    lport[64];            // Local port 
  uint32  rxsequence;           // next message sequence number for receive
  uint32  txsequence;           // next message sequence number for transmit
  DEVICE  *dptr;                // Device associated with link
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
UDP_LINK udp_links[MAXLINKS] = { {0} };         // data for every active connection
TMLN udp_lines[MAXLINKS] = { {0} };             // line descriptors
TMXR udp_tmxr = { MAXLINKS, NULL, 0, udp_lines};// datagram mux

int32 udp_find_free_link (void)
{
  //   Find a free UDP_LINK block, initialize it and return its index.  If none
  // are free, then return -1 ...
  int32 i;
  for (i = 0;  i < MAXLINKS;  ++i) {
    if (udp_links[i].used == 0) {
      memset(&udp_links[i], 0, sizeof(UDP_LINK));
      return i;
    }
  }
  return NOLINK;
}

t_stat udp_parse_remote (int32 link, const char *premote)
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
  char *end;  int32 lport, rport;
  char host[64], port[16];

  if (*premote == '\0') return SCPE_2FARG;
  memset (udp_links[link].lport, 0, sizeof(udp_links[link].lport));
  memset (udp_links[link].rhostport, 0, sizeof(udp_links[link].rhostport));
  // Handle the llll::rrrr case first
  if (2 == sscanf (premote, "%d::%d", &lport, &rport)) {
    if ((lport < 1) || (lport >65535) || (rport < 1) || (rport >65535)) return SCPE_ARG;
    snprintf (udp_links[link].lport, sizeof (udp_links[link].lport) - 1, "%d", lport);
    snprintf (udp_links[link].rhostport, sizeof (udp_links[link].rhostport) - 1, "localhost:%d", rport);
    return SCPE_OK;
  }

  // Look for the local port number and save it away.
  lport = strtoul(premote, &end, 10);
  if ((*end == ':') && (lport > 0)) {
    snprintf (udp_links[link].lport, sizeof (udp_links[link].lport) - 1, "%d", lport);
    premote = end+1;
  }

  if (sim_parse_addr (premote, host, sizeof(host), "localhost", port, sizeof(port), NULL, NULL))
    return SCPE_ARG;
  snprintf (udp_links[link].rhostport, sizeof (udp_links[link].rhostport) - 1, "%s:%s", host, port);
  if (udp_links[link].lport[0] == '\0')
    strlcpy (udp_links[link].lport, port, sizeof (udp_links[link].lport));

  if ((strcmp (udp_links[link].lport, port) == 0) && 
      (strcmp ("localhost", host) == 0))
    return sim_messagef (SCPE_ARG, "WARNING - use different transmit and receive ports!\n");

  return SCPE_OK;
}

t_stat udp_create (DEVICE *dptr, const char *premote, int32 *pln)
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
  char linkinfo[256];
  int32 link = udp_find_free_link();
  if (link < 0) return SCPE_MEM;

  // Parse the remote name and set up the ipaddr and port ...
  if ((ret = udp_parse_remote(link, premote)) != SCPE_OK) return ret;

  // Create the socket connection to the destination ...
  sprintf(linkinfo, "Buffer=%d,Line=%d,%s,UDP,Connect=%s", (int)(sizeof(UDP_PACKET)+sizeof(int32)), link, udp_links[link].lport, udp_links[link].rhostport);
  ret = tmxr_open_master (&udp_tmxr, linkinfo);
  if (ret != SCPE_OK) return ret;

  // All done - mark the TCP_LINK data as "used" and return the index.
  udp_links[link].used = TRUE;  *pln = link;
  udp_lines[link].dptr = udp_links[link].dptr = dptr;      // save device
  udp_tmxr.uptr = dptr->units;
  udp_tmxr.last_poll_time = 1;          // h316's use of TMXR doesn't poll periodically for connects
  (void)tmxr_poll_conn (&udp_tmxr);     // force connection initialization now
  udp_tmxr.last_poll_time = 1;          // h316's use of TMXR doesn't poll periodically for connects
  sim_debug(IMP_DBG_UDP, dptr, "link %d - listening on port %s and sending to %s\n", link, udp_links[link].lport, udp_links[link].rhostport);
  return SCPE_OK;
}

t_stat udp_release (DEVICE *dptr, int32 link)
{
  //   Close a link that was created by udp_create() and release any resources
  // allocated to it.  We always return SCPE_OK unless the link specified is
  // already unused.
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;
  if (dptr != udp_links[link].dptr) return SCPE_IERR;

  tmxr_detach_ln (&udp_lines[link]);
  udp_links[link].used = FALSE;
  sim_debug(IMP_DBG_UDP, dptr, "link %d - closed\n", link);

  return SCPE_OK;
}

t_stat udp_send (DEVICE *dptr, int32 link, uint16 *pdata, uint16 count)
{
  //   This routine does all the work of sending an IMP data packet.  pdata
  // is a pointer (usually into H316 simulated memory) to the IMP packet data,
  // count is the length of the data (in H316 words, not bytes!), and pdest is
  // the destination socket.  There are two things worthy of note here - first,
  // notice that the H316 words are sent in network order, so the remote simh
  // doesn't necessarily need to have the same endian-ness as this machine.
  // Second, notice that transmitting sockets are NOT set to non blocking so
  // this routine might wait, but we assume the wait will never be too long.
  UDP_PACKET pkt;  int pktlen;  uint16 i;  t_stat iret;
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;
  if ((pdata == NULL) || (count == 0) || (count > MAXDATA)) return SCPE_IERR;
  if (dptr != udp_links[link].dptr) return SCPE_IERR;

  //   Build the UDP packet, filling in our own header information and copying
  // the H316 words from memory.  REMEMBER THAT EVERYTHING IS IN NETWORK ORDER!
  pkt.magic = htonl(MAGIC);
  pkt.sequence = htonl(udp_links[link].txsequence++);
  pkt.count = htons(count);
  for (i = 0;  i < count;  ++i)  pkt.data[i] = htons(*pdata++);
  pktlen = UDP_HEADER_LEN + count*sizeof(uint16);

  // Send it and we're outta here ...
  iret = tmxr_put_packet_ln (&udp_lines[link], (const uint8 *)&pkt, (size_t)pktlen);
  if (iret != SCPE_OK) return sim_messagef(iret, "UDP%d - tmxr_put_packet_ln() failed with error %s\n", link, sim_error_text(iret));
  sim_debug(IMP_DBG_UDP, dptr, "link %d - packet sent (sequence=%d, length=%d)\n", link, ntohl(pkt.sequence), ntohs(pkt.count));
  return SCPE_OK;
}

t_stat udp_set_link_loopback (DEVICE *dptr, int32 link, t_bool enable_loopback)
{
  // Enable or disable the local (interface) loopback on this link...
  if ((link < 0) || (link >= MAXLINKS)) return SCPE_IERR;
  if (!udp_links[link].used) return SCPE_IERR;
  if (dptr != udp_links[link].dptr) return SCPE_IERR;

  return tmxr_set_line_loopback (&udp_lines[link], enable_loopback);
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
  size_t pktsiz;
  const uint8 *pbuf;
  t_stat ret;

  udp_lines[link].rcve = TRUE;          // Enable receiver
  tmxr_poll_rx (&udp_tmxr);
  ret = tmxr_get_packet_ln (&udp_lines[link], &pbuf, &pktsiz);
  udp_lines[link].rcve = FALSE;          // Disable receiver
  if (ret != SCPE_OK) {
    sim_messagef (ret, "UDP%d - tmxr_get_packet_ln() failed with error %s\n", link, sim_error_text(ret));
    return NOLINK;
  }
  if (pbuf == NULL) return 0;
  // Got a packet, so copy it to the packet buffer
  memcpy (ppkt, pbuf, pktsiz);
  return pktsiz;
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
  if (dptr != udp_links[link].dptr) return SCPE_IERR;

  while ((pktlen = udp_receive_packet(link, &pkt)) > 0) {
    // First do some header checks for a valid UDP packet ...
    if (((size_t)pktlen) < UDP_HEADER_LEN) {
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
    //
    //  And there's one final complication to worry about - if the simh on the
    // other end is restarted for some reason, then his sequence numbers will
    // reset to zero.  In that case we'll never recover synchronization without
    // special efforts.  The hack is to check for a packet sequence number of
    // zero and, if we find it, force synchronization.  This improves the
    // situation, but I freely admit that it's possible to think of a number of
    // cases where this also fails.  The only absolute solution is to implement
    // a more complicated system with non-IMP control messages exchanged between
    // the modem emulation on both ends.  That'd be nice, but I'll leave it as
    // an exercise for later.
    pktseq = ntohl(pkt.sequence);
    if ((pktseq == 0) && (udp_links[link].rxsequence != 0)) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - remote modem restarted\n", link);
    } else if (pktseq < udp_links[link].rxsequence) {
      sim_debug(IMP_DBG_UDP, dptr, "link %d - received packet out of sequence 1 (expected=%d received=%d\n", link, udp_links[link].rxsequence, pktseq);
      continue;  // discard this packet!
    }
    else if (pktseq != udp_links[link].rxsequence) {
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
