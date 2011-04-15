/* sim_ether.c: OS-dependent network routines
  ------------------------------------------------------------------------------
   Copyright (c) 2002-2007, David T. Hittner

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

  Define one of the two macros below to enable networking:
    USE_NETWORK - Create statically linked network code
    USE_SHARED  - Create dynamically linked network code (_WIN32 only)

  ------------------------------------------------------------------------------

  Supported/Tested Platforms:

  Windows(NT,2K,XP,2K3)     WinPcap         V3.0+
  Linux                     libpcap at least 0.9
  OpenBSD,FreeBSD,NetBSD    libpcap at least 0.9
  MAC OS/X                  libpcap at least 0.9
  Solaris Sparc             libpcap at least 0.9
  Solaris Intel             libpcap at least 0.9
  AIX                       ??
  HP/UX                     ??
  Compaq Tru64 Unix         ??
  VMS                       Alpha/Itanium VMS only, needs VMS libpcap
  
  WinPcap is available from: 
                        http://winpcap.polito.it/
  libpcap for VMS is available from: 
                        http://simh.trailing-edge.com/sources/vms-pcap.zip
  libpcap for other Unix platforms is available at: 
        Current Version:  http://www.tcpdump.org/daily/libpcap-current.tar.gz
        Released Version: http://www.tcpdump.org/release/
        Note: You can only use the released version if it is at least 
              version 0.9

        
        We've gotten the tarball, unpacked, built and installed it with:
            gzip -dc libpcap-current.tar.gz | tar xvf -
            cd libpcap-directory-name
            ./configure
            make
            make install
        Note:  The "make install" step generally will have to be done as root.
        This will install libpcap in /usr/local/lib and /usr/local/include
        It is then important to make sure that you get the just installed 
        libpcap components referenced during your build.  This is generally 
        achieved by invoking gcc with: 
             -isystem /usr/local/include -L /usr/local/lib


  Note: Building for the platforms indicated above, with the indicated libpcap, 
  should automatically leverage the appropriate mechanisms contained here.  
  Things are structured so that it is likely to work for any other as yet 
  untested platform.  If it works for you, please let the author know so we 
  can update the table above.  If it doesn't work, then the following #define 
  variables can influence the operation on an untested platform.

  USE_BPF           - Determines if this code leverages a libpcap/WinPcap 
                      provided bpf packet filtering facility.  All tested 
                      environments have bpf facilities that work the way we 
                      need them to.  However a new one might not.  undefine 
                      this variable to let this code do its own filtering.
  USE_SETNONBLOCK   - Specifies whether the libpcap environment's non-blocking 
                      semantics are to be leveraged.  This helps to manage the 
                      varying behaviours of the kernel packet facilities 
                      leveraged by libpcap.
  USE_READER_THREAD - Specifies that packet reading should be done in the 
                      context of a separate thread.  The Posix threading 
                      APIs are used.  This option is less efficient than the
                      default non-threaded approach, but it exists since some 
                      platforms don't want to work with nonblocking libpcap 
                      semantics.   OpenBSD and NetBSD either don't have pthread 
                      APIs available, or they are too buggy to be useful. 
                      Using the threaded approach may require special compile 
                      and/or link time switches (i.e. -lpthread or -pthread, 
                      etc.) Consult the documentation for your platform as 
                      needed.  Although this may be 'less efficient' than the
                      non-threaded approach, the efficiency is an overall system
                      efficiency not necessarily a simulator efficiency.  This 
                      means that work is removed from the thread executing 
                      simulated instructions so the simulated system will most
                      likely run faster (given that modern host CPUs are 
                      multi-core and have someplace to do this work in parallel).
  MUST_DO_SELECT    - Specifies that, when USE_READER_THREAD is active,  
                      select() should be used to determin when available 
                      packets are ready for reading.  Otherwise, we depend 
                      on the libpcap/kernel packet timeout specified on 
                      pcap_open_live.  If USE_READER_THREAD is not set, then 
                      MUST_DO_SELECT is irrelevant
  USE_TAP_NETWORK   - Specifies that support for tap networking should be 
                      included.  This can be leveraged, along with OS bridging
                      capabilities to share a single LAN interface.  This 
                      allows device names of the form tap:tap0 to be specified
                      at open time.  This functionality is only useful/needed 
                      on *nix platforms since native sharing of Windows NIC 
                      devices works with no external magic.

  NEED_PCAP_SENDPACKET
                    - Specifies that you are using an older version of libpcap
                      which doesn't provide a pcap_sendpacket API.

  NOTE: Changing these defines is done in either sim_ether.h OR on the global 
        compiler command line which builds all of the modules included in a
        simulator.

  ------------------------------------------------------------------------------

  Modification history:

  09-Jan-11  MP   Fixed missing crc data when USE_READER_THREAD is defined and 
                  crc's are needed (only the pdp11_xu)
  16-Dec-10  MP   added priority boost for read and write threads when 
                  USE_READER_THREAD does I/O in separate threads.  This helps
                  throughput since it allows these I/O bound threads to preempt 
                  the main thread (which is executing simulated instructions).                  
  09-Dec-10  MP   allowed more flexible parsing of MAC address strings
  09-Dec-10  MP   Added support to determine if network address conflicts exist
  07-Dec-10  MP   Reworked DECnet self detection to the more general approach
                  of loopback self when a Physical Address is being set.
  04-Dec-10  MP   Changed eth_write to do nonblocking writes when 
                  USE_READER_THREAD is defined.
  20-Aug-10  TVO  Fix for Mac OSX 10.6
  17-Jun-10  MP   Fixed bug in the AUTODIN II hash filtering.
  14-Jun-10  MP   Added support for integrated Tap networking interfaces on BSD 
                  platforms.
  13-Jun-10  MP   Added support for integrated Tap networking interfaces on Linux 
                  platforms.
  31-May-10  MP   Added support for more TOE (TCP Offload Engine) features for IPv4
                  network traffic from the host and/or from hosts on the LAN.  These
                  new TOE features are: LSO (Large Send Offload) and Jumbo packet
                  fragmentation support.  These features allow a simulated network
                  device to suuport traffic when a host leverages a NIC's Large 
                  Send Offload capabilities to fregment and/or segment outgoing 
                  network traffic.  Additionally a simulated network device can 
                  reasonably exist on a LAN which is configured to use Jumbo frames.
  21-May-10  MP   Added functionslity to fixup IP header checksums to accomodate 
                  packets from a host with a NIC which has TOE (TCP Offload Engine)
                  enabled which is expected to implement the checksum computations
                  in hardware.  Since we catch packets before they arrive at the
                  NIC the expected checksum insertions haven't been performed yet.
                  This processing is only done for packets sent from the hoat to 
                  the guest we're supporting.  In general this will be a relatively 
                  small number of packets so it is done for all IP frame packets
                  coming from the hoat to the guest.  In order to make the 
                  determination of packets specifically arriving from the host we
                  need to know the hardware MAC address of the host NIC.  Currently
                  determining a NIC's MAC address is relatively easy on Windows.
                  The non-windows code works on linux and may work on other *nix 
                  platforms either as is or with slight modifications.  The code, 
                  as implemented, only messes with this activity if the host 
                  interface MAC address can be determined.
  20-May-10  MP   Added general support to deal with receiving packets smaller 
                  than ETH_MIN_PACKET in length.  These come from packets
                  looped back by some bridging mechanism and need to be padded
                  to the minimum frame size.  A real NIC won't pass us any 
                  packets like that.  This fix belongs here since this layer
                  is responsible for interfacing to they physical layer 
                  devices, AND it belongs here to get CRC processing right.
  05-Mar-08  MP   Added optional multicast filtering support for doing
                  LANCE style AUTODIN II based hashed filtering.
  07-Feb-08  MP   Added eth_show_dev to display ethernet state
                  Changed the return value from eth_read to return whether
                  or not a packet was read.  No existing callers used or 
                  checked constant return value that previously was being
                  supplied.
  29-Jan-08  MP   Added eth_set_async to provide a mechanism (when 
                  USE_READER_THREAD is enabled) to allow packet reception 
                  to dynamically update the simulator event queue and 
                  potentially avoid polling for I/O.  This provides a minimal 
                  overhead (no polling) maximal responsiveness for network 
                  activities.
  29-Jan-08  MP   Properly sequenced activities in eth_close to avoid a race
                  condition when USE_READER_THREAD is enabled.
  25-Jan-08  MP   Changed the following when USE_READER_THREAD is enabled:
                  - Fixed bug when the simulated device doesn't need crc 
                    in packet data which is read.
                  - Added call to pcap_setmintocopy to minimize packet 
                    delivery latencies.
                  - Added ethq_destroy and used it to avoid a memory leak in
                    eth_close.
                  - Properly cleaned up pthread mutexes in eth_close.
                  Migrated to using sim_os_ms_sleep for a delay instead of
                  a call to select().
                  Fixed the bpf filter used when no traffic is to be matched.
                  Reworked eth_add_packet_crc32 implementation to avoid an
                  extra buffer copy while reading packets.
                  Fixedup #ifdef's relating to USE_SHARED so that setting 
                  USE_SHARED or USE_NETWORK will build a working network 
                  environment.
  23-Jan-08  MP   Reworked eth_packet_trace and eth_packet_trace_ex to allow
                  only output ethernet header+crc and provide a mechanism for
                  the simulated device to display full packet data debugging.
  17-May-07  DTH  Fixed non-ethernet device removal loop (from Naoki Hamada)
  15-May-07  DTH  Added dynamic loading of wpcap.dll;
                  Corrected exceed max index bug in ethX lookup
  04-May-07  DTH  Corrected failure to look up ethernet device names in
                  the registry on Windows XP x64
  10-Jul-06  RMS  Fixed linux conditionalization (from Chaskiel Grundman)
  02-Jun-06  JDB  Fixed compiler warning for incompatible sscanf parameter
  15-Dec-05  DTH  Patched eth_host_devices [remove non-ethernet devices]
                  (from Mark Pizzolato and Galen Tackett, 08-Jun-05)
                  Patched eth_open [tun fix](from Antal Ritter, 06-Oct-05)
  30-Nov-05  DTH  Added option to regenerate CRC on received packets; some
                  ethernet devices need to pass it on to the simulation, and by
                  the time libpcap/winpcap gets the packet, the host OS network
                  layer has already stripped CRC out of the packet
  01-Dec-04  DTH  Added Windows user-defined adapter names (from Timothe Litt)
  25-Mar-04  MP   Revised comments and minor #defines to deal with updated
                  libpcap which now provides pcap_sendpacket on all platforms.
  04-Feb-04  MP   Returned success/fail status from eth_write to support
                  determining if the current libpcap connection can successfully 
                  write packets.
                  Added threaded approach to reading packets since
                  this works better on some platforms (solaris intel) than the 
                  inconsistently implemented non-blocking read approach.
  04-Feb-04  DTH  Converted ETH_DEBUG to sim_debug
  13-Jan-04  MP   tested and fixed on OpenBSD, NetBS and FreeBSD.
  09-Jan-04  MP   removed the BIOCSHDRCMPLT ioctl() for OS/X
  05-Jan-04  DTH  Added eth_mac_scan
  30-Dec-03  DTH  Cleaned up queue routines, added no network support message
  26-Dec-03  DTH  Added ethernet show and queue functions from pdp11_xq
  15-Dec-03  MP   polished generic libpcap support.
  05-Dec-03  DTH  Genericized eth_devices() and #ifdefs
  03-Dec-03  MP   Added Solaris support
  02-Dec-03  DTH  Corrected decnet fix to use reflection counting
  01-Dec-03  DTH  Added BPF source filtering and reflection counting
  28-Nov-03  DTH  Rewrote eth_devices using universal pcap_findalldevs()
  25-Nov-03  DTH  Verified DECNET_FIX, reversed ifdef to mainstream code
  19-Nov-03  MP   Fixed BPF functionality on Linux/BSD.
  17-Nov-03  DTH  Added xBSD simplification
  14-Nov-03  DTH  Added #ifdef DECNET_FIX for problematic duplicate detection code
  13-Nov-03  DTH  Merged in __FreeBSD__ support
  21-Oct-03  MP   Added enriched packet dumping for debugging
  20-Oct-03  MP   Added support for multiple ethernet devices on VMS
  20-Sep-03  Ankan Add VMS support (Alpha only)
  29-Sep-03  MP   Changed separator character in eth_fmt_mac to be ":" to
                  format ethernet addresses the way the BPF compile engine
                  wants to see them.
                  Added BPF support to filter packets
                  Added missing printf in eth_close
  07-Jun-03  MP   Added WIN32 support for DECNET duplicate address detection.
  06-Jun-03  MP   Fixed formatting of Ethernet Protocol Type in eth_packet_trace
  30-May-03  DTH  Changed WIN32 to _WIN32 for consistency
  07-Mar-03  MP   Fixed Linux implementation of PacketGetAdapterNames to also
                  work on Red Hat 6.2-sparc and Debian 3.0r1-sparc.
  03-Mar-03  MP   Changed logging to be consistent on stdout and sim_log
  01-Feb-03  MP   Changed type of local variables in eth_packet_trace to
                  conform to the interface needs of eth_mac_fmt wich produces
                  char data instead of unsigned char data.  Suggested by the
                  DECC compiler.
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
*/

#include <ctype.h>
#include "sim_ether.h"
#include "sim_sock.h"

extern FILE *sim_log;


/*============================================================================*/
/*                  OS-independant ethernet routines                          */
/*============================================================================*/

t_stat eth_mac_scan (ETH_MAC* mac, char* strmac)
{
  int a0, a1, a2, a3, a4, a5;
  const ETH_MAC zeros = {0,0,0,0,0,0};
  const ETH_MAC ones  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ETH_MAC newmac;

  if ((6 != sscanf(strmac, "%x:%x:%x:%x:%x:%x", &a0, &a1, &a2, &a3, &a4, &a5)) &&
      (6 != sscanf(strmac, "%x.%x.%x.%x.%x.%x", &a0, &a1, &a2, &a3, &a4, &a5)) &&
      (6 != sscanf(strmac, "%x-%x-%x-%x-%x-%x", &a0, &a1, &a2, &a3, &a4, &a5)))
    return SCPE_ARG;
  if ((a0 > 0xFF) || (a1 > 0xFF) || (a2 > 0xFF) || (a3 > 0xFF) || (a4 > 0xFF) || (a5 > 0xFF))
    return SCPE_ARG;
  newmac[0] = a0;
  newmac[1] = a1;
  newmac[2] = a2;
  newmac[3] = a3;
  newmac[4] = a4;
  newmac[5] = a5;

  /* final check - mac cannot be broadcast or multicast address */
  if (!memcmp(newmac, zeros, sizeof(ETH_MAC)) ||  /* broadcast */
      !memcmp(newmac, ones,  sizeof(ETH_MAC)) ||  /* broadcast */
      (newmac[0] & 0x01)                          /* multicast */
     )
    return SCPE_ARG;

  /* new mac is OK, copy into passed mac */
  memcpy (*mac, newmac, sizeof(ETH_MAC));
  return SCPE_OK;
}

void eth_mac_fmt(ETH_MAC* mac, char* buff)
{
  uint8* m = (uint8*) mac;
  sprintf(buff, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
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
  while (len > 8) {
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
    len -= 8;
  }
  while (0 != len--)
    crc = (crc >> 8) ^ crcTable[ (crc ^ (*buf++)) & 0xFF ];
  return(crc ^ mask);
}

int eth_get_packet_crc32_data(const uint8 *msg, int len, uint8 *crcdata)
{
  int crc_len;

  if (len <= ETH_MAX_PACKET) {
    uint32 crc = eth_crc32(0, msg, len);                  /* calculate CRC */
    uint32 ncrc = htonl(crc);                             /* CRC in network order */
    int size = sizeof(ncrc);                              /* size of crc field */
    memcpy(crcdata, &ncrc, size);                         /* append crc to packet */
    crc_len = len + size;                                 /* set packet crc length */
  } else {
    crc_len = 0;                                          /* appending crc would destroy packet */
  }
  return crc_len;
}

int eth_add_packet_crc32(uint8 *msg, int len)
{
  int crc_len;

  if (len <= ETH_MAX_PACKET) {
    crc_len = eth_get_packet_crc32_data(msg, len, &msg[len]);/* append crc to packet */
  } else {
    crc_len = 0;                                          /* appending crc would destroy packet */
  }
  return crc_len;
}

void eth_setcrc(ETH_DEV* dev, int need_crc)
{
  dev->need_crc = need_crc;
}

void eth_packet_trace_ex(ETH_DEV* dev, const uint8 *msg, int len, char* txt, int detail, uint32 reason)
{
  if (dev->dptr->dctrl & reason) {
    char src[20];
    char dst[20];
    unsigned short* proto = (unsigned short*) &msg[12];
    uint32 crc = eth_crc32(0, msg, len);
    eth_mac_fmt((ETH_MAC*)&msg[0], dst);
    eth_mac_fmt((ETH_MAC*)&msg[6], src);
    sim_debug(reason, dev->dptr, "%s  dst: %s  src: %s  proto: 0x%04X  len: %d  crc: %X\n",
          txt, dst, src, ntohs(*proto), len, crc);
    if (detail) {
      int i, same, group, sidx, oidx;
      char outbuf[80], strbuf[18];
      static char hex[] = "0123456789ABCDEF";
      for (i=same=0; i<len; i += 16) {
        if ((i > 0) && (0 == memcmp(&msg[i], &msg[i-16], 16))) {
          ++same;
          continue;
        }
        if (same > 0) {
          sim_debug(reason, dev->dptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
          same = 0;
        }
        group = (((len - i) > 16) ? 16 : (len - i));
        for (sidx=oidx=0; sidx<group; ++sidx) {
          outbuf[oidx++] = ' ';
          outbuf[oidx++] = hex[(msg[i+sidx]>>4)&0xf];
          outbuf[oidx++] = hex[msg[i+sidx]&0xf];
          if (isprint(msg[i+sidx]))
            strbuf[sidx] = msg[i+sidx];
          else
            strbuf[sidx] = '.';
        }
        outbuf[oidx] = '\0';
        strbuf[sidx] = '\0';
        sim_debug(reason, dev->dptr, "%04X%-48s %s\n", i, outbuf, strbuf);
      }
      if (same > 0)
        sim_debug(reason, dev->dptr, "%04X thru %04X same as above\n", i-(16*same), len-1);
    }
  }
}

void eth_packet_trace(ETH_DEV* dev, const uint8 *msg, int len, char* txt)
{
  eth_packet_trace_ex(dev, msg, len, txt, 0, dev->dbit);
}

char* eth_getname(int number, char* name)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = eth_devices(ETH_MAX_DEVICE, list);

  if (count <= number) return 0;
  strcpy(name, list[number].name);
  return name;
}

char* eth_getname_bydesc(char* desc, char* name)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = eth_devices(ETH_MAX_DEVICE, list);
  int i;
  size_t j=strlen(desc);

  for (i=0; i<count; i++) {
    int found = 1;
    size_t k = strlen(list[i].desc);

    if (j != k) continue;
    for (k=0; k<j; k++)
      if (tolower(list[i].desc[k]) != tolower(desc[k]))
        found = 0;
    if (found == 0) continue;

    /* found a case-insensitive description match */
    strcpy(name, list[i].name);
    return name;
  }
  /* not found */
  return 0;
}

/* strncasecmp() is not available on all platforms */
int eth_strncasecmp(char* string1, char* string2, size_t len)
{
  size_t i;
  unsigned char s1, s2;

  for (i=0; i<len; i++) {
    s1 = string1[i];
    s2 = string2[i];
    if (islower (s1)) s1 = toupper (s1);
    if (islower (s2)) s2 = toupper (s2);

    if (s1 < s2)
      return -1;
    if (s1 > s2)
      return 1;
    if (s1 == 0) return 0;
  }
  return 0;
}

char* eth_getname_byname(char* name, char* temp)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = eth_devices(ETH_MAX_DEVICE, list);
  size_t n;
  int i, found;

  found = 0;
  n = strlen(name);
  for (i=0; i<count && !found; i++) {
    if (eth_strncasecmp(name, list[i].name, n) == 0) {
      found = 1;
      strcpy(temp, list[i].name); /* only case might be different */
    }
  }
  if (found) {
    return temp;
  } else {
    return 0;
  }
}

void eth_zero(ETH_DEV* dev)
{
  /* set all members to NULL OR 0 */
  memset(dev, 0, sizeof(ETH_DEV));
  dev->reflections = -1;                          /* not established yet */
}

t_stat eth_show (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int number = eth_devices(ETH_MAX_DEVICE, list);

  fprintf(st, "ETH devices:\n");
  if (number == -1)
    fprintf(st, "  network support not available in simulator\n");
  else
    if (number == 0)
      fprintf(st, "  no network devices are available\n");
    else {
      size_t min, len;
      int i;
      for (i=0, min=0; i<number; i++)
        if ((len = strlen(list[i].name)) > min) min = len;
      for (i=0; i<number; i++)
        fprintf(st,"  %d  %-*s (%s)\n", i, (int)min, list[i].name, list[i].desc);
    }
  return SCPE_OK;
}

t_stat ethq_init(ETH_QUE* que, int max)
{
  /* create dynamic queue if it does not exist */
  if (!que->item) {
    que->item = (struct eth_item *) calloc(max, sizeof(struct eth_item));
    if (!que->item) {
      /* failed to allocate memory */
      char* msg = "EthQ: failed to allocate dynamic queue[%d]\r\n";
      printf(msg, max);
      if (sim_log) fprintf(sim_log, msg, max);
      return SCPE_MEM;
    };
    que->max = max;
  };
  ethq_clear(que);
  return SCPE_OK;
}

t_stat ethq_destroy(ETH_QUE* que)
{
  /* release dynamic queue if it exists */
  ethq_clear(que);
  que->max = 0;
  if (que->item) {
    free(que->item);
    que->item = NULL;
  };
  return SCPE_OK;
}

void ethq_clear(ETH_QUE* que)
{
  /* clear packet array */
  memset(que->item, 0, sizeof(struct eth_item) * que->max);
  /* clear rest of structure */
  que->count = que->head = que->tail = 0;
}

void ethq_remove(ETH_QUE* que)
{
  struct eth_item* item = &que->item[que->head];

  if (que->count) {
    memset(item, 0, sizeof(struct eth_item));
    if (++que->head == que->max)
      que->head = 0;
    que->count--;
  }
}

void ethq_insert_data(ETH_QUE* que, int32 type, const uint8 *data, int used, int len, int crc_len, const uint8 *crc_data, int32 status)
{
  struct eth_item* item;

  /* if queue empty, set pointers to beginning */
  if (!que->count) {
    que->head = 0;
    que->tail = -1;
  }

  /* find new tail of the circular queue */
  if (++que->tail == que->max)
    que->tail = 0;
  if (++que->count > que->max) {
    que->count = que->max;
    /* lose oldest packet */
    if (++que->head == que->max)
      que->head = 0;
    que->loss++;
    }
  if (que->count > que->high)
    que->high = que->count;

  /* set information in (new) tail item */
  item = &que->item[que->tail];
  item->type = type;
  item->packet.len = len;
  item->packet.used = used;
  item->packet.crc_len = crc_len;
  memcpy(item->packet.msg, data, ((len > crc_len) ? len : crc_len));
  if (crc_data && (crc_len > len))
    memcpy(&item->packet.msg[len], crc_data, ETH_CRC_SIZE);
  item->packet.status = status;
}

void ethq_insert(ETH_QUE* que, int32 type, ETH_PACK* pack, int32 status)
{
  ethq_insert_data(que, type, pack->msg, pack->used, pack->len, pack->crc_len, NULL, status);
}

/*============================================================================*/
/*                        Non-implemented versions                            */
/*============================================================================*/

#if !defined (USE_NETWORK) && (!defined (_WIN32) || !defined (USE_SHARED))
t_stat eth_open(ETH_DEV* dev, char* name, DEVICE* dptr, uint32 dbit)
  {return SCPE_NOFNC;}
t_stat eth_close (ETH_DEV* dev)
  {return SCPE_NOFNC;}
t_stat eth_check_address_conflict (ETH_DEV* dev, 
                                   ETH_MAC* const mac)
  {return SCPE_NOFNC;}
t_stat eth_set_async (ETH_DEV *dev, int latency)
  {return SCPE_NOFNC;}
t_stat eth_clr_async (ETH_DEV *dev)
  {return SCPE_NOFNC;}
t_stat eth_write (ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
  {return SCPE_NOFNC;}
int eth_read (ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
  {return SCPE_NOFNC;}
t_stat eth_filter (ETH_DEV* dev, int addr_count, ETH_MAC* const addresses,
                   ETH_BOOL all_multicast, ETH_BOOL promiscuous)
  {return SCPE_NOFNC;}
t_stat eth_filter_hash (ETH_DEV* dev, int addr_count, ETH_MAC* const addresses,
                   ETH_BOOL all_multicast, ETH_BOOL promiscuous, ETH_MULTIHASH* const hash)
  {return SCPE_NOFNC;}
int eth_devices (int max, ETH_LIST* dev)
  {return -1;}
void eth_show_dev (FILE* st, ETH_DEV* dev)
  {}
#else    /* endif unimplemented */

/*============================================================================*/
/*      WIN32, Linux, and xBSD routines use WinPcap and libpcap packages      */
/*        OpenVMS Alpha uses a WinPcap port and an associated execlet         */
/*============================================================================*/

#if defined (xBSD) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <net/bpf.h>
#endif /* xBSD */

#include <pcap.h>
#include <string.h>

#ifdef USE_TAP_NETWORK
#if defined(__linux)
#include <sys/ioctl.h> 
#include <net/if.h> 
#include <linux/if_tun.h> 
#elif defined(USE_BSDTUNTAP)
#include <sys/types.h>
#include <net/if_types.h>
#include <net/if_tun.h>
#else /* We don't know how to do this on the current platform */
#undef USE_TAP_NETWORK
#endif
#endif /* USE_TAP_NETWORK */

/* Allows windows to look up user-defined adapter names */
#if defined(_WIN32)
#include <winreg.h>
#endif

#if defined(_WIN32) && defined(USE_SHARED)
/* Dynamic DLL loading technique and modified source comes from
   Etherial/WireShark capture_pcap.c */

/* Dynamic DLL load variables */
static HINSTANCE hDll = 0;                      /* handle to DLL */
static int dll_loaded = 0;                      /* 0=not loaded, 1=loaded, 2=DLL load failed, 3=Func load failed */
static char* no_wpcap = "wpcap load failure";

/* define pointers to pcap functions needed */
static void    (*p_pcap_close) (pcap_t *);
static int     (*p_pcap_compile) (pcap_t *, struct bpf_program *, char *, int, bpf_u_int32);
static int     (*p_pcap_datalink) (pcap_t *);
static int     (*p_pcap_dispatch) (pcap_t *, int, pcap_handler, u_char *);
static int     (*p_pcap_findalldevs) (pcap_if_t **, char *);
static void    (*p_pcap_freealldevs) (pcap_if_t *);
static void    (*p_pcap_freecode) (struct bpf_program *);
static char*   (*p_pcap_geterr) (pcap_t *);
static int     (*p_pcap_lookupnet) (const char *, bpf_u_int32 *, bpf_u_int32 *, char *);
static pcap_t* (*p_pcap_open_live) (const char *, int, int, int, char *);
static int     (*p_pcap_setmintocopy) (pcap_t* handle, int);
static HANDLE  (*p_pcap_getevent) (pcap_t *);
static int     (*p_pcap_sendpacket) (pcap_t* handle, const u_char* msg, int len);
static int     (*p_pcap_setfilter) (pcap_t *, struct bpf_program *);
static char*   (*p_pcap_lib_version) (void);

/* load function pointer from DLL */
void load_function(char* function, void** func_ptr) {
  *func_ptr = GetProcAddress(hDll, function);
  if (*func_ptr == 0) {
    char* msg = "Eth: Failed to find function '%s' in wpcap.dll\r\n";

    printf (msg, function);
    if (sim_log) fprintf (sim_log, msg, function);
    dll_loaded = 3;
  }
}

/* load wpcap.dll as required */
int load_wpcap(void) {
  switch(dll_loaded) {
    case 0:                  /* not loaded */
            /* attempt to load DLL */
      hDll = LoadLibrary(TEXT("wpcap.dll"));
      if (hDll == 0) {
        /* failed to load DLL */
        char* msg  = "Eth: Failed to load wpcap.dll\r\n";
        char* msg2 = "Eth: You must install WinPcap 4.x to use networking\r\n";

        printf (msg);
        printf (msg2);
        if (sim_log) {
          fprintf (sim_log, msg);
          fprintf (sim_log, msg2);
        }
        dll_loaded = 2;
        break;
      } else {
        /* DLL loaded OK */
        dll_loaded = 1;
      }

      /* load required functions; sets dll_load=3 on error */
      load_function("pcap_close",        (void**) &p_pcap_close);
      load_function("pcap_compile",      (void**) &p_pcap_compile);
      load_function("pcap_datalink",     (void**) &p_pcap_datalink);
      load_function("pcap_dispatch",     (void**) &p_pcap_dispatch);
      load_function("pcap_findalldevs",  (void**) &p_pcap_findalldevs);
      load_function("pcap_freealldevs",  (void**) &p_pcap_freealldevs);
      load_function("pcap_freecode",     (void**) &p_pcap_freecode);
      load_function("pcap_geterr",       (void**) &p_pcap_geterr);
      load_function("pcap_lookupnet",    (void**) &p_pcap_lookupnet);
      load_function("pcap_open_live",    (void**) &p_pcap_open_live);
      load_function("pcap_setmintocopy", (void**) &p_pcap_setmintocopy);
      load_function("pcap_getevent",     (void**) &p_pcap_getevent);
      load_function("pcap_sendpacket",   (void**) &p_pcap_sendpacket);
      load_function("pcap_setfilter",    (void**) &p_pcap_setfilter);
      load_function("pcap_lib_version",  (void**) &p_pcap_lib_version);

      if (dll_loaded == 1) {
        /* log successful load */
        char* version = p_pcap_lib_version();
        printf("%s\n", version);
        if (sim_log)
          fprintf(sim_log, "%s\n", version);
      }
      break;
    default:                /* loaded or failed */
      break;
  }
  return (dll_loaded == 1) ? 1 : 0;
}

/* define functions with dynamic revectoring */
void pcap_close(pcap_t* a) {
  if (load_wpcap() != 0) {
    p_pcap_close(a);
  }
}

int pcap_compile(pcap_t* a, struct bpf_program* b, char* c, int d, bpf_u_int32 e) {
  if (load_wpcap() != 0) {
    return p_pcap_compile(a, b, c, d, e);
  } else {
    return 0;
  }
}

int pcap_datalink(pcap_t* a) {
  if (load_wpcap() != 0) {
    return p_pcap_datalink(a);
  } else {
    return 0;
  }
}

int pcap_dispatch(pcap_t* a, int b, pcap_handler c, u_char* d) {
  if (load_wpcap() != 0) {
    return p_pcap_dispatch(a, b, c, d);
  } else {
    return 0;
  }
}

int pcap_findalldevs(pcap_if_t** a, char* b) {
  if (load_wpcap() != 0) {
    return p_pcap_findalldevs(a, b);
  } else {
    *a = 0;
    strcpy(b, no_wpcap);
    return -1;
  }
}

void pcap_freealldevs(pcap_if_t* a) {
  if (load_wpcap() != 0) {
    p_pcap_freealldevs(a);
  }
}

void pcap_freecode(struct bpf_program* a) {
  if (load_wpcap() != 0) {
    p_pcap_freecode(a);
  }
}

char* pcap_geterr(pcap_t* a) {
  if (load_wpcap() != 0) {
    return p_pcap_geterr(a);
  } else {
    return (char*) 0;
  }
}

int pcap_lookupnet(const char* a, bpf_u_int32* b, bpf_u_int32* c, char* d) {
  if (load_wpcap() != 0) {
    return p_pcap_lookupnet(a, b, c, d);
  } else {
    return 0;
  }
}

pcap_t* pcap_open_live(const char* a, int b, int c, int d, char* e) {
  if (load_wpcap() != 0) {
    return p_pcap_open_live(a, b, c, d, e);
  } else {
    return (pcap_t*) 0;
  }
}

int pcap_setmintocopy(pcap_t* a, int b) {
  if (load_wpcap() != 0) {
    return p_pcap_setmintocopy(a, b);
  } else {
    return 0;
  }
}

HANDLE pcap_getevent(pcap_t* a) {
  if (load_wpcap() != 0) {
    return p_pcap_getevent(a);
  } else {
    return (HANDLE) 0;
  }
}

int pcap_sendpacket(pcap_t* a, const u_char* b, int c) {
  if (load_wpcap() != 0) {
    return p_pcap_sendpacket(a, b, c);
  } else {
    return 0;
  }
}

int pcap_setfilter(pcap_t* a, struct bpf_program* b) {
  if (load_wpcap() != 0) {
    return p_pcap_setfilter(a, b);
  } else {
    return 0;
  }
}
#endif

/* Some platforms have always had pcap_sendpacket */
#if defined(_WIN32) || defined(__VMS)
#define HAS_PCAP_SENDPACKET 1
#else
/* The latest libpcap and WinPcap all have pcap_sendpacket */
#if !defined (NEED_PCAP_SENDPACKET)
#define HAS_PCAP_SENDPACKET 1
#endif
#endif

#if !defined (HAS_PCAP_SENDPACKET)
/* libpcap has no function to write a packet, so we need to implement
   pcap_sendpacket() for compatibility with the WinPcap base code.
   Return value: 0=Success, -1=Failure */
int pcap_sendpacket(pcap_t* handle, const u_char* msg, int len)
{
#if defined (__linux)
  return (send(pcap_fileno(handle), msg, len, 0) == len)? 0 : -1;
#else
  return (write(pcap_fileno(handle), msg, len) == len)? 0 : -1;
#endif /* linux */
}
#endif /* !HAS_PCAP_SENDPACKET */

#ifdef _WIN32
#include <Packet32.h>
#include <ntddndis.h>

static int pcap_mac_if_win32(char *AdapterName, UCHAR MACAddress[6])
{
  LPADAPTER         lpAdapter;
  PPACKET_OID_DATA  OidData;
  BOOLEAN           Status;
  int               ReturnValue;
  HINSTANCE         hDll;         /* handle to DLL */
  LPADAPTER (*p_PacketOpenAdapter)(PCHAR AdapterName);
  VOID (*p_PacketCloseAdapter)(LPADAPTER lpAdapter);
  BOOLEAN (*p_PacketRequest)(LPADAPTER  AdapterObject,BOOLEAN Set,PPACKET_OID_DATA  OidData);

  hDll = LoadLibrary(TEXT("packet.dll"));
  p_PacketOpenAdapter = (void *)GetProcAddress(hDll, "PacketOpenAdapter");
  p_PacketCloseAdapter = (void *)GetProcAddress(hDll, "PacketCloseAdapter");
  p_PacketRequest = (void *)GetProcAddress(hDll, "PacketRequest");
  
  /* Open the selected adapter */

  lpAdapter =   p_PacketOpenAdapter(AdapterName);

  if (!lpAdapter || (lpAdapter->hFile == INVALID_HANDLE_VALUE)) {
    FreeLibrary(hDll);
    return -1;
  }

  /* Allocate a buffer to get the MAC adress */

  OidData = malloc(6 + sizeof(PACKET_OID_DATA));
  if (OidData == NULL) {
    p_PacketCloseAdapter(lpAdapter);
    FreeLibrary(hDll);
    return -1;
  }

  /* Retrieve the adapter MAC querying the NIC driver */

  OidData->Oid = OID_802_3_CURRENT_ADDRESS;

  OidData->Length = 6;
  memset(OidData->Data, 0, 6);
	
  Status = p_PacketRequest(lpAdapter, FALSE, OidData);
  if(Status) {
    memcpy(MACAddress, OidData->Data, 6);
    ReturnValue = 0;
  } else
    ReturnValue = -1;

  free(OidData);
  p_PacketCloseAdapter(lpAdapter);
  FreeLibrary(hDll);
  return ReturnValue;
}
#endif

static void eth_get_nic_hw_addr(ETH_DEV* dev, char *devname)
{
  memset(&dev->host_nic_phy_hw_addr, 0, sizeof(dev->host_nic_phy_hw_addr));
  dev->have_host_nic_phy_addr = 0;
#ifdef _WIN32
  if (!pcap_mac_if_win32(devname, (UCHAR *)&dev->host_nic_phy_hw_addr))
    dev->have_host_nic_phy_addr = 1;
#elif !defined (__VMS)
{
  char command[1024];
  FILE *f;

  memset(command, 0, sizeof(command));
  snprintf(command, sizeof(command)-1, "ifconfig %s | grep [0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F] >NIC.hwaddr", devname);
  system(command);
  if (f = fopen("NIC.hwaddr", "r")) {
    if (fgets(command, sizeof(command)-1, f)) {
      char *p1, *p2;
        
      p1 = strchr(command, ':');
      while (p1) {
        p2 = strchr(p1+1, ':');
        if (p2 == p1+3) {
          int mac_bytes[6];
          if (6 == sscanf(p1-2, "%02x:%02x:%02x:%02x:%02x:%02x", &mac_bytes[0], &mac_bytes[1], &mac_bytes[2], &mac_bytes[3], &mac_bytes[4], &mac_bytes[5])) {
            dev->host_nic_phy_hw_addr[0] = mac_bytes[0];
            dev->host_nic_phy_hw_addr[1] = mac_bytes[1];
            dev->host_nic_phy_hw_addr[2] = mac_bytes[2];
            dev->host_nic_phy_hw_addr[3] = mac_bytes[3];
            dev->host_nic_phy_hw_addr[4] = mac_bytes[4];
            dev->host_nic_phy_hw_addr[5] = mac_bytes[5];
            dev->have_host_nic_phy_addr = 1;
          }
          break;
        }
        p1 = p2;
      }
    }
    fclose(f);
    remove("NIC.hwaddr");
  }
}
#endif
}

/* Forward declarations */
static void
_eth_callback(u_char* info, const struct pcap_pkthdr* header, const u_char* data);

static t_stat
_eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine);

#if defined (USE_READER_THREAD)
#include <pthread.h>

static void *
_eth_reader(void *arg)
{
ETH_DEV* volatile dev = (ETH_DEV*)arg;
int status;
int sched_policy;
struct sched_param sched_priority;
#if defined (_WIN32)
HANDLE hWait = pcap_getevent ((pcap_t*)dev->handle);
#endif
#if defined (MUST_DO_SELECT)
struct timeval timeout;
int sel_ret;
int pcap_fd;

if (dev->pcap_mode)
  pcap_fd = pcap_get_selectable_fd((pcap_t *)dev->handle);
else
  pcap_fd = dev->fd_handle;
#endif

  sim_debug(dev->dbit, dev->dptr, "Reader Thread Starting\n");

  /* Boost Priority for this I/O thread vs the CPU instruction execution 
     thread which in general won't be readily yielding the processor when 
     this thread needs to run */
  pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
  ++sched_priority.sched_priority;
  pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

  while ((dev->pcap_mode && dev->handle) || dev->fd_handle) {
#if defined (MUST_DO_SELECT)
    fd_set setl;
    FD_ZERO(&setl);
    FD_SET(pcap_fd, &setl);
    timeout.tv_sec = 0;
    timeout.tv_usec = 250*1000;
    sel_ret = select(1+pcap_fd, &setl, NULL, NULL, &timeout);
    if (sel_ret < 0 && errno != EINTR) break;
    if (sel_ret > 0) {
#else /* !MUST_DO_SELECT */
#if defined (_WIN32)
    if (WAIT_OBJECT_0 == WaitForSingleObject (hWait, 250)) {
#else
    if (1)  {
#endif
#endif /* MUST_DO_SELECT */
      if ((dev->pcap_mode) && (!dev->handle))
        break;
      if ((!dev->pcap_mode) && (!dev->fd_handle))
        break;
      /* dispatch read request queue available packets */
      if (dev->pcap_mode)
        status = pcap_dispatch ((pcap_t*)dev->handle, -1, &_eth_callback, (u_char*)dev);
#ifdef USE_TAP_NETWORK
      else {
        struct pcap_pkthdr header;
        int len;
        u_char buf[ETH_MAX_JUMBO_FRAME];

        memset(&header, 0, sizeof(header));
        len = read(dev->fd_handle, buf, sizeof(buf));
        if (len > 0) {
          status = 1;
          header.len = len;
          _eth_callback((u_char *)dev, &header, buf);
        } else {
          status = 0;
        }
      }
#endif /* USE_TAP_NETWORK */

      if ((status > 0) && (dev->asynch_io)) {
        int wakeup_needed;
        pthread_mutex_lock (&dev->lock);
        wakeup_needed = (dev->read_queue.count != 0);
        pthread_mutex_unlock (&dev->lock);
        if (wakeup_needed) {
          sim_debug(dev->dbit, dev->dptr, "Queueing automatic poll\n");
          sim_activate_abs (dev->dptr->units, dev->asynch_io_latency);
          }
        }
      }
  }

  sim_debug(dev->dbit, dev->dptr, "Reader Thread Exiting\n");
  return NULL;
}

static void *
_eth_writer(void *arg)
{
ETH_DEV* volatile dev = (ETH_DEV*)arg;
struct write_request *request;
int sched_policy;
struct sched_param sched_priority;

  /* Boost Priority for this I/O thread vs the CPU instruction execution 
     thread which in general won't be readily yielding the processor when 
     this thread needs to run */
  pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
  ++sched_priority.sched_priority;
  pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

  sim_debug(dev->dbit, dev->dptr, "Writer Thread Starting\n");

  pthread_mutex_lock (&dev->writer_lock);
  while ((dev->pcap_mode && dev->handle) || dev->fd_handle) {
    pthread_cond_wait (&dev->writer_cond, &dev->writer_lock);
    while (request = dev->write_requests) {
      /* Pull buffer off request list */
      dev->write_requests = request->next;
      pthread_mutex_unlock (&dev->writer_lock);
      
      dev->write_status = _eth_write(dev, &request->packet, NULL);
      
      pthread_mutex_lock (&dev->writer_lock);
      /* Put buffer on free buffer list */
      request->next = dev->write_buffers;
      dev->write_buffers = request;
    }
  }
  pthread_mutex_unlock (&dev->writer_lock);

  sim_debug(dev->dbit, dev->dptr, "Writer Thread Exiting\n");
  return NULL;
}
#endif

t_stat eth_set_async (ETH_DEV *dev, int latency)
{
#if !defined(USE_READER_THREAD) || !defined(SIM_ASYNCH_IO)
  char *msg = "Eth: can't operate asynchronously, must poll\r\n";
  printf ("%s", msg);
  if (sim_log) fprintf (sim_log, "%s", msg);
  return SCPE_NOFNC;
#else
  int wakeup_needed;

  dev->asynch_io = 1;
  dev->asynch_io_latency = latency;
  pthread_mutex_lock (&dev->lock);
  wakeup_needed = (dev->read_queue.count != 0);
  pthread_mutex_unlock (&dev->lock);
  if (wakeup_needed) {
    sim_debug(dev->dbit, dev->dptr, "Queueing automatic poll\n");
    sim_activate_abs (dev->dptr->units, dev->asynch_io_latency);
  }
#endif
  return SCPE_OK;
}

t_stat eth_clr_async (ETH_DEV *dev)
{
#if !defined(USE_READER_THREAD) || !defined(SIM_ASYNCH_IO)
  return SCPE_NOFNC;
#else
  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  dev->asynch_io = 0;
  return SCPE_OK;
#endif
}

t_stat eth_open(ETH_DEV* dev, char* name, DEVICE* dptr, uint32 dbit)
{
  int bufsz = (BUFSIZ < ETH_MAX_PACKET) ? ETH_MAX_PACKET : BUFSIZ;
  char errbuf[PCAP_ERRBUF_SIZE];
  char temp[1024];
  char* savname = name;
  int   num;
  char* msg;

  if (bufsz < ETH_MAX_JUMBO_FRAME)
      bufsz = ETH_MAX_JUMBO_FRAME;  /* Enable handling of jumbo frames */

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
    if (savname == 0) /* didn't translate */
      return SCPE_OPENERR;
  } else {
    /* are they trying to use device description? */
    savname = eth_getname_bydesc(name, temp);
    if (savname == 0) { /* didn't translate */
      /* probably is not ethX and has no description */
      savname = eth_getname_byname(name, temp);
      if (savname == 0) /* didn't translate */
        savname = name;
    }
  }

  /* attempt to connect device */
  memset(errbuf, 0, sizeof(errbuf));
  if (strncmp("tap:", savname, 4)) {
    dev->handle = (void*) pcap_open_live(savname, bufsz, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
    if (!dev->handle) { /* can't open device */
      msg = "Eth: pcap_open_live error - %s\r\n";
      printf (msg, errbuf);
      if (sim_log) fprintf (sim_log, msg, errbuf);
      return SCPE_OPENERR;
    }
    dev->pcap_mode = 1;
  } else {
    int  tun = -1;    /* TUN/TAP Socket */
    int  on = 1;
    char dev_name[64] = "";

#if defined(USE_TAP_NETWORK)
    if (!strcmp(savname, "tap:tapN")) {
      msg = "Eth: Must specify actual tap device name (i.e. tap:tap0)\r\n";
      printf (msg, errbuf);
      if (sim_log) fprintf (sim_log, msg, errbuf);
      return SCPE_OPENERR;
    }
#endif
#if defined(__linux) && defined(USE_TAP_NETWORK)
    if ((tun = open("/dev/net/tun", O_RDWR)) >= 0) {
      struct ifreq ifr; /* Interface Requests */

      memset(&ifr, 0, sizeof(ifr));
      /* Set up interface flags */
      strcpy(ifr.ifr_name, savname+4);
      ifr.ifr_flags = IFF_TAP|IFF_NO_PI;

      // Send interface requests to TUN/TAP driver.
      if (ioctl(tun, TUNSETIFF, &ifr) >= 0) {
        if (ioctl(tun, FIONBIO, &on)) {
          strncpy(errbuf, strerror(errno), sizeof(errbuf)-1);
          close(tun);
        } else {
          dev->fd_handle = tun;
          strcpy(savname, ifr.ifr_name);
        }
      } else
        strncpy(errbuf, strerror(errno), sizeof(errbuf)-1);
    } else
      strncpy(errbuf, strerror(errno), sizeof(errbuf)-1);
#elif defined(USE_BSDTUNTAP) && defined(USE_TAP_NETWORK)
    snprintf(dev_name, sizeof(dev_name)-1, "/dev/%s", savname+4);
    dev_name[sizeof(dev_name)-1] = '\0';

    if ((tun = open(dev_name, O_RDWR)) >= 0) {
      if (ioctl(tun, FIONBIO, &on)) {
        strncpy(errbuf, strerror(errno), sizeof(errbuf)-1);
        close(tun);
      } else {
        dev->fd_handle = tun;
        strcpy(savname, savname+4);
      }
    } else {
      strncpy(errbuf, strerror(errno), sizeof(errbuf)-1);
    }
#else
    strncpy(errbuf, "No support for tap: devices", sizeof(errbuf)-1);
#endif /* !defined(__linux) && !defined(USE_BSDTUNTAP) */
  }
  if (errbuf[0]) {
    msg = "Eth: open error - %s\r\n";
    printf (msg, errbuf);
    if (sim_log) fprintf (sim_log, msg, errbuf);
    return SCPE_OPENERR;
  }
  msg = "Eth: opened OS device %s\r\n";
  printf (msg, savname);
  if (sim_log) fprintf (sim_log, msg, savname);

  /* get the NIC's hardware MAC address */
  eth_get_nic_hw_addr(dev, savname);

  /* save name of device */
  dev->name = malloc(strlen(savname)+1);
  strcpy(dev->name, savname);

  /* save debugging information */
  dev->dptr = dptr;
  dev->dbit = dbit;

#if !defined(HAS_PCAP_SENDPACKET) && defined (xBSD) && !defined (__APPLE__)
  /* Tell the kernel that the header is fully-formed when it gets it.
     This is required in order to fake the src address. */
  if (dev->pcap_mode) {
    int one = 1;
    ioctl(pcap_fileno(dev->handle), BIOCSHDRCMPLT, &one);
  }
#endif /* xBSD */

#if defined (USE_READER_THREAD)
  {
  pthread_attr_t attr;

#if defined(_WIN32)
  pcap_setmintocopy (dev->handle, 0);
#endif
  ethq_init (&dev->read_queue, 200);         /* initialize FIFO queue */
  pthread_mutex_init (&dev->lock, NULL);
  pthread_mutex_init (&dev->writer_lock, NULL);
  pthread_cond_init (&dev->writer_cond, NULL);
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
  pthread_create (&dev->reader_thread, &attr, _eth_reader, (void *)dev);
  pthread_create (&dev->writer_thread, &attr, _eth_writer, (void *)dev);
  pthread_attr_destroy(&attr);
  }
#else /* !defined (USE_READER_THREAD */
#ifdef USE_SETNONBLOCK
  /* set ethernet device non-blocking so pcap_dispatch() doesn't hang */
  if (dev->pcap_mode && (pcap_setnonblock (dev->handle, 1, errbuf) == -1)) {
    msg = "Eth: Failed to set non-blocking: %s\r\n";
    printf (msg, errbuf);
    if (sim_log) fprintf (sim_log, msg, errbuf);
  }
#endif
#endif /* !defined (USE_READER_THREAD */
#if defined (__APPLE__)
  if (1) {
    /* Deliver packets immediately, needed for OS X 10.6.2 and later
     * (Snow-Leopard).
     * See this thread on libpcap and Mac Os X 10.6 Snow Leopard on
     * the tcpdump mailinglist: http://seclists.org/tcpdump/2010/q1/110
     */
    int v = 1;
    ioctl(pcap_fileno(dev->handle), BIOCIMMEDIATE, &v);
  }
#endif
  return SCPE_OK;
}

t_stat eth_close(ETH_DEV* dev)
{
  char* msg = "Eth: closed %s\r\n";
  pcap_t *pcap;
  int pcap_fd = dev->fd_handle;

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* close the device */
  pcap = (pcap_t *)dev->handle;
  dev->handle = NULL;
  dev->fd_handle = 0;
  dev->have_host_nic_phy_addr = 0;

#if defined (USE_READER_THREAD)
  pthread_join (dev->reader_thread, NULL);
  pthread_mutex_destroy (&dev->lock);
  pthread_cond_signal (&dev->writer_cond);
  pthread_join (dev->writer_thread, NULL);
  pthread_mutex_destroy (&dev->writer_lock);
  pthread_cond_destroy (&dev->writer_cond);
  if (1) {
    struct write_request *buffer;

    while (buffer = dev->write_buffers) {
      dev->write_buffers = buffer->next;
      free(buffer);
    }
    while (buffer = dev->write_requests) {
      dev->write_requests = buffer->next;
      free(buffer);
    }
  }
  ethq_destroy (&dev->read_queue);         /* release FIFO queue */
#endif

  if (dev->pcap_mode)
    pcap_close(pcap);
#ifdef USE_TAP_NETWORK
  else
    close(pcap_fd);
#endif
  printf (msg, dev->name);
  if (sim_log) fprintf (sim_log, msg, dev->name);

  /* clean up the mess */
  free(dev->name);
  eth_zero(dev);

  return SCPE_OK;
}

t_stat eth_check_address_conflict (ETH_DEV* dev, 
                                   ETH_MAC* const mac)
{
  ETH_PACK send, recv;
  t_stat status;
  int responses = 0;

  sim_debug(dev->dbit, dev->dptr, "Determining Address Conflict...\n");

  /* build a loopback forward request packet */
  memset (&send, 0, sizeof(ETH_PACK));
  send.len = ETH_MIN_PACKET;                              /* minimum packet size */
  memcpy(&send.msg[0], mac, sizeof(ETH_MAC));             /* target address */
  memcpy(&send.msg[6], mac, sizeof(ETH_MAC));             /* source address */
  send.msg[12] = 0x90;                                    /* loopback packet type */
  send.msg[14] = 0;                                       /* Offset */
  send.msg[16] = 2;                                       /* Forward */
  memcpy(&send.msg[18], mac, sizeof(ETH_MAC));            /* Forward Destination */
  send.msg[24] = 1;                                       /* Reply */

  eth_filter(dev, 1, (ETH_MAC *)mac, 0, 0);

  /* send the packet */
  status = _eth_write (dev, &send, NULL);
  if (status != SCPE_OK) {
    char *msg;
    msg = "Eth: Error Transmitting packet: %s\r\n"
          "You may need to run as root, or install a libpcap version\r\n"
          "which is at least 0.9 from www.tcpdump.org\r\n";
    printf(msg, strerror(errno));
    if (sim_log) fprintf (sim_log, msg, strerror(errno));
    return status;
  }

  sim_os_ms_sleep (300);   /* time for a conflicting host to respond */

  /* empty the read queue and count the responses */
  do {
    memset (&recv, 0, sizeof(ETH_PACK));
    status = eth_read (dev, &recv, NULL);
    if (memcmp(send.msg, recv.msg, 14)== 0)
      responses++;
  } while (recv.len > 0);

  sim_debug(dev->dbit, dev->dptr, "Address Conflict = %d\n", responses);
  return responses;
}

t_stat eth_reflect(ETH_DEV* dev)
{
  /* Test with an address no NIC should have. */
  /* We do this to avoid reflections from the wire, */
  /* in the event that a simulated NIC has a MAC address conflict. */
  ETH_MAC mac = {0xfe,0xff,0xff,0xff,0xff,0xfe};

  sim_debug(dev->dbit, dev->dptr, "Determining Reflections...\n");

  dev->reflections = 0;
  dev->reflections = eth_check_address_conflict (dev, &mac);

  sim_debug(dev->dbit, dev->dptr, "Reflections = %d\n", dev->reflections);
  return dev->reflections;
}

static
t_stat _eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
  int status = 1;   /* default to failure */

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* make sure packet exists */
  if (!packet) return SCPE_ARG;

  /* make sure packet is acceptable length */
  if ((packet->len >= ETH_MIN_PACKET) && (packet->len <= ETH_MAX_PACKET)) {
    eth_packet_trace (dev, packet->msg, packet->len, "writing");

    /* dispatch write request (synchronous; no need to save write info to dev) */
    if (dev->pcap_mode)
      status = pcap_sendpacket((pcap_t*)dev->handle, (u_char*)packet->msg, packet->len);
#ifdef USE_TAP_NETWORK
    else
      status = ((packet->len == write(dev->fd_handle, (void *)packet->msg, packet->len)) ? 0 : -1);
#endif

    /* detect sending of loopback packet */
    if ((status == 0) && (LOOPBACK_SELF_FRAME(dev->physical_addr, packet->msg))) {
        dev->loopback_self_sent += dev->reflections;
        dev->loopback_self_sent_total++;
    }

  } /* if packet->len */

  /* call optional write callback function */
  if (routine)
    (routine)(status);

  return ((status == 0) ? SCPE_OK : SCPE_IOERR);
}

t_stat eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
#ifdef USE_READER_THREAD
  struct write_request *request;
  int write_queue_size = 1;

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* Get a buffer */
  pthread_mutex_lock (&dev->writer_lock);
  if (request = dev->write_buffers)
    dev->write_buffers = request->next;
  pthread_mutex_unlock (&dev->writer_lock);
  if (!request)
    request = malloc(sizeof(*request));

  /* Copy buffer contents */
  request->packet.len = packet->len;
  request->packet.used = packet->used;
  request->packet.status = packet->status;
  request->packet.crc_len = packet->crc_len;
  memcpy(request->packet.msg, packet->msg, packet->len);

  /* Insert buffer at the end of the write list (to make sure that */
  /* packets make it to the wire in the order they were presented here) */
  pthread_mutex_lock (&dev->writer_lock);
  request->next = NULL;
  if (dev->write_requests) {
    struct write_request *last_request = dev->write_requests;

    ++write_queue_size;
    while (last_request->next) {
      last_request = last_request->next;
      ++write_queue_size;
    }
    last_request->next = request;
  } else
    dev->write_requests = request;
  if (write_queue_size > dev->write_queue_peak)
    dev->write_queue_peak = write_queue_size;
  pthread_mutex_unlock (&dev->writer_lock);

  /* Awaken writer thread to perform actual write */
  pthread_cond_signal (&dev->writer_cond);

  /* Return with a status from some prior write */
  if (routine)
      (routine)(dev->write_status);
  return dev->write_status;
#else
  return _eth_write(dev, packet, routine);
#endif
}

static int
_eth_hash_lookup(ETH_MULTIHASH hash, const u_char* data)
{
  int key = 0x3f & (eth_crc32(0, data, 6) >> 26);

  key ^= 0x3f;
  return (hash[key>>3] & (1 << (key&0x7)));
}

static int
_eth_hash_validate(ETH_MAC *MultiCastList, int count, ETH_MULTIHASH hash)
{
  ETH_MULTIHASH lhash;
  int i;

  memset(lhash, 0, sizeof(lhash));
  for (i=0; i<count; ++i) {
    int key = 0x3f & (eth_crc32(0, MultiCastList[i], 6) >> 26);

    key ^= 0x3F;
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X Key: %X, Byte: %X, Val: %X\n", 
        MultiCastList[i][0], MultiCastList[i][1], MultiCastList[i][2], MultiCastList[i][3], MultiCastList[i][4], MultiCastList[i][5], 
        key, key>>3, (1 << (key&0x7)));
    lhash[key>>3] |= (1 << (key&0x7));
  }
  if (memcmp(hash, lhash, sizeof(lhash))) {
    printf("Inconsistent Computed Hash:\n");
    printf("Should be: %02X %02X %02X %02X %02X %02X %02X %02X\n", 
            hash[0], hash[1], hash[2], hash[3], 
            hash[4], hash[5], hash[6], hash[7]);
    printf("Was:       %02X %02X %02X %02X %02X %02X %02X %02X\n", 
            lhash[0], lhash[1], lhash[2], lhash[3], 
            lhash[4], lhash[5], lhash[6], lhash[7]);
  }
    printf("Should be: %02X %02X %02X %02X %02X %02X %02X %02X\n", 
            hash[0], hash[1], hash[2], hash[3], 
            hash[4], hash[5], hash[6], hash[7]);
    printf("Was:       %02X %02X %02X %02X %02X %02X %02X %02X\n", 
            lhash[0], lhash[1], lhash[2], lhash[3], 
            lhash[4], lhash[5], lhash[6], lhash[7]);
  return 0;
}

static void
_eth_test_multicast_hash()
{
  ETH_MAC tMacs[] = {
                     {0xAB, 0x00, 0x04, 0x01, 0xAC, 0x10},
                     {0xAB, 0x00, 0x00, 0x04, 0x00, 0x00},
                     {0x09, 0x00, 0x2B, 0x00, 0x00, 0x0F},
                     {0x09, 0x00, 0x2B, 0x02, 0x01, 0x04},
                     {0x09, 0x00, 0x2B, 0x02, 0x01, 0x07},
                     {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                     {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}};
  ETH_MULTIHASH thash = {0x01, 0x40, 0x00, 0x00, 0x48, 0x88, 0x40, 0x00};

  _eth_hash_validate(tMacs, sizeof(tMacs)/sizeof(tMacs[0]), thash);
}

/* The IP header */
struct IPHeader {
  uint8 verhlen;          /* Version & Header Length in dwords */
#define IP_HLEN(IP) (((IP)->verhlen&0xF)<<2) /* Header Length in Bytes */
#define IP_VERSION(IP) ((((IP)->verhlen)>>4)&0xF) /* IP Version */
  uint8 tos;              /* Type of service */
  uint16 total_len;       /* Length of the packet in dwords */
  uint16 ident;           /* unique identifier */
  uint16 flags;           /* Fragmentation Flags */
#define IP_DF_FLAG (0x4000)
#define IP_MF_FLAG (0x2000)
#define IP_OFFSET_MASK (0x1FFF)
#define IP_FRAG_DF(IP) (ntohs(((IP)->flags))&IP_DF_FLAG)
#define IP_FRAG_MF(IP) (ntohs(((IP)->flags))&IP_MF_FLAG)
#define IP_FRAG_OFFSET(IP) (ntohs(((IP)->flags))&IP_OFFSET_MASK)
  uint8 ttl;              /* Time to live */
  uint8 proto;            /* Protocol number (TCP, UDP etc) */
  uint16 checksum;        /* IP checksum */
  uint32 source_ip;       /* Source Address */
  uint32 dest_ip;         /* Destination Address */
};

/* ICMP header */
struct ICMPHeader {
  uint8 type;          /* ICMP packet type */
  uint8 code;          /* Type sub code */
  uint16 checksum;     /* ICMP Checksum */
  uint32 otherstuff[1];/* optional data */
};

struct UDPHeader {
  uint16 source_port;
  uint16 dest_port;
  uint16 length;      /* The length of the entire UDP datagram, including both header and Data fields. */
  uint16 checksum;
};

struct TCPHeader {
  uint16 source_port;
  uint16 dest_port;
  uint32 sequence_number;
  uint32 acknowledgement_number;
  uint16 data_offset_and_flags;
#define TCP_DATA_OFFSET(TCP) ((ntohs((TCP)->data_offset_and_flags)>>12)<<2)
#define TCP_CWR_FLAG (0x80)
#define TCP_ECR_FLAG (0x40)
#define TCP_URG_FLAG (0x20)
#define TCP_ACK_FLAG (0x10)
#define TCP_PSH_FLAG (0x08)
#define TCP_RST_FLAG (0x04)
#define TCP_SYN_FLAG (0x02)
#define TCP_FIN_FLAG (0x01)
#define TCP_FLAGS_MASK (0xFF)
  uint16 window;
  uint16 checksum;
  uint16 urgent;
  uint16 otherstuff[1];  /* The rest of the packet */
};

#ifndef IPPROTO_TCP
#define IPPROTO_TCP             6               /* tcp */
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP             17              /* user datagram protocol */
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP            1               /* control message protocol */
#endif

static uint16 
ip_checksum(uint16 *buffer, int size) 
{
  unsigned long cksum = 0;
    
  /* Sum all the words together, adding the final byte if size is odd  */
  while (size > 1) {
    cksum += *buffer++;
    size -= sizeof(*buffer);
  }
  if (size) {
    uint8 endbytes[2];

    endbytes[0] = *((uint8 *)buffer);
    endbytes[1] = 0;
    cksum += *((uint16 *)endbytes);
  }

  /* Do a little shuffling  */
  cksum = (cksum >> 16) + (cksum & 0xffff);
  cksum += (cksum >> 16);
    
  /* Return the bitwise complement of the resulting mishmash  */
  return (uint16)(~cksum);
}

static uint16 
pseudo_checksum(uint16 len, uint16 proto, uint16 *src_addr, uint16 *dest_addr, uint8 *buff)
{
  uint32 sum;

  /* Sum the data first */
  sum = 0xffff&(~ip_checksum((uint16 *)buff, len));

  /* add the pseudo header which contains the IP source and destinationn addresses */
  sum += src_addr[0];
  sum += src_addr[1];
  sum += dest_addr[0];
  sum += dest_addr[1];
  /* and the protocol number and the length of the UDP packet */
  sum = sum + htons(proto) + htons(len);

  /* Do a little shuffling  */
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
    
  /* Return the bitwise complement of the resulting mishmash  */
  return (uint16)(~sum);
}

static void
_eth_fix_ip_jumbo_offload(ETH_DEV* dev, const u_char* msg, int len)
{
  unsigned short* proto = (unsigned short*) &msg[12];
  struct IPHeader *IP;
  struct TCPHeader *TCP = NULL;
  struct UDPHeader *UDP;
  struct ICMPHeader *ICMP;
  uint16 orig_checksum;
  uint16 payload_len;
  uint16 mtu_payload;
  uint16 ip_flags;
  uint16 frag_offset;
  struct pcap_pkthdr header;
  uint16 tcp_flags;

  /* Only interested in IP frames */
  if (ntohs(*proto) != 0x0800) {
    ++dev->jumbo_dropped; /* Non IP Frames are dropped */
    return;
  }
  IP = (struct IPHeader *)&msg[14];
  if (IP_VERSION(IP) != 4) {
    ++dev->jumbo_dropped; /* Non IPv4 jumbo frames are dropped */
    return;
  }
  if ((IP_HLEN(IP) > len) || (ntohs(IP->total_len) > len)) {
    ++dev->jumbo_dropped; /* Bogus header length frames are dropped */
    return;
  }
  if (IP_FRAG_OFFSET(IP) || IP_FRAG_MF(IP)) {
    ++dev->jumbo_dropped; /* Previously fragmented, but currently jumbo sized frames are dropped */
    return;
  }
  switch (IP->proto) {
   case IPPROTO_UDP:
      UDP = (struct UDPHeader *)(((char *)IP)+IP_HLEN(IP));
      if (ntohs(UDP->length) > (len-IP_HLEN(IP))) {
        ++dev->jumbo_dropped; /* Bogus UDP packet length (packet contained length exceeds packet size) frames are dropped */
        return;
      }
      if (UDP->checksum == 0)
        break; /* UDP Cghecksums are disabled */
      orig_checksum = UDP->checksum;
      UDP->checksum = 0;
      UDP->checksum = pseudo_checksum(ntohs(UDP->length), IPPROTO_UDP, (uint16 *)(&IP->source_ip), (uint16 *)(&IP->dest_ip), (uint8 *)UDP);
      if (orig_checksum != UDP->checksum)
        eth_packet_trace (dev, msg, len, "reading jumbo UDP header Checksum Fixed");
      break;
   case IPPROTO_ICMP:
      ICMP = (struct ICMPHeader *)(((char *)IP)+IP_HLEN(IP));
      orig_checksum = ICMP->checksum;
      ICMP->checksum = 0;
      ICMP->checksum = ip_checksum((uint16 *)ICMP, ntohs(IP->total_len)-IP_HLEN(IP));
      if (orig_checksum != ICMP->checksum)
        eth_packet_trace (dev, msg, len, "reading jumbo ICMP header Checksum Fixed");
      break;
   case IPPROTO_TCP:
      TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
      if ((TCP_DATA_OFFSET(TCP) > (len-IP_HLEN(IP))) || (TCP_DATA_OFFSET(TCP) < 20)) {
        ++dev->jumbo_dropped; /* Bogus TCP packet header length (packet contained length exceeds packet size) frames are dropped */
        return;
      }
      /* We don't do anything with the TCP checksum since we're going to resegment the TCP data below */
      break;
   default:
      ++dev->jumbo_dropped; /* We onlt handle UDP, ICMP and TCP jumbo frames others are dropped */
      return;
  }
  /* Reasonable Checksums are now in the jumbo packet, but we've got to actually */
  /* deliver ONLY standard sized ethernet frames.  Our job here is to now act as */
  /* a router might have to and fragment these IPv4 frames as they are delivered */
  /* into the virtual NIC. We do this by walking down the packet and dispatching */
  /* a chunk at a time recomputing an appropriate header for each chunk. For */
  /* datagram oriented protocols (UDP and ICMP) this is done by simple packet */
  /* fragmentation.  For TCP this is done by breaking large packets into separate */
  /* TCP packets. */
  memset(&header, 0, sizeof(header));
  switch (IP->proto) {
   case IPPROTO_UDP:
   case IPPROTO_ICMP:
      ++dev->jumbo_fragmented;
      payload_len = ntohs(IP->total_len) - IP_HLEN(IP);
      mtu_payload = ETH_MIN_JUMBO_FRAME - 14 - IP_HLEN(IP);
      frag_offset = 0;
      while (payload_len > 0) {
        ip_flags = frag_offset;
        if (payload_len > mtu_payload) {
          ip_flags |= IP_MF_FLAG;
          IP->total_len = htons(((mtu_payload>>3)<<3) + IP_HLEN(IP));
        } else {
          IP->total_len = htons(payload_len + IP_HLEN(IP));
        }
        IP->flags = htons(ip_flags);
        IP->checksum = 0;
        IP->checksum = ip_checksum((uint16 *)IP, IP_HLEN(IP));
        header.len = 14 + ntohs(IP->total_len);
        eth_packet_trace (dev, ((u_char *)IP)-14, header.len, "reading Datagram fragment");
#if ETH_MIN_JUMBO_FRAME < ETH_MAX_PACKET
          { /* Debugging is easier it we read packets directly with pcap */
          ETH_PACK pkt;

          memset(&pkt, 0, sizeof(pkt));
          memcpy(pkt.msg, ((u_char *)IP)-14, header.len);
          pkt.len = header.len;
          _eth_write(dev, &pkt, NULL);
          }
#else
        _eth_callback((u_char *)dev, &header, ((u_char *)IP)-14);
#endif
        payload_len -= (ntohs(IP->total_len) - IP_HLEN(IP));
        frag_offset += (ntohs(IP->total_len) - IP_HLEN(IP))>>3;
        if (payload_len > 0) {
          /* Move the MAC and IP headers down to just prior to the next payload segment */
          memcpy(((u_char *)IP) + ntohs(IP->total_len) - (14 + IP_HLEN(IP)), ((u_char *)IP) - 14, 14 + IP_HLEN(IP));
          IP = (struct IPHeader *)(((u_char *)IP) + ntohs(IP->total_len) - IP_HLEN(IP));
        }
      }
      break;
   case IPPROTO_TCP:
      ++dev->jumbo_fragmented;
      TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
      tcp_flags = ntohs(TCP->data_offset_and_flags)&TCP_FLAGS_MASK;
      TCP->data_offset_and_flags = htons(((TCP_DATA_OFFSET(TCP)>>2)<<12)|TCP_ACK_FLAG);
      payload_len = ntohs(IP->total_len) - IP_HLEN(IP) - TCP_DATA_OFFSET(TCP);
      mtu_payload = ETH_MIN_JUMBO_FRAME - 14 - IP_HLEN(IP) - TCP_DATA_OFFSET(TCP);
      while (payload_len > 0) {
        if (payload_len > mtu_payload) {
          IP->total_len = htons(mtu_payload + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
        } else {
          TCP->data_offset_and_flags = htons(ntohs(TCP->data_offset_and_flags)|(tcp_flags&(TCP_PSH_FLAG|TCP_ACK_FLAG|TCP_FIN_FLAG|TCP_RST_FLAG)));
          IP->total_len = htons(payload_len + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
        }
        IP->checksum = 0;
        IP->checksum = ip_checksum((uint16 *)IP, IP_HLEN(IP));
        TCP->checksum = 0;
        TCP->checksum = pseudo_checksum(ntohs(IP->total_len)-IP_HLEN(IP), IPPROTO_TCP, (uint16 *)(&IP->source_ip), (uint16 *)(&IP->dest_ip), (uint8 *)TCP);
        header.len = 14 + ntohs(IP->total_len);
        eth_packet_trace_ex (dev, ((u_char *)IP)-14, header.len, "reading TCP segment", 1, dev->dbit);
#if ETH_MIN_JUMBO_FRAME < ETH_MAX_PACKET
          { /* Debugging is easier it we read packets directly with pcap */
          ETH_PACK pkt;

          memset(&pkt, 0, sizeof(pkt));
          memcpy(pkt.msg, ((u_char *)IP)-14, header.len);
          pkt.len = header.len;
          _eth_write(dev, &pkt, NULL);
          }
#else
        _eth_callback((u_char *)dev, &header, ((u_char *)IP)-14);
#endif
        payload_len -= (ntohs(IP->total_len) - (IP_HLEN(IP) + TCP_DATA_OFFSET(TCP)));
        if (payload_len > 0) {
          /* Move the MAC, IP and TCP headers down to just prior to the next payload segment */
          memcpy(((u_char *)IP) + ntohs(IP->total_len) - (14 + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP)), ((u_char *)IP) - 14, 14 + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
          IP = (struct IPHeader *)(((u_char *)IP) + ntohs(IP->total_len) - (IP_HLEN(IP) + TCP_DATA_OFFSET(TCP)));
          TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
          TCP->sequence_number = htonl(mtu_payload + ntohl(TCP->sequence_number));
        }
      }
      break;
  }
}

static void
_eth_fix_ip_xsum_offload(ETH_DEV* dev, u_char* msg, int len)
{
  unsigned short* proto = (unsigned short*) &msg[12];
  struct IPHeader *IP;
  struct TCPHeader *TCP;
  struct UDPHeader *UDP;
  struct ICMPHeader *ICMP;
  uint16 orig_checksum;

  /* Only need to process locally originated packets */
  if ((!dev->have_host_nic_phy_addr) || (memcmp(msg+6, dev->host_nic_phy_hw_addr, 6)))
    return;
  /* Only interested in IP frames */
  if (ntohs(*proto) != 0x0800)
    return;
  IP = (struct IPHeader *)&msg[14];
  if (IP_VERSION(IP) != 4)
    return; /* Only interested in IPv4 frames */
  if ((IP_HLEN(IP) > len) || (ntohs(IP->total_len) > len))
    return; /* Bogus header length */
  orig_checksum = IP->checksum;
  IP->checksum = 0;
  IP->checksum = ip_checksum((uint16 *)IP, IP_HLEN(IP));
  if (orig_checksum != IP->checksum)
    eth_packet_trace (dev, msg, len, "reading IP header Checksum Fixed");
  if (IP_FRAG_OFFSET(IP) || IP_FRAG_MF(IP))
    return; /* Insufficient data to compute payload checksum */
  switch (IP->proto) {
   case IPPROTO_UDP:
      UDP = (struct UDPHeader *)(((char *)IP)+IP_HLEN(IP));
      if (ntohs(UDP->length) > (len-IP_HLEN(IP)))
        return; /* packet contained length exceeds packet size */
      if (UDP->checksum == 0)
        return; /* UDP Cghecksums are disabled */
      orig_checksum = UDP->checksum;
      UDP->checksum = 0;
      UDP->checksum = pseudo_checksum(ntohs(UDP->length), IPPROTO_UDP, (uint16 *)(&IP->source_ip), (uint16 *)(&IP->dest_ip), (uint8 *)UDP);
      if (orig_checksum != UDP->checksum)
        eth_packet_trace (dev, msg, len, "reading UDP header Checksum Fixed");
      break;
   case IPPROTO_TCP:
      TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
      orig_checksum = TCP->checksum;
      TCP->checksum = 0;
      TCP->checksum = pseudo_checksum(ntohs(IP->total_len)-IP_HLEN(IP), IPPROTO_TCP, (uint16 *)(&IP->source_ip), (uint16 *)(&IP->dest_ip), (uint8 *)TCP);
      if (orig_checksum != TCP->checksum)
        eth_packet_trace (dev, msg, len, "reading TCP header Checksum Fixed");
      break;
   case IPPROTO_ICMP:
      ICMP = (struct ICMPHeader *)(((char *)IP)+IP_HLEN(IP));
      orig_checksum = ICMP->checksum;
      ICMP->checksum = 0;
      ICMP->checksum = ip_checksum((uint16 *)ICMP, ntohs(IP->total_len)-IP_HLEN(IP));
      if (orig_checksum != ICMP->checksum)
        eth_packet_trace (dev, msg, len, "reading ICMP header Checksum Fixed");
      break;
  }
}

static void
_eth_callback(u_char* info, const struct pcap_pkthdr* header, const u_char* data)
{
  ETH_DEV*  dev = (ETH_DEV*) info;
#ifdef USE_BPF
  int to_me = 1;

  /* AUTODIN II hash mode? */
  if ((dev->hash_filter) && (data[0] & 0x01) && (!dev->promiscuous) && (!dev->all_multicast))
    to_me = _eth_hash_lookup(dev->hash, data);
#else /* !USE_BPF */
  int to_me = 0;
  int from_me = 0;
  int i;

  eth_packet_trace (dev, data, header->len, "received");

  for (i = 0; i < dev->addr_count; i++) {
    if (memcmp(data, dev->filter_address[i], 6) == 0) to_me = 1;
    if (memcmp(&data[6], dev->filter_address[i], 6) == 0) from_me = 1;
  }

  /* all multicast mode? */
  if (dev->all_multicast && (data[0] & 0x01)) to_me = 1;

  /* promiscuous mode? */
  if (dev->promiscuous) to_me = 1;

  /* AUTODIN II hash mode? */
  if ((dev->hash_filter) && (!to_me) && (data[0] & 0x01))
    to_me = _eth_hash_lookup(dev->hash, data);
#endif /* USE_BPF */

  /* detect reception of loopback packet to our physical address */
  if (LOOPBACK_SELF_FRAME(dev->physical_addr, data)) {
    dev->loopback_self_rcvd_total++;
        /* lower reflection count - if already zero, pass it on */
    if (dev->loopback_self_sent > 0) {
      eth_packet_trace (dev, data, header->len, "ignored");
      dev->loopback_self_sent--;
      to_me = 0;
    } 
#ifndef USE_BPF
    else
      from_me = 0;
#endif
  }

#ifdef USE_BPF
  if (to_me) {
#else /* !USE_BPF */
  if (to_me && !from_me) {
#endif
    if (header->len > ETH_MIN_JUMBO_FRAME) {
      _eth_fix_ip_jumbo_offload(dev, data, header->len);
      return;
    }
#if defined (USE_READER_THREAD)
    if (1) {
      int crc_len = 0;
      uint8 crc_data[4];
      uint32 len = header->len;
      u_char* moved_data = NULL;

      if (header->len < ETH_MIN_PACKET) {   /* Pad runt packets before CRC append */
        moved_data = malloc(ETH_MIN_PACKET);
        memcpy(moved_data, data, len);
        memset(moved_data + len, 0, ETH_MIN_PACKET-len);
        len = ETH_MIN_PACKET;
        data = moved_data;
      }

      /* If necessary, fix IP header checksums for packets originated locally */
      /* but were presumed to be traversing a NIC which was going to handle that task */
      /* This must be done before any needed CRC calculation */
      _eth_fix_ip_xsum_offload(dev, (u_char*)data, len);
    
      if (dev->need_crc)
        crc_len = eth_get_packet_crc32_data(data, len, crc_data);

      eth_packet_trace (dev, data, len, "rcvqd");

      pthread_mutex_lock (&dev->lock);
      ethq_insert_data(&dev->read_queue, 2, data, 0, len, crc_len, crc_data, 0);
      pthread_mutex_unlock (&dev->lock);
      free(moved_data);
    }
#else
    /* set data in passed read packet */
    dev->read_packet->len = header->len;
    memcpy(dev->read_packet->msg, data, header->len);
    /* Handle runt case and pad with zeros.  */
    /* The real NIC won't hand us runts from the wire, BUT we may be getting */
    /* some packets looped back before they actually traverse the wire */
    /* (by an internal bridge device for instance) */
    if (header->len < ETH_MIN_PACKET) {
      memset(&dev->read_packet->msg[header->len], 0, ETH_MIN_PACKET-header->len);
      dev->read_packet->len = ETH_MIN_PACKET;
    }
    /* If necessary, fix IP header checksums for packets originated by the local host */
    /* but were presumed to be traversing a NIC which was going to handle that task */
    /* This must be done before any needed CRC calculation */
    _eth_fix_ip_xsum_offload(dev, dev->read_packet->msg, dev->read_packet->len);
    if (dev->need_crc)
      dev->read_packet->crc_len = eth_add_packet_crc32(dev->read_packet->msg, dev->read_packet->len);
    else
      dev->read_packet->crc_len = 0;

    eth_packet_trace (dev, dev->read_packet->msg, dev->read_packet->len, "reading");

    /* call optional read callback function */
    if (dev->read_callback)
      (dev->read_callback)(0);
#endif
  }
}

int eth_read(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
  int status;

  /* make sure device exists */

  if (!dev) return 0;

  /* make sure packet exists */
  if (!packet) return 0;

  packet->len = 0;
#if !defined (USE_READER_THREAD)
  /* set read packet */
  dev->read_packet = packet;

  /* set optional callback routine */
  dev->read_callback = routine;

  /* dispatch read request to either receive a filtered packet or timeout */
  do {
    if (dev->pcap_mode)
      status = pcap_dispatch((pcap_t*)dev->handle, 1, &_eth_callback, (u_char*)dev);
#ifdef USE_TAP_NETWORK
    else {
      struct pcap_pkthdr header;
      int len;
      u_char buf[ETH_MAX_JUMBO_FRAME];

      memset(&header, 0, sizeof(header));
      len = read(dev->fd_handle, buf, sizeof(buf));
      if (len > 0) {
        status = 1;
        header.len = len;
        _eth_callback((u_char *)dev, &header, buf);
      } else {
        status = 0;
      }
    }
#endif /* USE_TAP_NETWORK */
  } while ((status) && (0 == packet->len));

#else /* USE_READER_THREAD */

    status = 0;
    pthread_mutex_lock (&dev->lock);
    if (dev->read_queue.count > 0) {
      ETH_ITEM* item = &dev->read_queue.item[dev->read_queue.head];
      packet->len = item->packet.len;
      packet->crc_len = item->packet.crc_len;
      memcpy(packet->msg, item->packet.msg, ((packet->len > packet->crc_len) ? packet->len : packet->crc_len));
      status = 1;
      ethq_remove(&dev->read_queue);
    }
    pthread_mutex_unlock (&dev->lock);  
    if ((status) && (routine))
      routine(0);
#endif

  return status;
}

t_stat eth_filter(ETH_DEV* dev, int addr_count, ETH_MAC* const addresses,
                  ETH_BOOL all_multicast, ETH_BOOL promiscuous)
{
return eth_filter_hash(dev, addr_count, addresses, 
                       all_multicast, promiscuous, 
                       NULL);
}

t_stat eth_filter_hash(ETH_DEV* dev, int addr_count, ETH_MAC* const addresses,
                       ETH_BOOL all_multicast, ETH_BOOL promiscuous, 
                       ETH_MULTIHASH* const hash)
{
  int i;
  bpf_u_int32  bpf_subnet, bpf_netmask;
  char buf[114+66*ETH_FILTER_MAX];
  char errbuf[PCAP_ERRBUF_SIZE];
  char mac[20];
  char* buf2;
  t_stat status;
#ifdef USE_BPF
  struct bpf_program bpf;
  char* msg;
#endif

  /* make sure device exists */
  if (!dev) return SCPE_UNATT;

  /* filter count OK? */
  if ((addr_count < 0) || (addr_count > ETH_FILTER_MAX))
    return SCPE_ARG;
  else
    if (!addresses) return SCPE_ARG;

  /* test reflections.  This is done early in this routine since eth_reflect */
  /* calls eth_filter recursively and thus changes the state of the device. */
  if (dev->reflections == -1)
    status = eth_reflect(dev);

  /* set new filter addresses */
  for (i = 0; i < addr_count; i++)
    memcpy(dev->filter_address[i], addresses[i], sizeof(ETH_MAC));
  dev->addr_count = addr_count;

  /* store other flags */
  dev->all_multicast = all_multicast;
  dev->promiscuous   = promiscuous;

  /* store multicast hash data */
  dev->hash_filter = (hash != NULL);
  if (hash) {
    memcpy(dev->hash, hash, sizeof(*hash));
    sim_debug(dev->dbit, dev->dptr, "Multicast Hash: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
                                    dev->hash[0], dev->hash[1], dev->hash[2], dev->hash[3], 
                                    dev->hash[4], dev->hash[5], dev->hash[6], dev->hash[7]);
  }

  /* print out filter information if debugging */
  if (dev->dptr->dctrl & dev->dbit) {
    sim_debug(dev->dbit, dev->dptr, "Filter Set\n");
    for (i = 0; i < addr_count; i++) {
      char mac[20];
      eth_mac_fmt(&dev->filter_address[i], mac);
      sim_debug(dev->dbit, dev->dptr, "  Addr[%d]: %s\n", i, mac);
    }
    if (dev->all_multicast)
      sim_debug(dev->dbit, dev->dptr, "All Multicast\n");
    if (dev->promiscuous)
      sim_debug(dev->dbit, dev->dptr, "Promiscuous\n");
  }

  /* setup BPF filters and other fields to minimize packet delivery */
  strcpy(buf, "");

  /* construct destination filters - since the real ethernet interface was set
     into promiscuous mode by eth_open(), we need to filter out the packets that
     our simulated interface doesn't want. */
  if (!dev->promiscuous) {
    for (i = 0; i < addr_count; i++) {
      eth_mac_fmt(&dev->filter_address[i], mac);
      if (!strstr(buf, mac))    /* eliminate duplicates */
        sprintf(&buf[strlen(buf)], "%s(ether dst %s)", (*buf) ? " or " : "((", mac);
    }
    if (dev->all_multicast || dev->hash_filter)
      sprintf(&buf[strlen(buf)], "%s(ether multicast)", (*buf) ? " or " : "((");
    if (strlen(buf) > 0)
      sprintf(&buf[strlen(buf)], ")");
  }

  /* construct source filters - this prevents packets from being reflected back 
     by systems where WinPcap and libpcap cause packet reflections. Note that
     some systems do not reflect packets at all. This *assumes* that the 
     simulated NIC will not send out packets with multicast source fields. */
  if ((addr_count > 0) && (dev->reflections > 0)) {
    if (strlen(buf) > 0)
      sprintf(&buf[strlen(buf)], " and ");
    sprintf (&buf[strlen(buf)], "not (");
    buf2 = &buf[strlen(buf)];
    for (i = 0; i < addr_count; i++) {
      if (dev->filter_address[i][0] & 0x01) continue; /* skip multicast addresses */
      eth_mac_fmt(&dev->filter_address[i], mac);
      if (!strstr(buf2, mac))   /* eliminate duplicates */
        sprintf(&buf2[strlen(buf2)], "%s(ether src %s)", (*buf2) ? " or " : "", mac);
    }
    sprintf (&buf[strlen(buf)], ")");
  }
  if (strlen(buf) > 0)
    sprintf(&buf[strlen(buf)], ")");
  /* When changing the Physical Address on a LAN interface, VMS sends out a 
     loopback packet with the source and destination addresses set to the same 
     value as the Physical Address which is being setup.  This packet is
     designed to find and help diagnose MAC address conflicts (which also 
     include DECnet address conflicts). Normally, this packet would not be 
     seen by the sender, only by the other machine that has the same Physical 
     Address (or possibly DECnet address). If the ethernet subsystem is 
     reflecting packets, the network startup will fail to start if it sees the 
     reflected packet, since it thinks another system is using this Physical 
     Address (or DECnet address). We have to let these packets through, so 
     that if another machine has the same Physical Address (or DECnet address)
     that we can detect it. Both eth_write() and _eth_callback() help by 
     checking the reflection count - eth_write() adds the reflection count to
     dev->loopback_self_sent, and _eth_callback() check the value - if the
     dev->loopback_self_sent count is zero, then the packet has come from 
     another machine with the same address, and needs to be passed on to the 
     simulated machine. */
  memset(dev->physical_addr, 0, sizeof(ETH_MAC));
  dev->loopback_self_sent = 0;
  /* check for physical address in filters */
  if ((addr_count) && (dev->reflections > 0)) {
    for (i = 0; i < addr_count; i++) {
      if (dev->filter_address[i][0]&1)
        continue;  /* skip all multicast addresses */
      eth_mac_fmt(&dev->filter_address[i], mac);
      if (strcmp(mac, "00:00:00:00:00:00") != 0) {
        memcpy(dev->physical_addr, &dev->filter_address[i], sizeof(ETH_MAC));
        /* let packets through where dst and src are the same as our physical address */
        sprintf (&buf[strlen(buf)], " or ((ether dst %s) and (ether src %s))", mac, mac);
        break;
      }
    }
  }
  if ((0 == strlen(buf)) && (!dev->promiscuous)) /* Empty filter means match nothing */
      strcpy(buf, "ether host fe:ff:ff:ff:ff:ff"); /* this should be a good match nothing filter */
  sim_debug(dev->dbit, dev->dptr, "BPF string is: |%s|\n", buf);

  /* get netmask, which is required for compiling */
  if (dev->pcap_mode && (pcap_lookupnet(dev->handle, &bpf_subnet, &bpf_netmask, errbuf)<0)) {
      bpf_netmask = 0;
  }

#ifdef USE_BPF
  if (dev->pcap_mode) {
#ifdef USE_READER_THREAD
    pthread_mutex_lock (&dev->lock);
    ethq_clear (&dev->read_queue); /* Empty FIFO Queue when filter list changes */
    pthread_mutex_unlock (&dev->lock);
#endif
    /* compile filter string */
    if ((status = pcap_compile(dev->handle, &bpf, buf, 1, bpf_netmask)) < 0) {
      sprintf(errbuf, "%s", pcap_geterr(dev->handle));
      msg = "Eth: pcap_compile error: %s\r\n";
      printf(msg, errbuf);
      if (sim_log) fprintf (sim_log, msg, errbuf);
      sim_debug(dev->dbit, dev->dptr, "Eth: pcap_compile error: %s\n", errbuf);
      /* show erroneous BPF string */
      msg = "Eth: BPF string is: |%s|\r\n";
      printf (msg, buf);
      if (sim_log) fprintf (sim_log, msg, buf);
    } else {
      /* apply compiled filter string */
      if ((status = pcap_setfilter(dev->handle, &bpf)) < 0) {
        sprintf(errbuf, "%s", pcap_geterr(dev->handle));
        msg = "Eth: pcap_setfilter error: %s\r\n";
        printf(msg, errbuf);
        if (sim_log) fprintf (sim_log, msg, errbuf);
        sim_debug(dev->dbit, dev->dptr, "Eth: pcap_setfilter error: %s\n", errbuf);
      } else {
#ifdef USE_SETNONBLOCK
        /* set file non-blocking */
        status = pcap_setnonblock (dev->handle, 1, errbuf);
#endif /* USE_SETNONBLOCK */
      }
      pcap_freecode(&bpf);
    }
  }
#endif /* USE_BPF */

  return SCPE_OK;
}

/*
     The libpcap provided API pcap_findalldevs() on most platforms, will 
     leverage the getifaddrs() API if it is available in preference to 
     alternate platform specific methods of determining the interface list.

     A limitation of getifaddrs() is that it returns only interfaces which
     have associated addresses.  This may not include all of the interesting
     interfaces that we are interested in since a host may have dedicated
     interfaces for a simulator, which is otherwise unused by the host.

     One could hand craft the the build of libpcap to specifically use 
     alternate methods to implement pcap_findalldevs().  However, this can 
     get tricky, and would then result in a sort of deviant libpcap.

     This routine exists to allow platform specific code to validate and/or 
     extend the set of available interfaces to include any that are not
     returned by pcap_findalldevs.

*/
int eth_host_devices(int used, int max, ETH_LIST* list)
{
  pcap_t* conn;
  int i, j, datalink;
  char errbuf[PCAP_ERRBUF_SIZE];

  for (i=0; i<used; ++i) {
    /* Cull any non-ethernet interface types */
    conn = pcap_open_live(list[i].name, ETH_MAX_PACKET, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
    if (NULL != conn) datalink = pcap_datalink(conn), pcap_close(conn);
    if ((NULL == conn) || (datalink != DLT_EN10MB)) {
      for (j=i; j<used-1; ++j)
        list[j] = list[j+1];
      --used;
      --i;
    }
  } /* for */

#if defined(_WIN32)
  /* replace device description with user-defined adapter name (if defined) */
  for (i=0; i<used; i++) {
    char regkey[2048];
    char regval[2048];
    LONG status;
    DWORD reglen, regtype;
    HKEY reghnd;

    /* These registry keys don't seem to exist for all devices, so we simply ignore errors. */
    /* Windows XP x64 registry uses wide characters by default,
       so we force use of narrow characters by using the 'A'(ANSI) version of RegOpenKeyEx.
       This could cause some problems later, if this code is internationalized. Ideally,
       the pcap lookup will return wide characters, and we should use them to build a wide
       registry key, rather than hardcoding the string as we do here. */
    if(list[i].name[strlen( "\\Device\\NPF_" )] == '{') {
      sprintf( regkey, "SYSTEM\\CurrentControlSet\\Control\\Network\\"
               "{4D36E972-E325-11CE-BFC1-08002BE10318}\\%hs\\Connection", list[i].name+
               strlen( "\\Device\\NPF_" ) );
      if((status = RegOpenKeyExA (HKEY_LOCAL_MACHINE, regkey, 0, KEY_QUERY_VALUE, &reghnd)) != ERROR_SUCCESS) {
        continue;
      }
      reglen = sizeof(regval);

      /* look for user-defined adapter name, bail if not found */  
      /* same comment about Windows XP x64 (above) using RegQueryValueEx */
      if((status = RegQueryValueExA (reghnd, "Name", NULL, &regtype, regval, &reglen)) != ERROR_SUCCESS) {
        RegCloseKey (reghnd);
        continue;
      }
      /* make sure value is the right type, bail if not acceptable */
      if((regtype != REG_SZ) || (reglen > sizeof(regval))) {
        RegCloseKey (reghnd);
        continue;
      }
      /* registry value seems OK, finish up and replace description */
      RegCloseKey (reghnd );
      sprintf (list[i].desc, "%s", regval);
    }
  } /* for */
#endif

#ifdef USE_TAP_NETWORK
  if (used < max) {
    list[used].num = used;
#if defined(__OpenBSD__)
    sprintf(list[used].name, "%s", "tap:tunN");
#else
    sprintf(list[used].name, "%s", "tap:tapN");
#endif
    sprintf(list[used].desc, "%s", "Integrated Tun/Tap support");
    ++used;
  }
#endif

  return used;
}

int eth_devices(int max, ETH_LIST* list)
{
  pcap_if_t* alldevs;
  pcap_if_t* dev;
  int i = 0;
  char errbuf[PCAP_ERRBUF_SIZE];

#ifndef DONT_USE_PCAP_FINDALLDEVS
  /* retrieve the device list */
  if (pcap_findalldevs(&alldevs, errbuf) == -1) {
    char* msg = "Eth: error in pcap_findalldevs: %s\r\n";
    printf (msg, errbuf);
    if (sim_log) fprintf (sim_log, msg, errbuf);
  } else {
    /* copy device list into the passed structure */
    for (i=0, dev=alldevs; dev; dev=dev->next) {
      if ((dev->flags & PCAP_IF_LOOPBACK) || (!strcmp("any", dev->name))) continue;
      list[i].num = i;
      sprintf(list[i].name, "%s", dev->name);
      if (dev->description)
        sprintf(list[i].desc, "%s", dev->description);
      else
        sprintf(list[i].desc, "%s", "No description available");
      if (i++ >= max) break;
    }

    /* free device list */
    pcap_freealldevs(alldevs);
  }
#endif

  /* Add any host specific devices and/or validate those already found */
  i = eth_host_devices(i, max, list);

  /* return device count */
  return i;
}

void eth_show_dev (FILE *st, ETH_DEV* dev)
{
  fprintf(st, "Ethernet Device:\n");
  if (!dev) {
    fprintf(st, "-- Not Attached\n");
    return;
  }
  fprintf(st, "  Name:                  %s\n", dev->name);
  fprintf(st, "  Reflections:           %d\n", dev->reflections);
  fprintf(st, "  Self Loopbacks Sent:   %d\n", dev->loopback_self_sent_total);
  fprintf(st, "  Self Loopbacks Rcvd:   %d\n", dev->loopback_self_rcvd_total);
  if (dev->have_host_nic_phy_addr) {
    char hw_mac[20];

    eth_mac_fmt(&dev->host_nic_phy_hw_addr, hw_mac);
    fprintf(st, "  Host NIC Address:      %s\n", hw_mac);
  }
  if (dev->jumbo_dropped)
    fprintf(st, "  Jumbo Dropped:         %d\n", dev->jumbo_dropped);
  if (dev->jumbo_fragmented)
    fprintf(st, "  Jumbo Fragmented:      %d\n", dev->jumbo_fragmented);
#if defined(USE_READER_THREAD)
  fprintf(st, "  Asynch Interrupts:       %s\n", dev->asynch_io?"Enabled":"Disabled");
  if (dev->asynch_io)
    fprintf(st, "  Interrupt Latency:       %d uSec\n", dev->asynch_io_latency);
  fprintf(st, "  Read Queue: Count:       %d\n", dev->read_queue.count);
  fprintf(st, "  Read Queue: High:        %d\n", dev->read_queue.high);
  fprintf(st, "  Read Queue: Loss:        %d\n", dev->read_queue.loss);
  fprintf(st, "  Peak Write Queue Size:   %d\n", dev->write_queue_peak);
#endif
}
#endif /* USE_NETWORK */
