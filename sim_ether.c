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

  This ethernet simulation is based on the PCAP and WinPcap packages.

  PCAP/WinPcap was chosen as the basis for network code since it is the most
  "universal" of the various network packages available. Using this style has
  allowed rapid network development for the major SIMH platforms. Developing
  a network package specifically for SIMH was rejected due to the time required;
  the advantage would be a more easily compiled and integrated code set.

  There are various problems associated with use of ethernet networking, which
  would be true regardless of the network package used, since there are no
  universally accepted networking methods. The most serious of these is getting
  the proper networking package loaded onto the system, since most environments
  do not come with the network interface packages loaded.

  The second most serious network issue relates to security. The network
  simulation needs to simulate operating system level functionality (packet
  driving). However, the host network programming interfaces tend to operate at
  the user level of functionality, so getting to the full functionality of
  the network interface usually requires that the person executing the
  network code be a privileged user of the host system. See the PCAP/WinPcap
  documentation for the appropriate host platform if unprivileged use of
  networking is needed - there may be known workarounds.

  ------------------------------------------------------------------------------

  Modification history:

  15-Jan-03  DTH  Corrected PacketGetAdapterNames parameter2 datatype
  26-Dec-02  DTH  Merged Mark Pizzolato's enhancements with main source
                  Added networking documentation
                  Changed _DEBUG to ETH_DEBUG
  20-Dec-02  MP   Added display of packet CRC to the eth_packet_trace.
                  This helps distinguish packets with identical lengths
                  and protocols.
  05-Dec-02  MP   With the goal of draining the input buffer more rapidly
                  changed eth_read to call pcap_dispatch repeatedly until
                  either a timeout returns nothing or a packet allowed by
                  the filter is seen.  This more closely reflects how the
                  pcap layer will work when the filtering is actually done
                  by a bpf filter.
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
    1) Addition of other host Operating Systems (VMS, MAC, etc..)
    2) Possible efficiency increase by using BPF filtering

  ------------------------------------------------------------------------------
*/


#include "sim_ether.h"

extern FILE *sim_log;

/*============================================================================*/
/*                  OS-independant ethernet routines                          */
/*============================================================================*/

void eth_mac_fmt(ETH_MAC* mac, char* buff)
{
  uint8* m = (uint8*) mac;
  sprintf(buff, "%02X-%02X-%02X-%02X-%02X-%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return;
}

static const uint32 crcTable[256] = {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
  0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
  0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
  0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
  0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
  0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
  0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
  0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
  0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
  0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
  0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
  0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
  0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
  0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
  0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
  0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
  0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
  0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
  0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
  0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
  0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
  0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
  0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
  0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
  0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
  0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
  0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
  0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
  0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
  0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
  0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
  0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
  0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
  0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
  0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
  0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
  0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32 eth_crc32(uint32 crc, const void* vbuf, size_t len)
{
  const uint32 mask = 0xFFFFFFFF;
  const unsigned char* buf = (const unsigned char*)vbuf;

  crc ^= mask;
  while (0 != len--)
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
  return(crc ^ mask);
}


void eth_packet_trace(ETH_PACK* packet, char* msg)
{
  unsigned char src[20];
  unsigned char dst[20];
  unsigned short* proto = (unsigned short*) &packet->msg[12];
  uint32 crc = eth_crc32(0, packet->msg, packet->len);
  eth_mac_fmt((ETH_MAC*)&packet->msg[0], dst);
  eth_mac_fmt((ETH_MAC*)&packet->msg[6], src);
  printf("Eth: %s  dst: %s  src: %s  protocol: %d  len: %d  crc: %X\n",
         msg, dst, src, *proto, packet->len, crc);
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
/*                   Uses WinPcap and libpcap packages                        */
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
  int   num;

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
  memset(errbuf, 0, sizeof(errbuf));
  dev->handle = (void*) pcap_open_live(savname, bufsz, ETH_PROMISC, -1, errbuf);
  if (!dev->handle) { /* can't open device */
    if (sim_log) fprintf (sim_log, "Eth: pcap_open_live error - %s\n", errbuf);
    return SCPE_OPENERR;
  } else {
    if (sim_log) fprintf (sim_log, "Eth: opened %s\n", savname);
  }

  /* save name of device */
  dev->name = malloc(strlen(savname)+1);
  strcpy(dev->name, savname);

#if defined (__NetBSD__) || defined(__OpenBSD__)
  /* Tell the kernel that the header is fully-formed when it gets it.
     This is required in order to fake the src address.
     Code is embedded in braces to create a scope for the local variable */
  {
  int one = 1;
  ioctl(pcap_fileno(dev->handle), BIOCSHDRCMPLT, &one);
  }
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
  if (sim_log) fprintf (sim_log, "Eth: closed %s\n", dev->name);

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
#ifdef ETH_DEBUG
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

  /* receive packet filter */
  int to_me = 0;
  int from_me = 0;
  int i;
  for (i = 0; i < ETH_FILTER_MAX; i++) {
    if (memcmp(data,     dev->filter_address[i], 6) == 0) to_me = 1;
#ifdef WIN32
    /*
    WinPcap has a known bug/feature that whenever a packet is transmitted,
    it is looped back into the receive buffers. This is not consistant with the
    behavior of real ethernet adapters, so the extra packets must be disposed of.

    This behavior is seen when starting DECNET; DECNET broadcasts a packet
    with the same source and destination addresses to make sure that no other
    ethernet adapter on the network is using the DECNET address that it wants.
    If it sees this test packet coming back in, it assumes that another node on
    the network has the same DECNET address and refuses to start, giving an
    "Invalid media address" error.

    This code section was ifdef'd for WIN32 only to allow other OS's a chance to
    properly implement the above behavior. If it breaks the ethernet simulator
    on other platforms, remove the ifdef so that it will affect your platform,
    and then notify the author so that he can fix the ifdef. :-)
    */
    if (memcmp(&data[6], dev->filter_address[i], 6) == 0) from_me = 1;
#endif
  } /* for */

  /* all multicast mode? */
  if (dev->all_multicast && (data[0] & 0x01)) to_me = 1;

  /* promiscuous mode? */
  if (dev->promiscuous) to_me = 1;

  if (to_me && !from_me) {

    /* set data in passed read packet */
    dev->read_packet->len = header->len;
    memcpy(dev->read_packet->msg, data, header->len);

#ifdef ETH_DEBUG
    eth_packet_trace (dev->read_packet, "reading");
#endif

    /* call optional read callback function */
    if (dev->read_callback)
      (dev->read_callback)(0);

  } /* if to_me && !from_me */
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
  packet->len = 0;

  /* set optional callback routine */
  dev->read_callback = routine;

  /* dispatch read request to either receive a packet (after filtering) or timeout */
  do {
    status = pcap_dispatch((pcap_t*)dev->handle, 1, &eth_callback, (u_char*)dev);
  } while ((status) && (0 == packet->len));
  return SCPE_OK;
}

t_stat eth_filter(ETH_DEV* dev, int addr_count, ETH_MAC* addresses,
                  ETH_BOOL all_multicast, ETH_BOOL promiscuous)
{
  int i;

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

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

  /* store other flags */
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
  unsigned long size = sizeof(buffer);
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

int PacketGetAdapterNames(char* buffer, unsigned long* size)
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

int PacketGetAdapterNames(char* buffer, unsigned long* size)
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

