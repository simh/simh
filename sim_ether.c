/* sim_ether.c: OS-dependent network routines
  ------------------------------------------------------------------------------

   Copyright (c) 2002, David T. Hittner

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  ------------------------------------------------------------------------------

  Modification history:

  31-Oct-02  DTH  Added USE_NETWORK conditional
                  Reworked not attached test
                  Added OpenBSD support (from Federico Schwindt)
                  Added ethX detection simplification (from Megan Gentry)
                  Removed sections of temporary code
                  Added parameter validation
  23-Oct-02  DTH  Beta 5 released
  22-Oct-02  DTH  Added all_multicast and promiscuous support
                  Fixed not attached behavior
  21-Oct-02  DTH  Added NetBSD support (from Jason Thorpe)
                  Patched buffer size to make sure entire packet is read in
                  Made 'ethX' check characters passed as well as length
                  Corrected copyright again
  16-Oct-02  DTH  Beta 4 released
                  Corrected copyright
  09-Oct-02  DTH  Beta 3 released
                  Added pdp11 write acceleration (from Patrick Caulfield)
  08-Oct-02  DTH  Beta 2 released
                  Integrated with 2.10-0p4
                  Added variable vector and copyrights
  04-Oct-02  DTH  Added linux support (from Patrick Caulfield)
  03-Oct-02  DTH  Beta version of xq/sim_ether released for SIMH 2.09-11
  24-Sep-02  DTH  Finished eth_devices, eth_getname
  18-Sep-02  DTH  Callbacks implemented
  13-Sep-02  DTH  Basic packet read/write written
  20-Aug-02  DTH  Created Sim_Ether for O/S independant ethernet implementation

  ------------------------------------------------------------------------------

  Work left to do:
    1) Add Asynchronous Packet Writing to WIN32 platform
    2) Addition of other host Operating Systems
    3) Addition of BPF packet filtering for efficiency

  ------------------------------------------------------------------------------
*/


#include "sim_ether.h"

/*============================================================================*/
/*                  OS-independant ethernet routines                          */
/*============================================================================*/

void eth_mac_fmt(ETH_MAC* mac, char* buff)
{
  uint8* m = (uint8*) mac;
  sprintf(buff, "%02X-%02X-%02X-%02X-%02X-%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return;
}

void eth_packet_trace(ETH_PACK* packet, char* msg)
{
  unsigned char src[20];
  unsigned char dst[20];
  unsigned short* proto = (unsigned short*) &packet->msg[12];
  eth_mac_fmt((ETH_MAC*)&packet->msg[0], dst);
  eth_mac_fmt((ETH_MAC*)&packet->msg[6], src);
  printf("%s:  dst: %s  src: %s  protocol: %d  len: %d\n",
         msg, dst, src, *proto, packet->len);
}

char* eth_getname(int number, char* name)
{
#define ETH_SUPPORTED_DEVICES 10
  ETH_LIST  list[ETH_SUPPORTED_DEVICES];
  int count = eth_devices(ETH_SUPPORTED_DEVICES, list);

  if (count < number) return 0;
  strcpy(name, list[number].name);
  return name;
}

void eth_zero(ETH_DEV* dev)
{
  /* set all members to NULL OR 0 */
  memset(dev, 0, sizeof(ETH_DEV));
}

/*============================================================================*/
/*                        Non-implemented versions                            */
/*============================================================================*/

#if !defined (WIN32) && !defined(linux) && !defined(__NetBSD__) && \
    !defined (__OpenBSD__) || !defined (USE_NETWORK)
t_stat eth_open (ETH_DEV* dev, char* name)
  {return SCPE_NOFNC;}
t_stat eth_close (ETH_DEV* dev)
  {return SCPE_NOFNC;}
t_stat eth_write (ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
  {return SCPE_NOFNC;}
t_stat eth_read (ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
  {return SCPE_NOFNC;}
t_stat eth_filter (ETH_DEV* dev, int addr_count, ETH_MAC* addresses,
                   ETH_BOOL all_multicast, ETH_BOOL promiscuous)
  {return SCPE_NOFNC;}
int eth_devices (int max, ETH_LIST* dev)
  {return 0;}
#else	 /* endif unimplemented */

/*============================================================================*/
/*                WIN32, Linux, NetBSD, and OpenBSD routines                  */
/*                   Uses WinPCAP and libpcap packages                        */
/*============================================================================*/

#include <ctype.h>
#include <pcap.h>
#ifdef WIN32
#include <packet32.h>
#endif /* WIN32 */
#if defined (__NetBSD__) || defined (__OpenBSD__)
#include <sys/ioctl.h>
#include <net/bpf.h>
#endif /* __NetBSD__ || __OpenBSD__*/
#if defined (linux) || defined(__NetBSD__) || defined (__OpenBSD__)
#include <fcntl.h>
#endif /* linux || __NetBSD__ || __OpenBSD__ */

t_stat eth_open(ETH_DEV* dev, char* name)
{
  const int bufsz = (BUFSIZ < ETH_MAX_PACKET) ? ETH_MAX_PACKET : BUFSIZ;
  char errbuf[PCAP_ERRBUF_SIZE];
  char temp[1024];
  char* savname = name;
  int   i, num;

  /* initialize device */
  eth_zero(dev);

  /* translate name of type "ethX" to real device name */
  if ((strlen(name) == 4)
      && (tolower(name[0]) == 'e')
      && (tolower(name[1]) == 't')
      && (tolower(name[2]) == 'h')
      && isdigit(name[3])
     ) {
    num = atoi(&name[3]);
    savname = eth_getname(num, temp);
  }

  /* attempt to connect device */
  dev->handle = (void*) pcap_open_live(savname, bufsz, ETH_PROMISC, -1, errbuf);

  if (!dev->handle) { /* can't open device */
#ifdef _DEBUG
    printf("pcap_open_live: %s\n", errbuf);
#endif
    return SCPE_OPENERR;
  }

  /* save name of device */
  dev->name = malloc(strlen(savname)+1);
  strcpy(dev->name, savname);

#if defined (__NetBSD__) || defined(__OpenBSD__)
  /* tell the kernel that the header is fully-formed when it gets it.
     this is required in order to fake the src address.  */
  i = 1;
  ioctl(pcap_fileno(dev->handle), BIOCSHDRCMPLT, &i);
#endif /* __NetBSD__ || __OpenBSD__ */

#if defined(linux) || defined(__NetBSD__) || defined (__OpenBSD__)
  /* set file non-blocking */
  fcntl(pcap_fileno(dev->handle), F_SETFL, fcntl(pcap_fileno(dev->handle), F_GETFL, 0) | O_NONBLOCK);
#endif /* linux || __NetBSD__ || __OpenBSD__ */

  return SCPE_OK;
}

t_stat eth_close(ETH_DEV* dev)
{
  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* close the device */
  pcap_close(dev->handle);

  /* clean up the mess */
  free(dev->name);
  eth_zero(dev);

  return SCPE_OK;
}

t_stat eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
  int status = 1;   /* default to failure */

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* make sure packet exists */
  if (!packet) return SCPE_ARG;

  /* make sure packet is acceptable length */
  if ((packet->len >= ETH_MIN_PACKET) && (packet->len <= ETH_MAX_PACKET)) {
    /* dispatch write request (synchronous; no need to save write info to dev) */
#ifdef _DEBUG
    eth_packet_trace (packet, "writing");
#endif
    status = pcap_sendpacket((pcap_t*)dev->handle, (u_char*)packet->msg, packet->len);
  } /* if packet->len */

  /* call optional write callback function */
  if (routine)
    (routine)(status);

  return SCPE_OK;
}

void eth_callback(u_char* info, const struct pcap_pkthdr* header, const u_char* data)
{
  ETH_DEV*  dev = (ETH_DEV*) info;

  /*+ temporary packet filter - should be done by BPF filter */
  int to_me = 0;
  int from_me = 0;
  int i;
  for (i = 0; i < ETH_FILTER_MAX; i++) {
    if (memcmp(data,     dev->filter_address[i], 6) == 0) to_me = 1;
    if (memcmp(&data[6], dev->filter_address[i], 6) == 0) from_me = 1;
  } /* for */

  /* all multicast mode? */
  if (dev->all_multicast && (data[0] & 0x01)) to_me = 1;

  /* promiscuous mode? */
  if (dev->promiscuous) to_me = 1;

  if (to_me && !from_me) {

  /*- temporary packet filter - should be done by driver filter */


  /* set data in passed read packet */
  dev->read_packet->len = header->len;
  memcpy(dev->read_packet->msg, data, header->len);

#ifdef _DEBUG
  eth_packet_trace (dev->read_packet, "reading");
#endif

  /* call optional read callback function */
  if (dev->read_callback)
    (dev->read_callback)(0);


  /*+ temporary packet filter - should be done by driver filter */
  } /* if use */
  /*- temporary packet filter - should be done by driver filter */

}

t_stat eth_read(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
  int status;

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* make sure packet exists */
  if (!packet) return SCPE_ARG;

  /* set read packet */
  dev->read_packet = packet;

  /* set optional callback routine */
  dev->read_callback = routine;

  /* dispatch read request */
  status = pcap_dispatch((pcap_t*)dev->handle, 1, &eth_callback, (u_char*)dev);

  return SCPE_OK;
}

t_stat eth_filter(ETH_DEV* dev, int addr_count, ETH_MAC* addresses,
                  ETH_BOOL all_multicast, ETH_BOOL promiscuous)
{
  int i;

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* should be implemented as a BPF filter, but for now.. */

  /* filter count OK? */
  if ((addr_count < 0) || (addr_count > ETH_FILTER_MAX))
    return SCPE_ARG;
  else
    if (!addresses) return SCPE_ARG;

  /* clear filter array */
  memset(dev->filter_address, 0, sizeof(ETH_MAC) * ETH_FILTER_MAX);

  /* set new filter addresses */
  for (i = 0; i < addr_count; i++)
    memcpy(dev->filter_address[i], addresses[i], sizeof(ETH_MAC));

  /* set all_multicast and promiscuous flags */
  dev->all_multicast = all_multicast;
  dev->promiscuous   = promiscuous;

  return SCPE_OK;
}

int eth_devices(int max, ETH_LIST* list)
{
  int   i, index, len;
  uint8 buffer[2048];
  uint8 buffer2[2048];
  uint8* cptr = buffer2;
  int size = sizeof(buffer);
  unsigned long ret;

  /* get names of devices from packet driver */
  ret = PacketGetAdapterNames(buffer, &size);

  /* device names in ascii or unicode format? */
  if ((buffer[1] == 0) && (buffer[3] == 0)) { /* unicode.. <sigh> */
    int i = 0;
    int cptr_inc = 2;
    /* want to use buffer for scanning, so copy to buffer2 */
    memcpy (buffer2, buffer, sizeof(buffer));
    /* convert unicode to ascii (assuming every other byte is zero) */
    while (cptr < (buffer2 + sizeof(buffer2))) {
      buffer[i] = *cptr;
      if ((buffer[i] == 0) && (buffer[i-1] == 0)) { /* end of unicode devices */
        /* descriptions are in ascii, so change increment */
        cptr_inc = 1;
      }
      cptr += cptr_inc;
      i++;
    }
  }

  /* scan ascii string and load list*/
  index = 0;
  cptr = buffer;
  /* extract device names and numbers */
  while (len = strlen(cptr)) {
    list[index].num = index;
    strcpy(list[index].name, cptr);
    cptr += len + 1;
    index++;
  }
  cptr += 2;
  /* extract device descriptions */
  for (i=0; i < index; i++) {
    len = strlen(cptr);
    strcpy(list[i].desc, cptr);
    cptr += len + 1;
  }
  return index; /* count of devices */
}

#endif /* (WIN32 || linux || __NetBSD__ || __OpenBSD__) && USE_NETWORK */

/*============================================================================*/
/*                          linux-specific code                               */
/*============================================================================*/

#if defined (linux) && defined (USE_NETWORK)
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <features.h>    /* for the glibc version number */
#if (__GLIBC__ >= 2 && __GLIBC_MINOR >= 1) || __GLIBC__ >= 3
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */
#else /*__GLIBC__*/
#include <asm/types.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#endif /*__GLIBC__*/

int pcap_sendpacket(pcap_t* handle, u_char* msg, int len)
{
  return (send(pcap_fileno(handle), msg, len, 0) == len)?0:-1;
}

int PacketGetAdapterNames(char* buffer, int* size)
{
  struct ifreq ifr;
  int iindex = 1;
  int sock = socket(PF_PACKET, SOCK_RAW, 0);
  int ptr = 0;

  ifr.ifr_ifindex = iindex;

  while (ioctl(sock, SIOCGIFNAME, &ifr) == 0) {
	  /* Only use ethernet interfaces */
	  ioctl(sock, SIOCGIFHWADDR, &ifr);
	  if (ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
	    strcpy(buffer+ptr, ifr.ifr_name);
	    ptr += strlen(buffer)+1;
  	}
	  ifr.ifr_ifindex = ++iindex;
  }

  close(sock);

  buffer[ptr++] = '\0';
  buffer[ptr++] = '\0';
  *size = ptr;
}

#endif /* linux && USE_NETWORK */

/*============================================================================*/
/*                          NetBSD/OpenBSD-specific code                      */
/*============================================================================*/

#if (defined (__NetBSD__) || defined(__OpenBSD__)) && defined (USE_NETWORK)
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <ifaddrs.h>
#include <string.h>

int pcap_sendpacket(pcap_t* handle, u_char* msg, int len)
{
  return (write(pcap_fileno(handle), msg, len) == len)?0:-1;
}

int PacketGetAdapterNames(char* buffer, int* size)
{
  const struct sockaddr_dl *sdl;
  struct ifaddrs *ifap, *ifa;
  char *p;
  int ptr = 0;

  if (getifaddrs(&ifap) != 0) {
    *size = 0;
    return (0);
  }

  p = NULL;
  for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family != AF_LINK)
      continue;
    if (p && strcmp(p, ifa->ifa_name) == 0)
      continue;
    sdl = (const struct sockaddr_dl *) ifa->ifa_addr;
    if (sdl->sdl_type != IFT_ETHER)
      continue;

    strcpy(buffer+ptr, ifa->ifa_name);
    ptr += strlen(ifa->ifa_name)+1;
  }

  freeifaddrs(ifap);

  buffer[ptr++] = '\0';
  buffer[ptr++] = '\0';
  *size = ptr;

  return (ptr);
}

#endif /* (__NetBSD__ || __OpenBSD__) && USE_NETWORK */

