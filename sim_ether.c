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
    USE_SHARED  - Create dynamically linked network code

  ------------------------------------------------------------------------------

  Supported/Tested Platforms:

  Windows(NT,2K,XP,2K3,Vista,Win7)     WinPcap-4.1.3 Npcap-V0.9994
  Linux                     libpcap at least 0.9
  OpenBSD,FreeBSD,NetBSD    libpcap at least 0.9
  MAC OS/X                  libpcap at least 0.9
  Solaris Sparc             libpcap at least 0.9
  Solaris Intel             libpcap at least 0.9
  AIX                       ??
  HP/UX                     ??
  Compaq Tru64 Unix         ??
  VMS                       Alpha/Itanium VMS only, needs VMS libpcap
  
  WinPcap is no longer developed or supported by was available from: 
                        http://winpcap.polito.it/
  Npcap is a complete replacement for systems running Windows7 and later
  and is available from:
                        https://nmap.org/npcap
  libpcap for VMS is available from: 
                        http://simh.trailing-edge.com/sources/vms-pcap.zip
  libpcap for other Unix platforms is available at: 
        NOTE: As of the release of this version of sim_ether.c ALL current 
              *nix platforms ship with a sufficiently new version of 
              libpcap, and ALL provide a libpcap-dev package for developing
              libpcap based applications.  The OS vendor supplied version
              of libpcap AND the libpcap-dev components are preferred for
              proper operation of both simh AND other applications on the 
              host system which use libpcap.
        Current Version:  http://www.tcpdump.org/daily/libpcap-current.tar.gz
        Released Version: http://www.tcpdump.org/release/

        When absolutely necessary (see NOTE above about vendor supplied 
        libpcap), we've gotten the tarball, unpacked, built and installed 
        it with:
            gzip -dc libpcap-current.tar.gz | tar xvf -
            cd libpcap-directory-name
            ./configure
            make
            make install
        Note:  The "make install" step generally will have to be done as root.
        This will install libpcap in /usr/local/lib and /usr/local/include
        The current simh makefile will do the right thing to locate and 
        reference the OS provided libpcap or the one just installed.


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
                      select() should be used to determine when available 
                      packets are ready for reading.  Otherwise, we depend 
                      on the libpcap/kernel packet timeout specified on 
                      pcap_open_live.  If USE_READER_THREAD is not set, then 
                      MUST_DO_SELECT is irrelevant
  HAVE_TAP_NETWORK  - Specifies that support for tap networking should be 
                      included.  This can be leveraged, along with OS bridging
                      capabilities to share a single LAN interface.  This 
                      allows device names of the form tap:tap0 to be specified
                      at open time.  This functionality is only useful/needed 
                      on *nix platforms since native sharing of Windows NIC 
                      devices works with no external magic.
  HAVE_VDE_NETWORK  - Specifies that support for vde networking should be 
                      included.  This can be leveraged, along with OS bridging
                      capabilities to share a single LAN interface.  It also
                      can allow a simulator to have useful networking 
                      functionality when running without root access.  This 
                      allows device names of the form vde:/tmp/switch to be 
                      specified at open time.  This functionality is only 
                      available on *nix platforms since the vde api isn't 
                      available on Windows.
  HAVE_SLIRP_NETWORK- Specifies that support for SLiRP networking should be 
                      included.  This can be leveraged to provide User Mode 
                      IP NAT connectivity for simulators.

  NEED_PCAP_SENDPACKET
                    - Specifies that you are using an older version of libpcap
                      which doesn't provide a pcap_sendpacket API.

  NOTE: Changing these defines is done in either sim_ether.h OR on the global 
        compiler command line which builds all of the modules included in a
        simulator.

  ------------------------------------------------------------------------------

  Modification history:

  30-Mar-12  MP   Added host NIC address determination on supported VMS platforms
  01-Mar-12  MP   Made host NIC address determination on *nix platforms more 
                  robust.
  01-Mar-12  MP   Added host NIC address determination work when building 
                  under Cygwin
  01-Mar-12  AGN  Add conditionals for Cygwin dynamic loading of wpcap.dll
  01-Mar-12  AGN  Specify the full /usr/lib for dlopen under Apple Mac OS X.
  17-Nov-11  MP   Added dynamic loading of libpcap on *nix platforms
  30-Oct-11  MP   Added support for vde (Virtual Distributed Ethernet) networking
  29-Oct-11  MP   Added support for integrated Tap networking interfaces on OSX
  12-Aug-11  MP   Cleaned up payload length determination
                  Fixed race condition detecting reflections when threaded 
                  reading and writing is enabled
  18-Apr-11  MP   Fixed race condition with self loopback packets in 
                  multithreaded environments
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
                  device to support traffic when a host leverages a NIC's Large 
                  Send Offload capabilities to fregment and/or segment outgoing 
                  network traffic.  Additionally a simulated network device can 
                  reasonably exist on a LAN which is configured to use Jumbo frames.
  21-May-10  MP   Added functionality to fixup IP header checksums to accomodate 
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
  15-Dec-05  DTH  Patched eth_host_pcap_devices [remove non-ethernet devices]
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
#include "sim_timer.h"
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

/* Internal routine - forward declaration */
static int _eth_get_system_id (char *buf, size_t buf_size);

/*============================================================================*/
/*                  OS-independant ethernet routines                          */
/*============================================================================*/

t_stat eth_mac_scan (ETH_MAC* mac, const char* strmac)
{
return eth_mac_scan_ex (mac, strmac, NULL);
}

t_stat eth_mac_scan_ex (ETH_MAC* mac, const char* strmac, UNIT *uptr)
{
  unsigned int a[6], g[6];
  FILE *f;
  char filebuf[64] = "";
  uint32 i;
  static const ETH_MAC zeros = {0,0,0,0,0,0};
  static const ETH_MAC ones  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  ETH_MAC newmac;
  struct {
      uint32 bits;
      char system_id[37];
      char cwd[PATH_MAX];
      char file[PATH_MAX];
      ETH_MAC base_mac;
      char uname[64];
      char sim[128];
      } state;
  CONST char *cptr, *tptr;
  uint32 data;

  /* Allow generated MAC address */
  /* XX:XX:XX:XX:XX:XX{/bits{>file}} */
  /* bits (if specified) must be from 16 thru 48 */

  memset (&state, 0, sizeof(state));
  _eth_get_system_id (state.system_id, sizeof(state.system_id));
  strlcpy (state.sim, sim_name, sizeof(state.sim));
  if (getcwd (state.cwd, sizeof(state.cwd))) {};
  if (uptr)
    strlcpy (state.uname, sim_uname (uptr), sizeof(state.uname));
  cptr = strchr (strmac, '>');
  if (cptr) {
    state.file[sizeof(state.file)-1] = '\0';
    strlcpy (state.file, cptr + 1, sizeof(state.file));
    if ((f = fopen (state.file, "r"))) {
      filebuf[sizeof(filebuf)-1] = '\0';
      if (fgets (filebuf, sizeof(filebuf)-1, f)) {};
      strmac = filebuf;
      fclose (f);
      strcpy (state.file, "");  /* avoid saving */
      }
    }
  cptr = strchr (strmac, '/');
  if (cptr) {
    state.bits = (uint32)strtotv (cptr + 1, &tptr, 10);
    if ((state.bits < 16) || (state.bits > 48))
      return sim_messagef (SCPE_ARG, "Invalid MAC address bits specifier '%d'. Valid values are from 16 thru 48\n", state.bits);
    }
  else
    state.bits = 48;
  data = eth_crc32 (0, (void *)&state, sizeof(state));
  for (i=g[0]=g[1]=0; i<4; i++)
    g[i+2] = (data >> (i << 3)) & 0xFF;
  if ((6 != sscanf(strmac, "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5])) &&
      (6 != sscanf(strmac, "%x.%x.%x.%x.%x.%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5])) &&
      (6 != sscanf(strmac, "%x-%x-%x-%x-%x-%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5])))
    return sim_messagef (SCPE_ARG, "Invalid MAC address format: '%s'\n", strmac);
  for (i=0; i<6; i++)
    if (a[i] > 0xFF)
      return sim_messagef (SCPE_ARG, "Invalid MAC address byte value: %02X\n", a[i]);
    else {
      uint32 mask, shift;
    
      state.base_mac[i] = a[i];
      if (((i + 1) << 3) < state.bits)
          shift = 0;
      else
          shift = ((i + 1) << 3) - state.bits;
      mask = 0xFF << shift;
      newmac[i] = (unsigned char)((a[i] & mask) | (g[i] & ~mask));
      }

  /* final check - mac cannot be broadcast or multicast address */
  if (!memcmp(newmac, zeros, sizeof(ETH_MAC)) ||  /* broadcast */
      !memcmp(newmac, ones,  sizeof(ETH_MAC)) ||  /* broadcast */
      (newmac[0] & 0x01)                          /* multicast */
     )
    return sim_messagef (SCPE_ARG, "Can't use Broadcast or MultiCast address as interface MAC address\n");

  /* new mac is OK */
  /* optionally save */
  if (state.file[0]) {              /* Save File specified? */
    f = fopen (state.file, "w");
    if (f == NULL)
      return sim_messagef (SCPE_ARG, "Can't open MAC address configuration file '%s'.\n", state.file);
    eth_mac_fmt (&newmac, filebuf);
    fprintf (f, "%s/48\n", filebuf);
    fprintf (f, "system-id: %s\n", state.system_id);
    fprintf (f, "directory: %s\n", state.cwd);
    fprintf (f, "simulator: %s\n", state.sim);
    fprintf (f, "device:    %s\n", state.uname);
    fprintf (f, "file:      %s\n", state.file);
    eth_mac_fmt (&state.base_mac, filebuf);
    fprintf (f, "base-mac:  %s\n", filebuf);
    fprintf (f, "specified: %d bits\n", state.bits);
    fprintf (f, "generated: %d bits\n", 48-state.bits);
    fclose (f);
    }
  /* copy into passed mac */
  memcpy (*mac, newmac, sizeof(ETH_MAC));
  return SCPE_OK;
}

void eth_mac_fmt(ETH_MAC* const mac, char* buff)
{
  const uint8* m = (const uint8*) mac;
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

void eth_packet_trace_ex(ETH_DEV* dev, const uint8 *msg, int len, const char* txt, int detail, uint32 reason)
{
  if (dev->dptr->dctrl & reason) {
    char src[20];
    char dst[20];
    const unsigned short* proto = (const unsigned short*) &msg[12];
    uint32 crc = eth_crc32(0, msg, len);
    eth_mac_fmt((ETH_MAC*)msg, dst);
    eth_mac_fmt((ETH_MAC*)(msg+6), src);
    sim_debug(reason, dev->dptr, "%s  dst: %s  src: %s  proto: 0x%04X  len: %d  crc: %X\n",
          txt, dst, src, ntohs(*proto), len, crc);
    if (detail) {
      int i, same, group, sidx, oidx;
      char outbuf[80], strbuf[18];
      static const char hex[] = "0123456789ABCDEF";

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
      if (same > 0) {
        sim_debug(reason, dev->dptr, "%04X thru %04X same as above\n", i-(16*same), len-1);
      }
    }
  }
}

void eth_packet_trace(ETH_DEV* dev, const uint8 *msg, int len, const char* txt)
{
  eth_packet_trace_ex(dev, msg, len, txt, 0, dev->dbit);
}

void eth_packet_trace_detail(ETH_DEV* dev, const uint8 *msg, int len, const char* txt)
{
  eth_packet_trace_ex(dev, msg, len, txt, 1     , dev->dbit);
}

void eth_zero(ETH_DEV* dev)
{
  /* set all members to NULL OR 0 */
  memset(dev, 0, sizeof(ETH_DEV));
  dev->reflections = -1;                          /* not established yet */
}

t_stat ethq_init(ETH_QUE* que, int max)
{
  /* create dynamic queue if it does not exist */
  if (!que->item) {
    que->item = (struct eth_item *) calloc(max, sizeof(struct eth_item));
    if (!que->item) {
      /* failed to allocate memory */
      sim_printf("EthQ: failed to allocate dynamic queue[%d]\n", max);
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
  int i;

  /* free up any extended packets */
  for (i=0; i<que->max; ++i)
    if (que->item[i].packet.oversize) {
      free (que->item[i].packet.oversize);
      que->item[i].packet.oversize = NULL;
      }
  /* clear packet array */
  memset(que->item, 0, sizeof(struct eth_item) * que->max);
  /* clear rest of structure */
  que->count = que->head = que->tail = 0;
}

void ethq_remove(ETH_QUE* que)
{
  struct eth_item* item = &que->item[que->head];

  if (que->count) {
    if (item->packet.oversize)
      free (item->packet.oversize);
    memset(item, 0, sizeof(struct eth_item));
    if (++que->head == que->max)
      que->head = 0;
    que->count--;
  }
}

void ethq_insert_data(ETH_QUE* que, int32 type, const uint8 *data, int used, size_t len, size_t crc_len, const uint8 *crc_data, int32 status)
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
  if (MAX (len, crc_len) <= sizeof (item->packet.msg)) {
    memcpy(item->packet.msg, data, ((len > crc_len) ? len : crc_len));
    if (crc_data && (crc_len > len))
      memcpy(&item->packet.msg[len], crc_data, ETH_CRC_SIZE);
    }
  else {
    item->packet.oversize = (uint8 *)realloc (item->packet.oversize, ((len > crc_len) ? len : crc_len));
    memcpy(item->packet.oversize, data, ((len > crc_len) ? len : crc_len));
    if (crc_data && (crc_len > len))
      memcpy(&item->packet.oversize[len], crc_data, ETH_CRC_SIZE);
    }
  item->packet.status = status;
}

void ethq_insert(ETH_QUE* que, int32 type, ETH_PACK* pack, int32 status)
{
ethq_insert_data(que, type, pack->oversize ? pack->oversize : pack->msg, pack->used, pack->len, pack->crc_len, NULL, status);
}

t_stat eth_show_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char *desc)
{
return eth_show (st, uptr, val, NULL);
}

#if defined (USE_NETWORK) || defined (USE_SHARED)
/* Internal routine - forward declaration */
static int _eth_devices (int max, ETH_LIST* dev);   /* get ethernet devices on host */

static const char* _eth_getname(int number, char* name, char *desc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = _eth_devices(ETH_MAX_DEVICE, list);

  if ((number < 0) || (count <= number))
      return NULL;
  if (list[number].eth_api != ETH_API_PCAP) {
    sim_printf ("Eth: Pcap capable device not found.  You may need to run as root\n");
    return NULL;
    }

  strcpy(name, list[number].name);
  strcpy(desc, list[number].desc);
  return name;
}

const char* eth_getname_bydesc(const char* desc, char* name, char *ndesc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = _eth_devices(ETH_MAX_DEVICE, list);
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
    strcpy(ndesc, list[i].desc);
    return name;
  }
  /* not found */
  return NULL;
}

char* eth_getname_byname(const char* name, char* temp, char *desc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = _eth_devices(ETH_MAX_DEVICE, list);
  size_t n;
  int i, found;

  found = 0;
  n = strlen(name);
  for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
      found = 1;
      strcpy(temp, list[i].name); /* only case might be different */
      strcpy(desc, list[i].desc);
    }
  }
  return (found ? temp : NULL);
}

char* eth_getdesc_byname(char* name, char* temp)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int count = _eth_devices(ETH_MAX_DEVICE, list);
  size_t n;
  int i, found;

  found = 0;
  n = strlen(name);
  for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
      found = 1;
      strcpy(temp, list[i].desc);
    }
  }
  return (found ? temp : NULL);
}

static ETH_DEV **eth_open_devices = NULL;
static int eth_open_device_count = 0;

static char*   (*p_pcap_lib_version) (void);

static void _eth_add_to_open_list (ETH_DEV* dev)
{
eth_open_devices = (ETH_DEV**)realloc(eth_open_devices, (eth_open_device_count+1)*sizeof(*eth_open_devices));
eth_open_devices[eth_open_device_count++] = dev;
}

static void _eth_remove_from_open_list (ETH_DEV* dev)
{
int i, j;

for (i=0; i<eth_open_device_count; ++i)
    if (eth_open_devices[i] == dev) {
        for (j=i+1; j<eth_open_device_count; ++j)
            eth_open_devices[j-1] = eth_open_devices[j];
        --eth_open_device_count;
        break;
        }
}

t_stat eth_show (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  ETH_LIST  list[ETH_MAX_DEVICE];
  int number;

  number = _eth_devices(ETH_MAX_DEVICE, list);
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
        fprintf(st," eth%d\t%-*s (%s)\n", i, (int)min, list[i].name, list[i].desc);
    }
  if (eth_open_device_count) {
    int i;
    char desc[ETH_DEV_DESC_MAX], *d;

    fprintf(st,"Open ETH Devices:\n");
    for (i=0; i<eth_open_device_count; i++) {
      d = eth_getdesc_byname(eth_open_devices[i]->name, desc);
      if (d)
        fprintf(st, " %-7s%s (%s)\n", eth_open_devices[i]->dptr->name, eth_open_devices[i]->dptr->units[0].filename, d);
      else
        fprintf(st, " %-7s%s\n", eth_open_devices[i]->dptr->name, eth_open_devices[i]->dptr->units[0].filename);
      eth_show_dev (st, eth_open_devices[i]);
      }
    }
  return SCPE_OK;
}

#endif
/*============================================================================*/
/*                        Non-implemented versions                            */
/*============================================================================*/

#if !defined (USE_NETWORK) && !defined (USE_SHARED)
const char *eth_capabilities(void)
    {return "no Ethernet";}
t_stat eth_open(ETH_DEV* dev, const char* name, DEVICE* dptr, uint32 dbit)
  {return SCPE_NOFNC;}
t_stat eth_close (ETH_DEV* dev)
  {return SCPE_NOFNC;}
t_stat eth_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
  {
  fprintf (st, "%s attach help\n\n", dptr->name);
  fprintf (st, "This simulator was not built with ethernet device support\n");
  return SCPE_OK;
  }
t_stat eth_check_address_conflict (ETH_DEV* dev, 
                                   ETH_MAC* const mac)
  {return SCPE_NOFNC;}
t_stat eth_set_throttle (ETH_DEV* dev, uint32 time, uint32 burst, uint32 delay)
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
const char *eth_version (void)
  {return NULL;}
void eth_show_dev (FILE* st, ETH_DEV* dev)
  {}
t_stat eth_show (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
  {
  fprintf(st, "ETH devices:\n");
  fprintf(st, "  network support not available in simulator\n");
  return SCPE_OK;
  }
static int _eth_get_system_id (char *buf, size_t buf_size)
  {memset (buf, 0, buf_size); return 0;}
t_stat sim_ether_test (DEVICE *dptr)
  {return SCPE_OK;}
#else    /* endif unimplemented */

const char *eth_capabilities(void)
 {
#if defined (USE_READER_THREAD)
 return "Threaded "
#else
 return "Polled "
#endif
     "Ethernet Packet transports"
#if defined (HAVE_PCAP_NETWORK)
     ":PCAP"
#endif
#if defined (HAVE_TAP_NETWORK)
     ":TAP"
#endif
#if defined (HAVE_VDE_NETWORK)
     ":VDE"
#endif
#if defined (HAVE_SLIRP_NETWORK)
     ":NAT"
#endif
     ":UDP";
 }

#if (defined (xBSD) || defined (__APPLE__)) && (defined (HAVE_TAP_NETWORK) || defined (HAVE_PCAP_NETWORK))
#include <sys/ioctl.h>
#include <net/bpf.h>
#endif

#if defined (HAVE_PCAP_NETWORK)
/*============================================================================*/
/*      WIN32, Linux, and xBSD routines use WinPcap and libpcap packages      */
/*        OpenVMS Alpha uses a WinPcap port and an associated execlet         */
/*============================================================================*/

#include <pcap.h>
#include <string.h>
#else
struct pcap_pkthdr {
    uint32 caplen;  /* length of portion present */
    uint32 len;     /* length this packet (off wire) */
};
#define PCAP_ERRBUF_SIZE 256
typedef void * pcap_t;  /* Pseudo Type to avoid compiler errors */
#define DLT_EN10MB 1    /* Dummy Value to avoid compiler errors */
#endif /* HAVE_PCAP_NETWORK */

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
static int eth_host_pcap_devices(int used, int max, ETH_LIST* list)
{
pcap_t* conn = NULL;
int i, j, datalink = 0;

for (i=0; i<used; ++i) {
  /* Cull any non-ethernet interface types */
#if defined(HAVE_PCAP_NETWORK)
  char errbuf[PCAP_ERRBUF_SIZE];

  conn = pcap_open_live(list[i].name, ETH_MAX_PACKET, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
  if (NULL != conn)
    datalink = pcap_datalink(conn), pcap_close(conn);
  list[i].eth_api = ETH_API_PCAP;
  if ((NULL == conn) || (datalink != DLT_EN10MB)) {
    for (j=i; j<used-1; ++j)
      list[j] = list[j+1];
    --used;
    --i;
    }
#endif
  } /* for */

#if defined(_WIN32)
/* replace device description with user-defined adapter name (if defined) */
for (i=0; i<used; i++) {
  char regkey[2048];
  unsigned char regval[2048];
  LONG status;
  DWORD reglen, regtype;
  HKEY reghnd;

  /* These registry keys don't seem to exist for all devices, so we simply ignore errors. */
  /* Windows XP x64 registry uses wide characters by default,
     so we force use of narrow characters by using the 'A'(ANSI) version of RegOpenKeyEx.
     This could cause some problems later, if this code is internationalized. Ideally,
     the pcap lookup will return wide characters, and we should use them to build a wide
     registry key, rather than hardcoding the string as we do here. */
  if (list[i].name[strlen( "\\Device\\NPF_" )] == '{') {
    sprintf( regkey, "SYSTEM\\CurrentControlSet\\Control\\Network\\"
             "{4D36E972-E325-11CE-BFC1-08002BE10318}\\%s\\Connection", list[i].name+
             strlen( "\\Device\\NPF_" ) );
    if ((status = RegOpenKeyExA (HKEY_LOCAL_MACHINE, regkey, 0, KEY_QUERY_VALUE, &reghnd)) != ERROR_SUCCESS)
      continue;
    reglen = sizeof(regval);

    /* look for user-defined adapter name, bail if not found */  
    /* same comment about Windows XP x64 (above) using RegQueryValueEx */
    if ((status = RegQueryValueExA (reghnd, "Name", NULL, &regtype, regval, &reglen)) != ERROR_SUCCESS) {
      RegCloseKey (reghnd);
      continue;
      }
    /* make sure value is the right type, bail if not acceptable */
    if ((regtype != REG_SZ) || (reglen > sizeof(regval))) {
      RegCloseKey (reghnd);
      continue;
      }
    /* registry value seems OK, finish up and replace description */
    RegCloseKey (reghnd );
    sprintf (list[i].desc, "%s", regval);
    }
  } /* for */
#endif

return used;
}

static int _eth_devices(int max, ETH_LIST* list)
{
int used = 0;
char errbuf[PCAP_ERRBUF_SIZE] = "";
#ifndef DONT_USE_PCAP_FINDALLDEVS
pcap_if_t* alldevs;
pcap_if_t* dev;

memset(list, 0, max*sizeof(*list));
errbuf[0] = '\0';
/* retrieve the device list */
if (pcap_findalldevs(&alldevs, errbuf) == -1) {
  if (errbuf[0])
    sim_printf ("Eth: %s\n", errbuf);
  }
else {
  /* copy device list into the passed structure */
  for (used=0, dev=alldevs; dev && (used < max); dev=dev->next, ++used) {
    if ((dev->flags & PCAP_IF_LOOPBACK) || (!strcmp("any", dev->name)))
      continue;
    strlcpy(list[used].name, dev->name, sizeof(list[used].name));
    if (dev->description)
      strlcpy(list[used].desc, dev->description, sizeof(list[used].desc));
    else
      strlcpy(list[used].desc, "No description available", sizeof(list[used].desc));
    }

  /* free device list */
  pcap_freealldevs(alldevs);
  }
#endif

/* Add any host specific devices and/or validate those already found */
used = eth_host_pcap_devices(used, max, list);

/* If no devices were found and an error message was left in the buffer, display it */
if ((used == 0) && (errbuf[0])) {
    sim_printf ("Eth: pcap_findalldevs warning: %s\n", errbuf);
    }

#ifdef HAVE_TAP_NETWORK
if (used < max) {
#if defined(__OpenBSD__)
  sprintf(list[used].name, "%s", "tap:tunN");
#else
  sprintf(list[used].name, "%s", "tap:tapN");
#endif
  sprintf(list[used].desc, "%s", "Integrated Tun/Tap support");
  list[used].eth_api = ETH_API_TAP;
  ++used;
  }
#endif
#ifdef HAVE_VDE_NETWORK
if (used < max) {
  sprintf(list[used].name, "%s", "vde:device{:switch-port-number}");
  sprintf(list[used].desc, "%s", "Integrated VDE support");
  list[used].eth_api = ETH_API_VDE;
  ++used;
  }
#endif
#ifdef HAVE_SLIRP_NETWORK
if (used < max) {
  sprintf(list[used].name, "%s", "nat:{optional-nat-parameters}");
  sprintf(list[used].desc, "%s", "Integrated NAT (SLiRP) support");
  list[used].eth_api = ETH_API_NAT;
  ++used;
  }
#endif

if (used < max) {
  sprintf(list[used].name, "%s", "udp:sourceport:remotehost:remoteport");
  sprintf(list[used].desc, "%s", "Integrated UDP bridge support");
  list[used].eth_api = ETH_API_UDP;
  ++used;
  }

/* return device count */
return used;
}

#ifdef HAVE_TAP_NETWORK
#if defined(__linux) || defined(__linux__)
#include <sys/ioctl.h> 
#include <net/if.h> 
#include <linux/if_tun.h> 
#elif defined(HAVE_BSDTUNTAP)
#include <sys/types.h>
#include <net/if_types.h>
#include <net/if.h>
#else /* We don't know how to do this on the current platform */
#undef HAVE_TAP_NETWORK
#endif
#endif /* HAVE_TAP_NETWORK */

#ifdef HAVE_VDE_NETWORK
#ifdef  __cplusplus
extern "C" {
#endif
#include <libvdeplug.h>
#ifdef  __cplusplus
}
#endif
#endif /* HAVE_VDE_NETWORK */

#ifdef HAVE_SLIRP_NETWORK
#include "sim_slirp.h"
#endif /* HAVE_SLIRP_NETWORK */

/* Allows windows to look up user-defined adapter names */
#if defined(_WIN32)
#include <winreg.h>
#endif

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif

#if defined(USE_SHARED) && (defined(_WIN32) || defined(HAVE_DLOPEN))
/* Dynamic DLL loading technique and modified source comes from
   Etherial/WireShark capture_pcap.c */

/* Dynamic DLL load variables */
#ifdef _WIN32
static HINSTANCE hLib = NULL;               /* handle to DLL */
#else
static void *hLib = 0;                      /* handle to Library */
#endif
static int lib_loaded = 0;                  /* 0=not loaded, 1=loaded, 2=library load failed, 3=Func load failed */

#define __STR_QUOTE(tok) #tok
#define __STR(tok) __STR_QUOTE(tok)
static const char* lib_name =
#if defined(_WIN32) || defined(__CYGWIN__)
                          "wpcap.dll";
#elif defined(__APPLE__)
                          "/usr/lib/libpcap.A.dylib";
#else
                          "libpcap." __STR(HAVE_DLOPEN);
#endif

static char no_pcap[PCAP_ERRBUF_SIZE] =
#if defined(_WIN32) || defined(__CYGWIN__)
    "wpcap.dll failed to load, install Npcap or WinPcap 4.1.3 to use pcap networking";
#elif defined(__APPLE__)
    "/usr/lib/libpcap.A.dylib failed to load, install libpcap to use pcap networking";
#else
    "libpcap." __STR(HAVE_DLOPEN) " failed to load, install libpcap to use pcap networking";
#endif
#undef __STR
#undef __STR_QUOTE

/* define pointers to pcap functions needed */
static void    (*p_pcap_close) (pcap_t *);
static int     (*p_pcap_compile) (pcap_t *, struct bpf_program *, const char *, int, bpf_u_int32);
static int     (*p_pcap_datalink) (pcap_t *);
static int     (*p_pcap_dispatch) (pcap_t *, int, pcap_handler, u_char *);
static int     (*p_pcap_findalldevs) (pcap_if_t **, char *);
static void    (*p_pcap_freealldevs) (pcap_if_t *);
static void    (*p_pcap_freecode) (struct bpf_program *);
static char*   (*p_pcap_geterr) (pcap_t *);
static int     (*p_pcap_lookupnet) (const char *, bpf_u_int32 *, bpf_u_int32 *, char *);
static pcap_t* (*p_pcap_open_live) (const char *, int, int, int, char *);
#ifdef _WIN32
static int     (*p_pcap_setmintocopy) (pcap_t* handle, int);
static HANDLE  (*p_pcap_getevent) (pcap_t *);
#else
#ifdef MUST_DO_SELECT
static int     (*p_pcap_get_selectable_fd) (pcap_t *);
#endif
static int     (*p_pcap_fileno) (pcap_t *);
#endif
static int     (*p_pcap_sendpacket) (pcap_t* handle, const u_char* msg, int len);
static int     (*p_pcap_setfilter) (pcap_t *, struct bpf_program *);
static int     (*p_pcap_setnonblock)(pcap_t* a, int nonblock, char *errbuf);

/* load function pointer from DLL */
typedef int (*_func)();

static void load_function(const char* function, _func* func_ptr) {
#ifdef _WIN32
    *func_ptr = (_func)((size_t)GetProcAddress(hLib, function));
#else
    *func_ptr = (_func)((size_t)dlsym(hLib, function));
#endif
    if (*func_ptr == 0) {
    sim_printf ("Eth: Failed to find function '%s' in %s\n", function, lib_name);
    lib_loaded = 3;
  }
}

/* load wpcap.dll as required */
int load_pcap(void) {
  switch(lib_loaded) {
    case 0:                  /* not loaded */
            /* attempt to load DLL */
#ifdef _WIN32
      if (1) {
        BOOL(WINAPI *p_SetDllDirectory)(LPCTSTR);
        UINT(WINAPI *p_GetSystemDirectory)(LPTSTR lpBuffer, UINT uSize);

        p_SetDllDirectory = (BOOL(WINAPI *)(LPCTSTR)) GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetDllDirectoryA");
        p_GetSystemDirectory = (UINT(WINAPI *)(LPTSTR, UINT)) GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemDirectoryA");
        if (p_SetDllDirectory && p_GetSystemDirectory) {
          char npcap_path[512] = "";

          if (p_GetSystemDirectory (npcap_path, sizeof(npcap_path) - 7))
            strlcat (npcap_path, "\\Npcap", sizeof(npcap_path));
          if (p_SetDllDirectory(npcap_path))
            hLib = LoadLibraryA(lib_name);
          p_SetDllDirectory (NULL);
          }
        if (hLib == NULL)
          hLib = LoadLibraryA(lib_name);
        }
#else
      hLib = dlopen(lib_name, RTLD_NOW);
#endif
      if (hLib == 0) {
        /* failed to load DLL */
        lib_loaded = 2;
        break;
      } else {
        /* library loaded OK */
        lib_loaded = 1;
      }

      /* load required functions; sets dll_load=3 on error */
      load_function("pcap_close",        (_func *) &p_pcap_close);
      load_function("pcap_compile",      (_func *) &p_pcap_compile);
      load_function("pcap_datalink",     (_func *) &p_pcap_datalink);
      load_function("pcap_dispatch",     (_func *) &p_pcap_dispatch);
      load_function("pcap_findalldevs",  (_func *) &p_pcap_findalldevs);
      load_function("pcap_freealldevs",  (_func *) &p_pcap_freealldevs);
      load_function("pcap_freecode",     (_func *) &p_pcap_freecode);
      load_function("pcap_geterr",       (_func *) &p_pcap_geterr);
      load_function("pcap_lookupnet",    (_func *) &p_pcap_lookupnet);
      load_function("pcap_open_live",    (_func *) &p_pcap_open_live);
#ifdef _WIN32
      load_function("pcap_setmintocopy", (_func *) &p_pcap_setmintocopy);
      load_function("pcap_getevent",     (_func *) &p_pcap_getevent);
#else
#ifdef MUST_DO_SELECT
      load_function("pcap_get_selectable_fd",     (_func *) &p_pcap_get_selectable_fd);
#endif
      load_function("pcap_fileno",       (_func *) &p_pcap_fileno);
#endif
      load_function("pcap_sendpacket",   (_func *) &p_pcap_sendpacket);
      load_function("pcap_setfilter",    (_func *) &p_pcap_setfilter);
      load_function("pcap_setnonblock",  (_func *) &p_pcap_setnonblock);
      load_function("pcap_lib_version",  (_func *) &p_pcap_lib_version);
      break;
    default:                /* loaded or failed */
      break;
  }
  return (lib_loaded == 1) ? 1 : 0;
}

/* define functions with dynamic revectoring */
void pcap_close(pcap_t* a) {
  if (load_pcap() != 0) {
    p_pcap_close(a);
  }
}

/* Some platforms's pcap.h have an ancient declaration of pcap_compile which doesn't have a const in the bpf string argument */
#if !defined (BPF_CONST_STRING)
int pcap_compile(pcap_t* a, struct bpf_program* b, char* c, int d, bpf_u_int32 e) {
#else
int pcap_compile(pcap_t* a, struct bpf_program* b, const char* c, int d, bpf_u_int32 e) {
#endif
  if (load_pcap() != 0) {
    return p_pcap_compile(a, b, c, d, e);
  } else {
    return 0;
  }
}

const char *pcap_lib_version(void) {
  static char buf[256];

  if ((load_pcap() != 0) && (p_pcap_lib_version != NULL)) {
    return p_pcap_lib_version();
  } else {
    sprintf (buf, "%s not installed",
#if defined(_WIN32)
        "npcap or winpcap"
#else
        "libpcap"
#endif
        );
    return buf;
  }
}

int pcap_datalink(pcap_t* a) {
  if (load_pcap() != 0) {
    return p_pcap_datalink(a);
  } else {
    return 0;
  }
}

int pcap_dispatch(pcap_t* a, int b, pcap_handler c, u_char* d) {
  if (load_pcap() != 0) {
    return p_pcap_dispatch(a, b, c, d);
  } else {
    return 0;
  }
}

int pcap_findalldevs(pcap_if_t** a, char* b) {
  if (load_pcap() != 0) {
    return p_pcap_findalldevs(a, b);
  } else {
    *a = 0;
    strcpy(b, no_pcap);
    no_pcap[0] = '\0';
    return -1;
  }
}

void pcap_freealldevs(pcap_if_t* a) {
  if (load_pcap() != 0) {
    p_pcap_freealldevs(a);
  }
}

void pcap_freecode(struct bpf_program* a) {
  if (load_pcap() != 0) {
    p_pcap_freecode(a);
  }
}

char* pcap_geterr(pcap_t* a) {
  if (load_pcap() != 0) {
    return p_pcap_geterr(a);
  } else {
    return (char*) "";
  }
}

int pcap_lookupnet(const char* a, bpf_u_int32* b, bpf_u_int32* c, char* d) {
  if (load_pcap() != 0) {
    return p_pcap_lookupnet(a, b, c, d);
  } else {
    return 0;
  }
}

pcap_t* pcap_open_live(const char* a, int b, int c, int d, char* e) {
  if (load_pcap() != 0) {
    return p_pcap_open_live(a, b, c, d, e);
  } else {
    return (pcap_t*) 0;
  }
}

#ifdef _WIN32
int pcap_setmintocopy(pcap_t* a, int b) {
  if (load_pcap() != 0) {
    return p_pcap_setmintocopy(a, b);
  } else {
    return -1;
  }
}

HANDLE pcap_getevent(pcap_t* a) {
  if (load_pcap() != 0) {
    return p_pcap_getevent(a);
  } else {
    return (HANDLE) 0;
  }
}

#else
#ifdef MUST_DO_SELECT
int pcap_get_selectable_fd(pcap_t* a) {
  if (load_pcap() != 0) {
    return p_pcap_get_selectable_fd(a);
  } else {
    return 0;
  }
}
#endif

int pcap_fileno(pcap_t * a) {
  if (load_pcap() != 0) {
    return p_pcap_fileno(a);
  } else {
    return 0;
  }
}
#endif

int pcap_sendpacket(pcap_t* a, const u_char* b, int c) {
  if (load_pcap() != 0) {
    return p_pcap_sendpacket(a, b, c);
  } else {
    return 0;
  }
}

int pcap_setfilter(pcap_t* a, struct bpf_program* b) {
  if (load_pcap() != 0) {
    return p_pcap_setfilter(a, b);
  } else {
    return 0;
  }
}

int pcap_setnonblock(pcap_t* a, int nonblock, char *errbuf) {
  if (load_pcap() != 0) {
    return p_pcap_setnonblock(a, nonblock, errbuf);
  } else {
    return 0;
  }
}
#endif /* defined(USE_SHARED) && (defined(_WIN32) || defined(HAVE_DLOPEN)) */

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
#if defined (__linux) || defined (__linux__)
  return (send(pcap_fileno(handle), msg, len, 0) == len)? 0 : -1;
#else
  return (write(pcap_fileno(handle), msg, len) == len)? 0 : -1;
#endif /* linux */
}
#endif /* !HAS_PCAP_SENDPACKET */

#if defined(_WIN32) || defined(__CYGWIN__)
/* extracted from WinPcap's Packet32.h */
struct _PACKET_OID_DATA {
    uint32 Oid;                 ///< OID code. See the Microsoft DDK documentation or the file ntddndis.h
                                ///< for a complete list of valid codes.
    uint32 Length;              ///< Length of the data field
    uint8 Data[1];              ///< variable-lenght field that contains the information passed to or received 
                                ///< from the adapter.
}; 
typedef struct _PACKET_OID_DATA PACKET_OID_DATA, *PPACKET_OID_DATA;
typedef void **LPADAPTER;
#define OID_802_3_CURRENT_ADDRESS               0x01010102 /* Extracted from ntddndis.h */

static int pcap_mac_if_win32(const char *AdapterName, unsigned char MACAddress[6])
{
  LPADAPTER         lpAdapter;
  PPACKET_OID_DATA  OidData;
  int               Status;
  int               ReturnValue;
#ifdef _WIN32
  HMODULE           hDll;         /* handle to DLL */
#else
  static void       *hDll = NULL; /* handle to Library */
  typedef int BOOLEAN;
#endif
  LPADAPTER (*p_PacketOpenAdapter)(const char *AdapterName);
  void (*p_PacketCloseAdapter)(LPADAPTER lpAdapter);
  int (*p_PacketRequest)(LPADAPTER  AdapterObject,BOOLEAN Set,PPACKET_OID_DATA  OidData);

#ifdef _WIN32
  hDll = LoadLibraryA("packet.dll");
  p_PacketOpenAdapter = (LPADAPTER (*)(const char *AdapterName))GetProcAddress(hDll, "PacketOpenAdapter");
  p_PacketCloseAdapter = (void (*)(LPADAPTER lpAdapter))GetProcAddress(hDll, "PacketCloseAdapter");
  p_PacketRequest = (int (*)(LPADAPTER  AdapterObject,BOOLEAN Set,PPACKET_OID_DATA  OidData))GetProcAddress(hDll, "PacketRequest");
#else
  hDll = dlopen("packet.dll", RTLD_NOW);
  p_PacketOpenAdapter = (LPADAPTER (*)(const char *AdapterName))dlsym(hDll, "PacketOpenAdapter");
  p_PacketCloseAdapter = (void (*)(LPADAPTER lpAdapter))dlsym(hDll, "PacketCloseAdapter");
  p_PacketRequest = (int (*)(LPADAPTER  AdapterObject,BOOLEAN Set,PPACKET_OID_DATA  OidData))dlsym(hDll, "PacketRequest");
#endif
  
  /* Open the selected adapter */

  lpAdapter =   p_PacketOpenAdapter(AdapterName);

  if (!lpAdapter || (*lpAdapter == (void *)-1)) {
#ifdef _WIN32
      FreeLibrary(hDll);
#else
      dlclose(hDll);
#endif
    return -1;
  }

  /* Allocate a buffer to get the MAC adress */

  OidData = (PACKET_OID_DATA *)malloc(6 + sizeof(PACKET_OID_DATA));
  if (OidData == NULL) {
    p_PacketCloseAdapter(lpAdapter);
#ifdef _WIN32
    FreeLibrary(hDll);
#else
    dlclose(hDll);
#endif
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
#ifdef _WIN32
  FreeLibrary(hDll);
#else
  dlclose(hDll);
#endif
  return ReturnValue;
}

#endif  /* defined(_WIN32) || defined(__CYGWIN__) */

#if defined (__VMS) && !defined(__VAX)
#include <descrip.h>
#include <iodef.h>
#include <ssdef.h>
#include <starlet.h>
#include <stdio.h>
#include <stsdef.h>
#include <nmadef.h>

static int pcap_mac_if_vms(const char *AdapterName, unsigned char MACAddress[6])
{
  char VMS_Device[16];
  $DESCRIPTOR(Device, VMS_Device);
  unsigned short iosb[4];
  unsigned short *w;
  unsigned char *pha = NULL;
  unsigned char *hwa = NULL;
  int tmpval;
  int status;
  unsigned short characteristics[512];
  long chardesc[] = {sizeof(characteristics), (long)&characteristics};
  unsigned short chan;
#pragma member_alignment save
#pragma nomember_alignment
  static struct {
    short fmt;
    long val_fmt;
    short pty;
    long val_pty;
    short pad;
    long val_pad;
    } setup  = {
        NMA$C_PCLI_FMT, NMA$C_LINFM_ETH,
        NMA$C_PCLI_PTY, 0x0090,
        NMA$C_PCLI_PAD, NMA$C_STATE_OFF,
    };
#pragma member_alignment restore
    long setupdesc[] = {sizeof(setup), (long)&setup};

  /* Convert Interface Name to VMS Device Name */
  /* This is a name shuffle */
  /*   WE0 becomes EWA0:    */
  /*   SE1 becomes ESB0:    */
  /*   XE0 becomes EXA0:    */
  tmpval = (int)(AdapterName[2]-'0');
  if ((tmpval < 0) || (tmpval > 25))
    return -1;
  VMS_Device[0] = toupper(AdapterName[1]);
  VMS_Device[1] = toupper(AdapterName[0]);
  VMS_Device[2] = 'A' + tmpval;
  VMS_Device[3] = '0';
  VMS_Device[4] = '\0';
  VMS_Device[5] = '\0';
  Device.dsc$w_length = strlen(VMS_Device);
  if (!$VMS_STATUS_SUCCESS( sys$assign (&Device, &chan, 0, 0, 0) ))
    return -1;
  status = sys$qiow (0, chan, IO$_SETMODE|IO$M_CTRL|IO$M_STARTUP, &iosb, 0, 0, 
                     0, &setupdesc, 0, 0, 0, 0);
  if ((!$VMS_STATUS_SUCCESS(status)) || (!$VMS_STATUS_SUCCESS(iosb[0]))) {
    sys$dassgn(chan);
    return -1;
    }
  status = sys$qiow (0, chan, IO$_SENSEMODE|IO$M_CTRL, &iosb, 0, 0, 
                     0, &chardesc, 0, 0, 0, 0);
  sys$dassgn(chan);
  if ((!$VMS_STATUS_SUCCESS(status)) || (!$VMS_STATUS_SUCCESS(iosb[0])))
    return -1;
  for (w=characteristics; w < &characteristics[iosb[1]]; ) {
    if ((((*w)&0xFFF) == NMA$C_PCLI_HWA) && (6 == *(w+1)))
      hwa = (unsigned char *)(w + 2);
    if ((((*w)&0xFFF) == NMA$C_PCLI_PHA) && (6 == *(w+1)))
      pha = (unsigned char *)(w + 2);
    if (((*w)&0x1000) == 0)
      w += 3;                       /* Skip over Longword Parameter */
    else
      w += (2 + ((1 + *(w+1))/2));  /* Skip over String Parameter */
    }
  if (pha != NULL)                  /* Prefer Physical Address */
    memcpy(MACAddress, pha, 6);
  else
    if (hwa != NULL)                /* Fallback to Hardware Address */
      memcpy(MACAddress, hwa, 6);
    else
      return -1;
  return 0;
}
#endif /* defined (__VMS) && !defined(__VAX) */

static void eth_get_nic_hw_addr(ETH_DEV* dev, const char *devname)
{
  memset(&dev->host_nic_phy_hw_addr, 0, sizeof(dev->host_nic_phy_hw_addr));
  dev->have_host_nic_phy_addr = 0;
  if (dev->eth_api != ETH_API_PCAP)
    return;
#if defined(_WIN32) || defined(__CYGWIN__)
  if (!pcap_mac_if_win32(devname, dev->host_nic_phy_hw_addr))
    dev->have_host_nic_phy_addr = 1;
#elif defined (__VMS) && !defined(__VAX)
  if (!pcap_mac_if_vms(devname, dev->host_nic_phy_hw_addr))
    dev->have_host_nic_phy_addr = 1;
#elif !defined(__CYGWIN__) && !defined(__VMS)
  if (1) {
    char command[1024];
    FILE *f;
    int i;
    const char *patterns[] = {
        "ip link show %.*s | grep [0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]",
        "ip link show %.*s | egrep [0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]",
        "ifconfig %.*s | grep [0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]",
        "ifconfig %.*s | egrep [0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]:[0-9a-fA-F]?[0-9a-fA-F]",
        NULL};

    memset(command, 0, sizeof(command));
    /* try to force an otherwise unused interface to be turned on */
    snprintf(command, sizeof(command)-1, "ip link set dev %.*s up", (int)(sizeof(command) - 21), devname);
    if (system(command)) {};
    snprintf(command, sizeof(command)-1, "ifconfig %.*s up", (int)(sizeof(command) - 14), devname);
    if (system(command)) {};
    for (i=0; patterns[i] && (0 == dev->have_host_nic_phy_addr); ++i) {
      snprintf(command, sizeof(command)-1, patterns[i], (int)(sizeof(command) - (2 + strlen(patterns[i]))), devname);
      if (NULL != (f = popen(command, "r"))) {
        while (0 == dev->have_host_nic_phy_addr) {
          if (fgets(command, sizeof(command)-1, f)) {
            char *p1, *p2;

            p1 = strchr(command, ':');
            while (p1) {
              p2 = strchr(p1+1, ':');
              if (p2 <= p1+3) {
                unsigned int mac_bytes[6];
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
          else
            break;
          }
        pclose(f);
        }
      }
    }
#endif
}

#if defined(__APPLE__)
#include <uuid/uuid.h>
#include <unistd.h>
static int _eth_get_system_id (char *buf, size_t buf_size)
{
static struct timespec wait = {5, 0};   /* 5 seconds */
static uuid_t uuid;

memset (buf, 0, buf_size);
if (buf_size < 37)
  return -1;
if (gethostuuid (uuid, &wait))
  memset (uuid, 0, sizeof(uuid));
uuid_unparse_lower(uuid, buf);
return 0;
}

#elif defined(_WIN32)
static int _eth_get_system_id (char *buf, size_t buf_size)
{
  LONG status;
  DWORD reglen, regtype;
  HKEY reghnd;

  memset (buf, 0, buf_size);
#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY         (0x0100)
#endif
  if ((status = RegOpenKeyExA (HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE|KEY_WOW64_64KEY, &reghnd)) != ERROR_SUCCESS)
    return -1;
  if (buf_size < 37)
    return -1;
  reglen = buf_size - 1;
  if ((status = RegQueryValueExA (reghnd, "MachineGuid", NULL, &regtype, (LPBYTE)buf, &reglen)) != ERROR_SUCCESS) {
    RegCloseKey (reghnd);
    return -1;
    }
  RegCloseKey (reghnd );
  /* make sure value is the right type, bail if not acceptable */
  if ((regtype != REG_SZ) || (reglen > buf_size))
    return -1;
  /* registry value seems OK */
  return 0;
}

#else
static int _eth_get_system_id (char *buf, size_t buf_size)
{
FILE *f;
t_bool popened = FALSE;

memset (buf, 0, buf_size);
if (buf_size < 37)
    return -1;
if ((f = fopen ("/etc/machine-id", "r")) == NULL) {
  f = popen ("hostname", "r");
  popened = TRUE;
  }
if (f) {
  size_t read_size;

  read_size = fread (buf, 1, buf_size - 1, f);
  buf[read_size] = '\0';
  if (popened)
    pclose (f);
  else
    fclose (f);
  }
while ((strlen (buf) > 0) && sim_isspace(buf[strlen (buf) - 1]))
  buf[strlen (buf) - 1] = '\0';
return 0;
}
#endif

/* Forward declarations */
static void
_eth_callback(u_char* info, const struct pcap_pkthdr* header, const u_char* data);

static t_stat
_eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine);

static void
_eth_error(ETH_DEV* dev, const char* where);

#if defined(HAVE_SLIRP_NETWORK)
static void _slirp_callback (void *opaque, const unsigned char *buf, int len)
{
struct pcap_pkthdr header;

memset(&header, 0, sizeof(header));
header.caplen = header.len = len;
_eth_callback((u_char *)opaque, &header, buf);
}
#endif

#if defined (USE_READER_THREAD)
static void *
_eth_reader(void *arg)
{
ETH_DEV* volatile dev = (ETH_DEV*)arg;
int status = 0;
int sel_ret = 0;
int do_select = 0;
SOCKET select_fd = 0;
#if defined (_WIN32)
HANDLE hWait = (dev->eth_api == ETH_API_PCAP) ? pcap_getevent ((pcap_t*)dev->handle) : NULL;
#endif

switch (dev->eth_api) {
  case ETH_API_PCAP:
#if defined (HAVE_PCAP_NETWORK)
#if defined (MUST_DO_SELECT)
    do_select = 1;
    select_fd = pcap_get_selectable_fd((pcap_t *)dev->handle);
#endif
#endif
    break;
  case ETH_API_TAP:
  case ETH_API_VDE:
  case ETH_API_UDP:
  case ETH_API_NAT:
    do_select = 1;
    select_fd = dev->fd_handle;
    break;
  }

sim_debug(dev->dbit, dev->dptr, "Reader Thread Starting\n");

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which, in general, won't be readily yielding the processor 
   when this thread needs to run */
sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

while (dev->handle) {
#if defined (_WIN32)
  if (dev->eth_api == ETH_API_PCAP) {
    if (WAIT_OBJECT_0 == WaitForSingleObject (hWait, 250))
      sel_ret = 1;
    }
  if ((dev->eth_api == ETH_API_UDP) || (dev->eth_api == ETH_API_NAT))
#endif /* _WIN32 */
  if (1) {
    if (do_select) {
#ifdef HAVE_SLIRP_NETWORK
      if (dev->eth_api == ETH_API_NAT) {
        sel_ret = sim_slirp_select ((SLIRP*)dev->handle, 250);
        }
      else
#endif
        {
        fd_set setl;
        struct timeval timeout;
        
        FD_ZERO(&setl);
        FD_SET(select_fd, &setl);
        timeout.tv_sec = 0;
        timeout.tv_usec = 250*1000;
        sel_ret = select(1+select_fd, &setl, NULL, NULL, &timeout);
        }
      }
    else
      sel_ret = 1;
    if (sel_ret < 0 && errno != EINTR) 
      break;
    }
  if (sel_ret > 0) {
    if (!dev->handle)
      break;
    /* dispatch read request queue available packets */
    switch (dev->eth_api) {
#ifdef HAVE_PCAP_NETWORK
      case ETH_API_PCAP:
        status = pcap_dispatch ((pcap_t*)dev->handle, -1, &_eth_callback, (u_char*)dev);
        break;
#endif
#ifdef HAVE_TAP_NETWORK
      case ETH_API_TAP:
        if (1) {
          struct pcap_pkthdr header;
          int len;
          u_char buf[ETH_MAX_JUMBO_FRAME];

          memset(&header, 0, sizeof(header));
          len = read(dev->fd_handle, buf, sizeof(buf));
          if (len > 0) {
            status = 1;
            header.caplen = header.len = len;
            _eth_callback((u_char *)dev, &header, buf);
            }
          else {
            if (len < 0)
              status = -1;
            else
              status = 0;
            }
          }
        break;
#endif /* HAVE_TAP_NETWORK */
#ifdef HAVE_VDE_NETWORK
      case ETH_API_VDE:
        if (1) {
          struct pcap_pkthdr header;
          int len;
          u_char buf[ETH_MAX_JUMBO_FRAME];

          memset(&header, 0, sizeof(header));
          len = vde_recv((VDECONN *)dev->handle, buf, sizeof(buf), 0);
          if (len > 0) {
            status = 1;
            header.caplen = header.len = len;
            _eth_callback((u_char *)dev, &header, buf);
            }
          else {
            if (len < 0)
              status = -1;
            else
              status = 0;
            }
          }
        break;
#endif /* HAVE_VDE_NETWORK */
#ifdef HAVE_SLIRP_NETWORK
      case ETH_API_NAT:
        sim_slirp_dispatch ((SLIRP*)dev->handle);
        status = 1;
        break;
#endif /* HAVE_SLIRP_NETWORK */
      case ETH_API_UDP:
        if (1) {
          struct pcap_pkthdr header;
          int len;
          u_char buf[ETH_MAX_JUMBO_FRAME];

          memset(&header, 0, sizeof(header));
          len = (int)sim_read_sock (select_fd, (char *)buf, (int32)sizeof(buf));
          if (len > 0) {
            status = 1;
            header.caplen = header.len = len;
            _eth_callback((u_char *)dev, &header, buf);
            }
          else {
            if (len < 0)
              status = -1;
            else
              status = 0;
            }
          }
        break;
      }
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
    if (status < 0) {
      ++dev->receive_packet_errors;
      _eth_error (dev, "_eth_reader");
      if (dev->handle) { /* Still attached? */
#if defined (_WIN32)
        hWait = (dev->eth_api == ETH_API_PCAP) ? pcap_getevent ((pcap_t*)dev->handle) : NULL;
#endif
        if (do_select) {
          select_fd = dev->fd_handle;
#if !defined (_WIN32) && defined(HAVE_PCAP_NETWORK)
          if (dev->eth_api == ETH_API_PCAP)
            select_fd = pcap_get_selectable_fd((pcap_t *)dev->handle);
#endif
          }
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
ETH_WRITE_REQUEST *request = NULL;

/* Boost Priority for this I/O thread vs the CPU instruction execution 
   thread which in general won't be readily yielding the processor when 
   this thread needs to run */
sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

sim_debug(dev->dbit, dev->dptr, "Writer Thread Starting\n");

pthread_mutex_lock (&dev->writer_lock);
while (dev->handle) {
  pthread_cond_wait (&dev->writer_cond, &dev->writer_lock);
  while (NULL != (request = dev->write_requests)) {
    if (dev->handle == NULL)      /* Shutting down? */
      break;
    /* Pull buffer off request list */
    dev->write_requests = request->next;
    pthread_mutex_unlock (&dev->writer_lock);

    if (dev->throttle_delay != ETH_THROT_DISABLED_DELAY) {
      uint32 packet_delta_time = sim_os_msec() - dev->throttle_packet_time;
      dev->throttle_events <<= 1;
      dev->throttle_events += (packet_delta_time < dev->throttle_time) ? 1 : 0;
      if ((dev->throttle_events & dev->throttle_mask) == dev->throttle_mask) {
        sim_os_ms_sleep (dev->throttle_delay);
        ++dev->throttle_count;
        }
      dev->throttle_packet_time = sim_os_msec();
      }
    dev->write_status = _eth_write(dev, &request->packet, NULL);

    pthread_mutex_lock (&dev->writer_lock);
    /* Put buffer on free buffer list */
    request->next = dev->write_buffers;
    dev->write_buffers = request;
    request = NULL;
    }
  }
/* If we exited these loops with a request allocated, */
/* avoid buffer leaking by putting it on free buffer list */
if (request) {
  request->next = dev->write_buffers;
  dev->write_buffers = request;
  }
pthread_mutex_unlock (&dev->writer_lock);

sim_debug(dev->dbit, dev->dptr, "Writer Thread Exiting\n");
return NULL;
}
#endif

t_stat eth_set_async (ETH_DEV *dev, int latency)
{
#if !defined(USE_READER_THREAD) || !defined(SIM_ASYNCH_IO)
char *msg = "Eth: Can't operate asynchronously, must poll.\n"
            " *** Build with USE_READER_THREAD defined and link with pthreads for asynchronous operation. ***\n";
return sim_messagef (SCPE_NOFNC, "%s", msg);
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

t_stat eth_set_throttle (ETH_DEV* dev, uint32 time, uint32 burst, uint32 delay)
{
if (!dev)
  return SCPE_IERR;
dev->throttle_time = time;
dev->throttle_burst = burst;
dev->throttle_delay = delay;
dev->throttle_mask = (1 << dev->throttle_burst) - 1;
return SCPE_OK;
}

static t_stat _eth_open_port(char *savname, int *eth_api, void **handle, SOCKET *fd_handle, char errbuf[PCAP_ERRBUF_SIZE], char *bpf_filter, void *opaque, DEVICE *dptr, uint32 dbit)
{
int bufsz = (BUFSIZ < ETH_MAX_PACKET) ? ETH_MAX_PACKET : BUFSIZ;

if (bufsz < ETH_MAX_JUMBO_FRAME)
  bufsz = ETH_MAX_JUMBO_FRAME;    /* Enable handling of jumbo frames */

*eth_api = 0;
*handle = NULL;
*fd_handle = 0;

/* attempt to connect device */
memset(errbuf, 0, PCAP_ERRBUF_SIZE);
if (0 == strncmp("tap:", savname, 4)) {
  int  tun = -1;    /* TUN/TAP Socket */
  int  on = 1;
  const char *devname = savname + 4;

  while (isspace(*devname))
      ++devname;
#if defined(HAVE_TAP_NETWORK)
  if (!strcmp(savname, "tap:tapN"))
    return sim_messagef (SCPE_OPENERR, "Eth: Must specify actual tap device name (i.e. tap:tap0)\n");
#endif
#if (defined(__linux) || defined(__linux__)) && defined(HAVE_TAP_NETWORK)
  if ((tun = open("/dev/net/tun", O_RDWR)) >= 0) {
    struct ifreq ifr; /* Interface Requests */

    memset(&ifr, 0, sizeof(ifr));
    /* Set up interface flags */
    strlcpy(ifr.ifr_name, devname, sizeof(ifr.ifr_name));
    ifr.ifr_flags = IFF_TAP|IFF_NO_PI;

    /* Send interface requests to TUN/TAP driver. */
    if (ioctl(tun, TUNSETIFF, &ifr) >= 0) {
      if (ioctl(tun, FIONBIO, &on)) {
        strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
        close(tun);
        tun = -1;
        }
      else {
        *fd_handle = (SOCKET)tun;
        strcpy(savname, ifr.ifr_name);
        }
      }
    else
      strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
    }
  else
    strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
  if ((tun >= 0) && (errbuf[0] != 0)) {
    close(tun);
    tun = -1;
    }
#elif defined(HAVE_BSDTUNTAP) && defined(HAVE_TAP_NETWORK)
  if (1) {
    char dev_name[64] = "";

    snprintf(dev_name, sizeof(dev_name)-1, "/dev/%s", devname);
    dev_name[sizeof(dev_name)-1] = '\0';

    if ((tun = open(dev_name, O_RDWR)) >= 0) {
      if (ioctl(tun, FIONBIO, &on)) {
        strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
        close(tun);
        tun = -1;
        }
      else {
        *fd_handle = (SOCKET)tun;
        memmove(savname, devname, strlen(devname) + 1);
        }
#if defined (__APPLE__)
      if (tun >= 0) {       /* Good so far? */
        struct ifreq ifr;
        int s;

        /* Now make sure the interface is up */
        memset (&ifr, 0, sizeof(ifr));
        ifr.ifr_addr.sa_family = AF_INET;
        strlcpy(ifr.ifr_name, savname, sizeof(ifr.ifr_name));
        if ((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
          if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) >= 0) {
            ifr.ifr_flags |= IFF_UP;
            if (ioctl(s, SIOCSIFFLAGS, (caddr_t)&ifr)) {
              strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
              close(tun);
              tun = -1;
              }
            }
          close(s);
          }
        }
#endif
      }
    else
      strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
    if ((tun >= 0) && (errbuf[0] != 0)) {
      close(tun);
      tun = -1;
      }
    }
#else
  strlcpy(errbuf, "No support for tap: devices", PCAP_ERRBUF_SIZE);
#endif /* !defined(__linux) && !defined(HAVE_BSDTUNTAP) */
  if (0 == errbuf[0]) {
    *eth_api = ETH_API_TAP;
    *handle = (void *)1;  /* Flag used to indicated open */
    }
  }
else { /* !tap: */
  if (0 == strncmp("vde:", savname, 4)) {
#if defined(HAVE_VDE_NETWORK)
    char vdeswitch_s[CBUFSIZE]; /* VDE switch name */
    char vdeport_s[CBUFSIZE];   /* VDE switch port (optional), numeric */
      
    struct vde_open_args voa;
    const char *devname = savname + 4;

    memset(&voa, 0, sizeof(voa));
    if (!strcmp(savname, "vde:vdedevice"))
      return sim_messagef (SCPE_OPENERR, "Eth: Must specify actual vde device name (i.e. vde:/tmp/switch)\n");
    while (isspace(*devname))
      ++devname;
    devname = get_glyph_nc (devname, vdeswitch_s, ':'); /* Extract switch name          */
    devname = get_glyph_nc (devname, vdeport_s, 0);     /* Extract optional port number */

    if (vdeport_s[0]) {                                 /* port provided? */
      t_stat r;

      voa.port = (int)get_uint (vdeport_s, 10, 255, &r);
      if (r != SCPE_OK)
          return sim_messagef (SCPE_OPENERR, "Eth: Invalid vde port number: %s in %s\n", vdeport_s, savname);
      }

    if (!(*handle = (void*) vde_open((char *)vdeswitch_s, (char *)"simh", &voa)))
      strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
    else {
      *eth_api = ETH_API_VDE;
      *fd_handle = (SOCKET)vde_datafd((VDECONN*)(*handle));
      }
#else
    strlcpy(errbuf, "No support for vde: network devices", PCAP_ERRBUF_SIZE);
#endif /* defined(HAVE_VDE_NETWORK) */
    }
  else { /* !vde: */
    if (0 == strncmp("nat:", savname, 4)) {
#if defined(HAVE_SLIRP_NETWORK)
      const char *devname = savname + 4;

      while (isspace(*devname))
        ++devname;
      if (!(*handle = (void*) sim_slirp_open(devname, opaque, &_slirp_callback, dptr, dbit, errbuf, PCAP_ERRBUF_SIZE)))
        strlcpy(errbuf, strerror(errno), PCAP_ERRBUF_SIZE);
      else {
        *eth_api = ETH_API_NAT;
        *fd_handle = 0;
        }
#else
      strlcpy(errbuf, "No support for nat: network devices", PCAP_ERRBUF_SIZE);
#endif /* defined(HAVE_SLIRP_NETWORK) */
      }
    else { /* not nat: */
      if (0 == strncmp("udp:", savname, 4)) {
        char localport[CBUFSIZE], host[CBUFSIZE], port[CBUFSIZE];
        char hostport[2*CBUFSIZE];
        const char *devname = savname + 4;

        if (!strcmp(savname, "udp:sourceport:remotehost:remoteport"))
          return sim_messagef (SCPE_OPENERR, "Eth: Must specify actual udp host and ports(i.e. udp:1224:somehost.com:2234)\n");

        while (isspace(*devname))
          ++devname;
        if (SCPE_OK != sim_parse_addr_ex (devname, host, sizeof(host), "localhost", port, sizeof(port), localport, sizeof(localport), NULL))
          return SCPE_OPENERR;

        if (localport[0] == '\0')
          strcpy (localport, port);
        sprintf (hostport, "%s:%s", host, port);
        if ((SCPE_OK == sim_parse_addr (hostport, NULL, 0, NULL, NULL, 0, NULL, "localhost")) &&
            (0 == strcmp (localport, port)))
          return sim_messagef (SCPE_OPENERR, "Eth: Must specify different udp localhost ports\n");
        *fd_handle = sim_connect_sock_ex (localport, hostport, NULL, NULL, SIM_SOCK_OPT_DATAGRAM);
        if (INVALID_SOCKET == *fd_handle)
          return SCPE_OPENERR;
        *eth_api = ETH_API_UDP;
        *handle = (void *)1;  /* Flag used to indicated open */
        }
      else { /* not udp:, so attempt to open the parameter as if it were an explicit device name */
#if defined(HAVE_PCAP_NETWORK)
        *handle = (void*) pcap_open_live(savname, bufsz, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
#if !defined(__CYGWIN__) && !defined(__VMS) && !defined(_WIN32)
        if (!*handle) { /* can't open device */
          if (strstr (errbuf, "That device is not up")) {
            char command[1024];

            /* try to force an otherwise unused interface to be turned on */
            memset(command, 0, sizeof(command));
            snprintf(command, sizeof(command)-1, "ifconfig %s up", savname);
            if (system(command)) {};
            errbuf[0] = '\0';
            *handle = (void*) pcap_open_live(savname, bufsz, ETH_PROMISC, PCAP_READ_TIMEOUT, errbuf);
            }
          }
#endif
        if (!*handle)  /* can't open device */
          return sim_messagef (SCPE_OPENERR, "Eth: pcap_open_live error - %s\n", errbuf);
        *eth_api = ETH_API_PCAP;
#if !defined(HAS_PCAP_SENDPACKET) && defined (xBSD) && !defined (__APPLE__)
        /* Tell the kernel that the header is fully-formed when it gets it.
           This is required in order to fake the src address. */
        if (1) {
          int one = 1;
          ioctl(pcap_fileno(*handle), BIOCSHDRCMPLT, &one);
          }
#endif /* xBSD */
#if defined(_WIN32)
        if ((pcap_setmintocopy ((pcap_t*)(*handle), 0) == -1) ||
            (pcap_getevent ((pcap_t*)(*handle)) == NULL)) {
          pcap_close ((pcap_t*)(*handle));
          errbuf[PCAP_ERRBUF_SIZE-1] = '\0';
          snprintf (errbuf, PCAP_ERRBUF_SIZE-1, "pcap can't initialize API for interface: %s", savname);
          return SCPE_OPENERR;
          }
#endif
#if !defined (USE_READER_THREAD)
#ifdef USE_SETNONBLOCK
        /* set ethernet device non-blocking so pcap_dispatch() doesn't hang */
        if (pcap_setnonblock (*handle, 1, errbuf) == -1) {
          sim_printf ("Eth: Failed to set non-blocking: %s\n", errbuf);
          }
#endif
#if defined (__APPLE__)
        if (1) {
          /* Deliver packets immediately, needed for OS X 10.6.2 and later
           * (Snow-Leopard).
           * See this thread on libpcap and Mac Os X 10.6 Snow Leopard on
           * the tcpdump mailinglist: http://seclists.org/tcpdump/2010/q1/110
           */
          int v = 1;
          ioctl(pcap_fileno(*handle), BIOCIMMEDIATE, &v);
          }
#endif /* defined (__APPLE__) */
#endif /* !defined (USE_READER_THREAD) */
#else
        strlcpy (errbuf, "Unknown or unsupported network device", PCAP_ERRBUF_SIZE);
#endif /* defined(HAVE_PCAP_NETWORK) */
        } /* not udp:, so attempt to open the parameter as if it were an explicit device name */
      } /* !nat: */
    } /* !vde: */
  } /* !tap: */
if (errbuf[0])
  return SCPE_OPENERR;

#ifdef USE_BPF
if (bpf_filter && (*eth_api == ETH_API_PCAP)) {
  struct bpf_program bpf;
  int status;
  bpf_u_int32  bpf_subnet, bpf_netmask;

  if (pcap_lookupnet(savname, &bpf_subnet, &bpf_netmask, errbuf)<0)
    bpf_netmask = 0;
  /* compile filter string */
  if ((status = pcap_compile((pcap_t*)(*handle), &bpf, bpf_filter, 1, bpf_netmask)) < 0) {
    sprintf(errbuf, "%s", pcap_geterr((pcap_t*)(*handle)));
    sim_printf("Eth: pcap_compile error: %s\n", errbuf);
    /* show erroneous BPF string */
    sim_printf ("Eth: BPF string is: |%s|\n", bpf_filter);
    }
  else {
    /* apply compiled filter string */
    if ((status = pcap_setfilter((pcap_t*)(*handle), &bpf)) < 0) {
      sprintf(errbuf, "%s", pcap_geterr((pcap_t*)(*handle)));
      sim_printf("Eth: pcap_setfilter error: %s\n", errbuf);
      }
    else {
#ifdef USE_SETNONBLOCK
      /* set file non-blocking */
      status = pcap_setnonblock ((pcap_t*)(*handle), 1, errbuf);
#endif /* USE_SETNONBLOCK */
      }
    pcap_freecode(&bpf);
    }
  }
#endif /* USE_BPF */
return SCPE_OK;
}

t_stat eth_open(ETH_DEV* dev, const char* name, DEVICE* dptr, uint32 dbit)
{
t_stat r;
int bufsz = (BUFSIZ < ETH_MAX_PACKET) ? ETH_MAX_PACKET : BUFSIZ;
char errbuf[PCAP_ERRBUF_SIZE];
char temp[1024], desc[1024] = "";
const char* savname = name;
char namebuf[4*CBUFSIZE];
int   num;

if (bufsz < ETH_MAX_JUMBO_FRAME)
  bufsz = ETH_MAX_JUMBO_FRAME;    /* Enable handling of jumbo frames */

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
  savname = _eth_getname(num, temp, desc);
  if (savname == NULL) /* didn't translate */
    return SCPE_OPENERR;
  }
else {
  /* are they trying to use device description? */
  savname = eth_getname_bydesc(name, temp, desc);
  if (savname == NULL) { /* didn't translate */
    /* probably is not ethX and has no description */
    savname = eth_getname_byname(name, temp, desc);
    if (savname == NULL) {/* didn't translate */
      savname = name;
      desc[0] = '\0';   /* no description */
      }
    }
  }

namebuf[sizeof(namebuf)-1] = '\0';
strlcpy (namebuf, savname, sizeof(namebuf));
if (strchr (namebuf, ':')) {
    for (num = 0; (namebuf[num] != ':') && (namebuf[num] != '\0'); num++)
        if (isupper (namebuf[num]))
            namebuf[num] = tolower (namebuf[num]);
    }
savname = namebuf;
r = _eth_open_port(namebuf, &dev->eth_api, &dev->handle, &dev->fd_handle, errbuf, NULL, (void *)dev, dptr, dbit);

if (errbuf[0])
  return sim_messagef (SCPE_OPENERR, "Eth: open error - %s\n", errbuf);
if (r != SCPE_OK)
  return r;

if (!strcmp (desc, "No description available"))
    strcpy (desc, "");
sim_messagef (SCPE_OK, "Eth: opened OS device %s%s%s\n", savname, desc[0] ? " - " : "", desc);

/* get the NIC's hardware MAC address */
eth_get_nic_hw_addr(dev, savname);

/* save name of device */
dev->name = (char *)malloc(strlen(savname)+1);
strcpy(dev->name, savname);

/* save debugging information */
dev->dptr = dptr;
dev->dbit = dbit;

#if defined (USE_READER_THREAD)
if (1) {
  pthread_attr_t attr;

  ethq_init (&dev->read_queue, 200);         /* initialize FIFO queue */
  pthread_mutex_init (&dev->lock, NULL);
  pthread_mutex_init (&dev->writer_lock, NULL);
  pthread_mutex_init (&dev->self_lock, NULL);
  pthread_cond_init (&dev->writer_cond, NULL);
  pthread_attr_init(&attr);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#if defined(__hpux)
  {
    /* libpcap needs sizeof(long) * 8192 bytes on the stack */
    size_t stack_size;
    const size_t min_stack_size = sizeof(long) * 8192 * 3 / 2;
    if (!pthread_attr_getstacksize(&attr, &stack_size) && stack_size < min_stack_size) {
      pthread_attr_setstacksize(&attr, min_stack_size);
    }
  }
#endif /* defined(__hpux) */
  pthread_create (&dev->reader_thread, &attr, _eth_reader, (void *)dev);
  pthread_create (&dev->writer_thread, &attr, _eth_writer, (void *)dev);
  pthread_attr_destroy(&attr);
  }
#endif /* defined (USE_READER_THREAD */
_eth_add_to_open_list (dev);
/* 
 * install a total filter on a newly opened interface and let the device
 * simulator install an appropriate filter that reflects the device's
 * configuration.
 */
return eth_filter_hash (dev, 0, NULL, FALSE, FALSE, NULL);
}

static t_stat _eth_close_port(int eth_api, pcap_t *pcap, SOCKET pcap_fd)
{
switch (eth_api) {
#ifdef HAVE_PCAP_NETWORK
  case ETH_API_PCAP:
    pcap_close(pcap);
    break;
#endif
#ifdef HAVE_TAP_NETWORK
  case ETH_API_TAP:
    close(pcap_fd);
    break;
#endif
#ifdef HAVE_VDE_NETWORK
  case ETH_API_VDE:
    vde_close((VDECONN*)pcap);
    break;
#endif
#ifdef HAVE_SLIRP_NETWORK
  case ETH_API_NAT:
    sim_slirp_close((SLIRP*)pcap);
    break;
#endif
  case ETH_API_UDP:
    sim_close_sock(pcap_fd);
    break;
  }
return SCPE_OK;
}

t_stat eth_close(ETH_DEV* dev)
{
pcap_t *pcap;
SOCKET pcap_fd;

/* make sure device exists */
if (!dev) return SCPE_UNATT;

/* close the device */
pcap_fd = dev->fd_handle;                   /* save handle to possibly close later */
pcap = (pcap_t *)dev->handle;
dev->handle = NULL;
dev->fd_handle = 0;
dev->have_host_nic_phy_addr = 0;

#if defined (USE_READER_THREAD)
pthread_join (dev->reader_thread, NULL);
pthread_mutex_destroy (&dev->lock);
pthread_cond_signal (&dev->writer_cond);
pthread_join (dev->writer_thread, NULL);
pthread_mutex_destroy (&dev->self_lock);
pthread_mutex_destroy (&dev->writer_lock);
pthread_cond_destroy (&dev->writer_cond);
if (1) {
  ETH_WRITE_REQUEST *buffer;
   while (NULL != (buffer = dev->write_buffers)) {
    dev->write_buffers = buffer->next;
    free(buffer);
    }
  while (NULL != (buffer = dev->write_requests)) {
    dev->write_requests = buffer->next;
    free(buffer);
    }
  }
ethq_destroy (&dev->read_queue);         /* release FIFO queue */
#endif

_eth_close_port (dev->eth_api, pcap, pcap_fd);
sim_messagef (SCPE_OK, "Eth: closed %s\n", dev->name);

/* clean up the mess */
free(dev->name);
free(dev->bpf_filter);
eth_zero(dev);
_eth_remove_from_open_list (dev);
return SCPE_OK;
}

const char *eth_version (void)
{
#if defined(HAVE_PCAP_NETWORK)
static char version[300];

if (!version[0]) {
  strlcpy(version, pcap_lib_version(), sizeof(version));
  if (memcmp(pcap_lib_version(), "Npcap", 5) == 0) {
    char maj_min[CBUFSIZE];
    char *c = version;

    while (*c && !isdigit (*c))
      ++c;
    get_glyph (c, maj_min, ',');
    if (strcmp ("0.9990", maj_min) < 0)
      snprintf(version, sizeof(version), "Unsupported - %s", pcap_lib_version());
    }
  }
return version;
#else
return NULL;
#endif
}

t_stat eth_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s attach help\n\n", dptr->name);
fprintf (st, "   sim> SHOW ETHERNET\n");
fprintf (st, "   libpcap version 1.0.0\n");
fprintf (st, "   ETH devices:\n");
fprintf (st, "    eth0   en0                                  (No description available)\n");
#if defined(HAVE_TAP_NETWORK)
fprintf (st, "    eth1   tap:tapN                             (Integrated Tun/Tap support)\n");
#endif
#if defined(HAVE_VDE_NETWORK)
fprintf (st, "    eth2   vde:device{:switch-port-number}      (Integrated VDE support)\n");
#endif
#if defined(HAVE_SLIRP_NETWORK)
fprintf (st, "    eth3   nat:{optional-nat-parameters}        (Integrated NAT (SLiRP) support)\n");
#endif
fprintf (st, "    eth4   udp:sourceport:remotehost:remoteport (Integrated UDP bridge support)\n");
fprintf (st, "   sim> ATTACH %s eth0\n\n", dptr->name);
fprintf (st, "or equivalently:\n\n");
fprintf (st, "   sim> ATTACH %s en0\n\n", dptr->name);
#if defined(HAVE_SLIRP_NETWORK)
sim_slirp_attach_help (st, dptr, uptr, flag, cptr);
#endif
return SCPE_OK;
}

static int _eth_rand_byte()
{
static int rand_initialized = 0;

if (!rand_initialized)
  srand((unsigned int)sim_os_msec());
return (rand() & 0xFF);
}

t_stat eth_check_address_conflict_ex (ETH_DEV* dev, 
                                      ETH_MAC* const mac,
                                      int *reflections,
                                      t_bool silent)
{
ETH_PACK send, recv;
t_stat status;
uint32 i;
int responses = 0;
uint32 offset, function;
char mac_string[32];

if (reflections)
    *reflections = 0;
eth_mac_fmt(mac, mac_string);
sim_debug(dev->dbit, dev->dptr, "Determining Address Conflict for MAC address: %s\n", mac_string);

/* 00:00:00:00:00:00 or any address with a multi-cast address is invalid */
if ((((*mac)[0] == 0) && ((*mac)[1] == 0) && ((*mac)[2] == 0) && 
     ((*mac)[3] == 0) && ((*mac)[4] == 0) && ((*mac)[5] == 0)) ||
     ((*mac)[0] & 1)) {
  return sim_messagef (SCPE_ARG, "%s: Invalid NIC MAC Address: %s\n", sim_dname(dev->dptr), mac_string);
  }

/* The process of checking address conflicts is used in two ways:
   1) to determine the behavior of the currently running packet 
      delivery facility regarding whether it may receive copies 
      of every packet sent (and how many). 
   2) to verify if a MAC address which this facility is planning 
      to use as the source address of packets is already in use 
      by some other node on the local network 
   Case #1, doesn't require (and explicitly doesn't want) any 
   interaction or response from other systems on the LAN so 
   therefore no considerations regarding switch packet forwarding 
   are important.  Meanwhile, Case #2 does require responses from 
   other components on the LAN to provide useful functionality. 
   The original designers of this mechanism did this when essentially 
   all LANs were single collision domains (i.e. ALL nodes which might 
   be affected by an address conflict were physically present on a single
   Ethernet cable which might have been extended by a couple of repeaters).
   Since that time, essentially no networks are single collision domains.  
   Thick and thinwire Ethernet cables don't exist and very few networks 
   even have hubs.  Today, essentially all LANs are deployed using one 
   or more layers of network switches.  In a switched LAN environment, the 
   switches on the LAN "learn" which ports on the LAN source traffic from 
   which MAC addresses and then forward traffic destined for particular 
   MAC address to the appropriate ports.  If a particular MAC address is
   already in use somewhere on the LAN, then the switches "know" where 
   it is.  The host based test using the loopback protocol is poorly 
   designed to detect this condition.  This test is performed by the host
   first changing the device's Physical MAC address to the address which
   is to be tested, and then sending a loopback packet FROM AND TO this
   MAC address with a loopback reply to be sent by a system which may be
   currently using the MAC address.  If no reply is received, then the 
   MAC address is presumed to be unused.  The sending of this packet will
   result in its delivery to the right system since the switch port/MAC
   address tables know where to deliver packets destined to this MAC 
   address, however the response it generates won't be delivered to the 
   system performing the test since the switches on the LAN won't know 
   about the local port being the right target for packets with this MAC 
   address.  A better test design to detect these conflicts would be for 
   the testing system to send a loopback packet FROM the current physical
   MAC address (BEFORE changing it) TO the MAC address being tested with 
   the loopback response coming to the current physical MAC address of 
   the device.  If a response is received, then the address is in use and
   the attempt to change the device's MAC address should fail.  Since we 
   can't change the software running in these simulators to implement this
   better conflict detection approach, we can still "do the right thing" 
   in the sim_ether layer.  We're already handling the loopback test 
   packets specially since we always had to avoid receiving the packets 
   which were being sent, but needed to allow for the incoming loopback 
   packets to be properly dealt with.  We can extend this current special
   handling to change outgoing "loopback to self" packets to have source 
   AND loopback destination addresses in the packets to be the host NIC's
   physical address.  The switch network will already know the correct 
   MAC/port relationship for the host NIC's physical address, so loopback 
   response packets will be delivered as needed.

   Code in _eth_write and _eth_callback provide the special handling to 
   perform the described loopback packet adjustments, and code in 
   eth_filter_hash makes sure that the loopback response packets are received.

   */

/* build a loopback forward request packet */
memset (&send, 0, sizeof(ETH_PACK));
send.len = ETH_MIN_PACKET;                              /* minimum packet size */
for (i=0; i<send.len; i++)
  send.msg[i] = _eth_rand_byte();
memcpy(&send.msg[0], mac, sizeof(ETH_MAC));             /* target address */
memcpy(&send.msg[6], mac, sizeof(ETH_MAC));             /* source address */
send.msg[12] = 0x90;                                    /* loopback packet type */
send.msg[13] = 0;
send.msg[14] = 0;                                       /* Offset */
send.msg[15] = 0;
send.msg[16] = 2;                                       /* Forward */
send.msg[17] = 0;
memcpy(&send.msg[18], mac, sizeof(ETH_MAC));            /* Forward Destination */
send.msg[24] = 1;                                       /* Reply */
send.msg[25] = 0;

eth_filter(dev, 1, (ETH_MAC *)mac, 0, 0);

/* send the packet */
status = _eth_write (dev, &send, NULL);
if (status != SCPE_OK) {
  const char *msg;
  msg = (dev->eth_api == ETH_API_PCAP) ?
      "%s: Eth: Error Transmitting packet: %s\n"
        "You may need to run as root, or install a libpcap version\n"
        "which is at least 0.9 from your OS vendor or www.tcpdump.org\n" :
      "%s: Eth: Error Transmitting packet: %s\n"
        "You may need to run as root.\n";
  return sim_messagef (SCPE_ARG, msg, sim_dname (dev->dptr), strerror(errno));
  }

sim_os_ms_sleep (300);   /* time for a conflicting host to respond */

eth_packet_trace_detail (dev, send.msg, send.len, "Sent-Address-Check");

/* empty the read queue and count the responses */
do {
  memset (&recv, 0, sizeof(ETH_PACK));
  status = eth_read (dev, &recv, NULL);
  eth_packet_trace_detail (dev, recv.msg, recv.len, "Recv-Address-Check");
  offset = 16 + (recv.msg[14] | (recv.msg[15] << 8));
  function = 0;
  if ((offset+2) < recv.len)
    function = recv.msg[offset] | (recv.msg[offset+1] << 8);
  if (((0 == memcmp(send.msg+12, recv.msg+12, 2)) &&   /* Protocol Match */
       (function == 1) &&                              /* Function is Reply */
       (0 == memcmp(&send.msg[offset], &recv.msg[offset], send.len-offset))) || /* Content Match */
      (0 == memcmp(send.msg, recv.msg, send.len)))     /* Packet Match (Reflection) */
    responses++;
  } while (recv.len > 0);

sim_debug(dev->dbit, dev->dptr, "Address Conflict = %d\n", responses);
if (responses && !silent)
  return sim_messagef (SCPE_ARG, "%s: MAC Address Conflict on LAN for address %s, change the MAC address to a unique value\n", sim_dname (dev->dptr), mac_string);
if (reflections)
  *reflections = responses;
return SCPE_OK;
}

t_stat eth_check_address_conflict (ETH_DEV* dev, 
                                   ETH_MAC* const mac)
{
char mac_string[32];

eth_mac_fmt(mac, mac_string);
if (0 == memcmp (mac, dev->host_nic_phy_hw_addr, sizeof *mac))
    return sim_messagef (SCPE_OK, "Sharing the host NIC MAC address %s may cause unexpected behavior\n", mac_string);
return eth_check_address_conflict_ex (dev, mac, NULL, FALSE);
}

t_stat eth_reflect(ETH_DEV* dev)
{
t_stat r;

/* Test with an address no NIC should have. */
/* We do this to avoid reflections from the wire, */
/* in the event that a simulated NIC has a MAC address conflict. */
static ETH_MAC mac = {0xfe,0xff,0xff,0xff,0xff,0xfe};

sim_debug(dev->dbit, dev->dptr, "Determining Reflections...\n");

r = eth_check_address_conflict_ex (dev, &mac, &dev->reflections, TRUE);
if (r != SCPE_OK)
  return sim_messagef (r, "eth: Error determining reflection count\n");

sim_debug(dev->dbit, dev->dptr, "Reflections = %d\n", dev->reflections);
return SCPE_OK;
}

static void
_eth_error(ETH_DEV* dev, const char* where)
{
char msg[64];
const char *netname = "";
time_t now;

time(&now);
sim_printf ("%s", asctime(localtime(&now)));
switch (dev->eth_api) {
  case ETH_API_PCAP:
      netname = "pcap";
      break;
  case ETH_API_TAP:
      netname = "tap";
      break;
  case ETH_API_VDE:
      netname = "vde";
      break;
  case ETH_API_UDP:
      netname = "udp";
      break;
  case ETH_API_NAT:
      netname = "nat";
      break;
  }
sprintf(msg, "%s(%s): ", where, netname);
switch (dev->eth_api) {
#if defined(HAVE_PCAP_NETWORK)
  case ETH_API_PCAP:
      sim_printf ("%s%s\n", msg, pcap_geterr ((pcap_t*)dev->handle));
      break;
#endif
  default:
      sim_err_sock (INVALID_SOCKET, msg);
      break;
  }
#ifdef USE_READER_THREAD
pthread_mutex_lock (&dev->lock);
++dev->error_waiting_threads;
if (!dev->error_needs_reset)
  dev->error_needs_reset = (((dev->transmit_packet_errors + dev->receive_packet_errors)%ETH_ERROR_REOPEN_THRESHOLD) == 0);
pthread_mutex_unlock (&dev->lock);
#else
dev->error_needs_reset = (((dev->transmit_packet_errors + dev->receive_packet_errors)%ETH_ERROR_REOPEN_THRESHOLD) == 0);
#endif
/* Limit errors to 1 per second (per invoking thread (reader and writer)) */
sim_os_sleep (1);
/* 
 When all of the threads which can reference this ETH_DEV object are
 simultaneously waiting in this routine, we have the potential to close
 and reopen the network connection.
 We do this after ETH_ERROR_REOPEN_THRESHOLD total errors have occurred.  
 In practice could be as frequently as once every ETH_ERROR_REOPEN_THRESHOLD/2 
 seconds, but normally would be about once every 1.5*ETH_ERROR_REOPEN_THRESHOLD 
 seconds (ONLY when the error condition exists).
 */
#ifdef USE_READER_THREAD
pthread_mutex_lock (&dev->lock);
if ((dev->error_waiting_threads == 2) &&
    (dev->error_needs_reset)) {
#else
if (dev->error_needs_reset) {
#endif
  char errbuf[PCAP_ERRBUF_SIZE];
  t_stat r;

  _eth_close_port(dev->eth_api, (pcap_t *)dev->handle, dev->fd_handle);
  sim_os_sleep (ETH_ERROR_REOPEN_PAUSE);

  r = _eth_open_port(dev->name, &dev->eth_api, &dev->handle, &dev->fd_handle, errbuf, dev->bpf_filter, (void *)dev, dev->dptr, dev->dbit);
  dev->error_needs_reset = FALSE;
  if (r == SCPE_OK)
    sim_printf ("%s ReOpened: %s \n", msg, dev->name);
  else
    sim_printf ("%s ReOpen Attempt Failed: %s - %s\n", msg, dev->name, errbuf);
  ++dev->error_reopen_count;
  }
#ifdef USE_READER_THREAD
--dev->error_waiting_threads;
pthread_mutex_unlock (&dev->lock);
#endif
}

static
t_stat _eth_write(ETH_DEV* dev, ETH_PACK* packet, ETH_PCALLBACK routine)
{
int status = 1;   /* default to failure */

/* make sure device exists */
if ((!dev) || (dev->eth_api == ETH_API_NONE)) return SCPE_UNATT;

/* make sure packet exists */
if (!packet) return SCPE_ARG;

/* make sure packet is acceptable length */
if ((packet->len >= ETH_MIN_PACKET) && (packet->len <= ETH_MAX_PACKET)) {
  int loopback_self_frame = LOOPBACK_SELF_FRAME(packet->msg, packet->msg);
  int loopback_physical_response = LOOPBACK_PHYSICAL_RESPONSE(dev, packet->msg);

  eth_packet_trace (dev, packet->msg, packet->len, "writing");

  /* record sending of loopback packet (done before actual send to avoid race conditions with receiver) */
  if (loopback_self_frame || loopback_physical_response) {
    /* Direct loopback responses to the host physical address since our physical address
       may not have been learned yet. */
    if (loopback_self_frame && dev->have_host_nic_phy_addr) {
      memcpy(&packet->msg[6],  dev->host_nic_phy_hw_addr, sizeof(ETH_MAC));
      memcpy(&packet->msg[18], dev->host_nic_phy_hw_addr, sizeof(ETH_MAC));
      eth_packet_trace (dev, packet->msg, packet->len, "writing-fixed");
    }
#ifdef USE_READER_THREAD
    pthread_mutex_lock (&dev->self_lock);
#endif
    dev->loopback_self_sent += dev->reflections;
    dev->loopback_self_sent_total++;
#ifdef USE_READER_THREAD
    pthread_mutex_unlock (&dev->self_lock);
#endif
  }

    /* dispatch write request (synchronous; no need to save write info to dev) */
  switch (dev->eth_api) {
#ifdef HAVE_PCAP_NETWORK
    case ETH_API_PCAP:
      status = pcap_sendpacket((pcap_t*)dev->handle, (u_char*)packet->msg, packet->len);
      break;
#endif
#ifdef HAVE_TAP_NETWORK
    case ETH_API_TAP:
      status = (((int)packet->len == write(dev->fd_handle, (void *)packet->msg, packet->len)) ? 0 : -1);
      break;
#endif
#ifdef HAVE_VDE_NETWORK
    case ETH_API_VDE:
      status = vde_send((VDECONN*)dev->handle, (void *)packet->msg, packet->len, 0);
      if ((status == (int)packet->len) || (status == 0))
        status = 0;
      else
        if ((status == -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
          status = 0;
        else
          status = 1;
      break;
#endif
#ifdef HAVE_SLIRP_NETWORK
    case ETH_API_NAT:
      status = sim_slirp_send((SLIRP*)dev->handle, (char *)packet->msg, (size_t)packet->len, 0);
      if ((status == (int)packet->len) || (status == 0))
        status = 0;
      else
        status = 1;
      break;
#endif
    case ETH_API_UDP:
      status = (((int32)packet->len == sim_write_sock (dev->fd_handle, (char *)packet->msg, (int32)packet->len)) ? 0 : -1);
      break;
    }
  ++dev->packets_sent;              /* basic bookkeeping */
  /* On error, correct loopback bookkeeping */
  if ((status != 0) && loopback_self_frame) {
#ifdef USE_READER_THREAD
    pthread_mutex_lock (&dev->self_lock);
#endif
    dev->loopback_self_sent -= dev->reflections;
    dev->loopback_self_sent_total--;
#ifdef USE_READER_THREAD
    pthread_mutex_unlock (&dev->self_lock);
#endif
    }
  if (status != 0) {
    ++dev->transmit_packet_errors;
    _eth_error (dev, "_eth_write");
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
ETH_WRITE_REQUEST *request;
int write_queue_size = 1;

/* make sure device exists */
if ((!dev) || (dev->eth_api == ETH_API_NONE)) return SCPE_UNATT;

/* Get a buffer */
pthread_mutex_lock (&dev->writer_lock);
if (NULL != (request = dev->write_buffers))
  dev->write_buffers = request->next;
pthread_mutex_unlock (&dev->writer_lock);
if (NULL == request)
  request = (ETH_WRITE_REQUEST *)malloc(sizeof(*request));

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
  ETH_WRITE_REQUEST *last_request = dev->write_requests;

  ++write_queue_size;
  while (last_request->next) {
    last_request = last_request->next;
    ++write_queue_size;
    }
  last_request->next = request;
  }
else
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

#if 0
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
else {
  printf("Should be: %02X %02X %02X %02X %02X %02X %02X %02X\n", 
         hash[0], hash[1], hash[2], hash[3], 
         hash[4], hash[5], hash[6], hash[7]);
  printf("Was:       %02X %02X %02X %02X %02X %02X %02X %02X\n", 
         lhash[0], lhash[1], lhash[2], lhash[3], 
         lhash[4], lhash[5], lhash[6], lhash[7]);
  }
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
#endif

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
#define TCP_FLAGS_MASK (0xFFF)
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
  uint16 endword;
  uint8 *endbytes = (uint8 *)&endword;

  endbytes[0] = *((uint8 *)buffer);
  endbytes[1] = 0;
  cksum += endword;
  }

/* Do a little shuffling  */
cksum = (cksum >> 16) + (cksum & 0xffff);
cksum += (cksum >> 16);
    
/* Return the bitwise complement of the resulting mishmash  */
return (uint16)(~cksum);
}

/* 
 * src_addr and dest_addr are presented in network byte order
 */

static uint16 
pseudo_checksum(uint16 len, uint16 proto, void *nsrc_addr, void *ndest_addr, uint8 *buff)
{
uint32 sum;
uint16 *src_addr = (uint16 *)nsrc_addr;
uint16 *dest_addr = (uint16 *)ndest_addr;

/* Sum the data first */
sum = 0xffff&(~ip_checksum((uint16 *)buff, len));

/* add the pseudo header which contains the IP source and 
   destination addresses already in network byte order */
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
_eth_fix_ip_jumbo_offload(ETH_DEV* dev, u_char* msg, int len)
{
const unsigned short* proto = (const unsigned short*) &msg[12];
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
uint16 orig_tcp_flags;

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
      break; /* UDP Checksums are disabled */
    orig_checksum = UDP->checksum;
    UDP->checksum = 0;
    UDP->checksum = pseudo_checksum(ntohs(UDP->length), IPPROTO_UDP, &IP->source_ip, &IP->dest_ip, (uint8 *)UDP);
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
    /* When we're performing LSO (Large Send Offload), we're given a 
       'template' header which may not include a value being populated 
       in the IP header length (which is only 16 bits).
       We process as payload everything which isn't known header data. */
    payload_len = (uint16)(len - (14 + IP_HLEN(IP)));
    mtu_payload = ETH_MIN_JUMBO_FRAME - (14 + IP_HLEN(IP));
    frag_offset = 0;
    while (payload_len > 0) {
      ip_flags = frag_offset;
      if (payload_len > mtu_payload) {
        ip_flags |= IP_MF_FLAG;
        IP->total_len = htons(((mtu_payload>>3)<<3) + IP_HLEN(IP));
        }
      else {
        IP->total_len = htons(payload_len + IP_HLEN(IP));
        }
      IP->flags = htons(ip_flags);
      IP->checksum = 0;
      IP->checksum = ip_checksum((uint16 *)IP, IP_HLEN(IP));
      header.caplen = header.len = 14 + ntohs(IP->total_len);
      eth_packet_trace (dev, ((u_char *)IP)-14, header.len, "reading Datagram fragment");
#if ETH_MIN_JUMBO_FRAME < ETH_MAX_PACKET
      if (1) {
        /* Debugging is easier if we read packets directly with pcap
           (i.e. we can use Wireshark to verify packet contents)
           we don't want to do this all the time for 2 reasons:
             1) sending through pcap involves kernel transitions and
             2) if the current system reflects sent packets, the 
                recieving side will receive and process 2 copies of 
                any packets sent this way. */
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
    eth_packet_trace_ex (dev, ((u_char *)IP)-14, len, "Fragmenting Jumbo TCP segment", 1, dev->dbit);
    TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
    orig_tcp_flags = ntohs(TCP->data_offset_and_flags);
    /* When we're performing LSO (Large Send Offload), we're given a 
       'template' header which may not include a value being populated 
       in the IP header length (which is only 16 bits).
       We process as payload everything which isn't known header data. */
    payload_len = (uint16)(len - (14 + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP)));
    mtu_payload = ETH_MIN_JUMBO_FRAME - (14 + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
    while (payload_len > 0) {
      if (payload_len > mtu_payload) {
        TCP->data_offset_and_flags = htons(orig_tcp_flags&~(TCP_PSH_FLAG|TCP_FIN_FLAG|TCP_RST_FLAG));
        IP->total_len = htons(mtu_payload + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
        }
      else {
        TCP->data_offset_and_flags = htons(orig_tcp_flags);
        IP->total_len = htons(payload_len + IP_HLEN(IP) + TCP_DATA_OFFSET(TCP));
        }
      IP->checksum = 0;
      IP->checksum = ip_checksum((uint16 *)IP, IP_HLEN(IP));
      TCP->checksum = 0;
      TCP->checksum = pseudo_checksum(ntohs(IP->total_len)-IP_HLEN(IP), IPPROTO_TCP, &IP->source_ip, &IP->dest_ip, (uint8 *)TCP);
      header.caplen = header.len = 14 + ntohs(IP->total_len);
      eth_packet_trace_ex (dev, ((u_char *)IP)-14, header.len, "reading TCP segment", 1, dev->dbit);
#if ETH_MIN_JUMBO_FRAME < ETH_MAX_PACKET
      if (1) {
        /* Debugging is easier if we read packets directly with pcap
           (i.e. we can use Wireshark to verify packet contents)
           we don't want to do this all the time for 2 reasons:
             1) sending through pcap involves kernel transitions and
             2) if the current system reflects sent packets, the 
                recieving side will receive and process 2 copies of 
                any packets sent this way. */
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
_eth_fix_ip_xsum_offload(ETH_DEV* dev, const u_char* msg, int len)
{
const unsigned short* proto = (const unsigned short*) &msg[12];
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
      return; /* UDP Checksums are disabled */
    orig_checksum = UDP->checksum;
    UDP->checksum = 0;
    UDP->checksum = pseudo_checksum(ntohs(UDP->length), IPPROTO_UDP, &IP->source_ip, &IP->dest_ip, (uint8 *)UDP);
    if (orig_checksum != UDP->checksum)
      eth_packet_trace (dev, msg, len, "reading UDP header Checksum Fixed");
    break;
  case IPPROTO_TCP:
    TCP = (struct TCPHeader *)(((char *)IP)+IP_HLEN(IP));
    orig_checksum = TCP->checksum;
    TCP->checksum = 0;
    TCP->checksum = pseudo_checksum(ntohs(IP->total_len)-IP_HLEN(IP), IPPROTO_TCP, &IP->source_ip, &IP->dest_ip, (uint8 *)TCP);
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

static int
_eth_process_loopback (ETH_DEV* dev, const u_char* data, uint32 len)
{
int protocol = data[12] | (data[13] << 8);
ETH_PACK  response;
uint32 offset, function;

if (protocol != 0x0090)     /* !ethernet loopback */
  return 0;

if (LOOPBACK_REFLECTION_TEST_PACKET(dev, data))
  return 0;                 /* Ignore reflection check packet */

offset   = 16 + (data[14] | (data[15] << 8));
if (offset >= len)
  return 0;
function = data[offset] | (data[offset+1] << 8);

if (function != 2) /*forward*/
  return 0;

/* The only packets we should be responding to are ones which 
   we received due to them being directed to our physical MAC address, 
   OR the Broadcast address OR to a Multicast address we're listening to 
   (we may receive others if we're in promiscuous mode, but shouldn't 
   respond to them) */
if ((0 == (data[0]&1)) &&           /* Multicast or Broadcast */
    (0 != memcmp(dev->filter_address[0], data, sizeof(ETH_MAC))))
  return 0;

/* Attempts to forward to multicast or broadcast addresses are explicitly 
   ignored by consuming the packet and doing nothing else */
if (data[offset+2]&1)
  return 1;

eth_packet_trace (dev, data, len, "rcvd");

sim_debug(dev->dbit, dev->dptr, "_eth_process_loopback()\n");

/* create forward response packet */
memset(&response, 0, sizeof(response));
response.len = len;
memcpy(response.msg, data, len);
memcpy(&response.msg[0], &response.msg[offset+2], sizeof(ETH_MAC));
memcpy(&response.msg[6], dev->filter_address[0], sizeof(ETH_MAC));
offset += 8 - 16; /* Account for the Ethernet Header and Offset value in this number  */
response.msg[14] = offset & 0xFF;
response.msg[15] = (offset >> 8) & 0xFF;

/* send response packet */
eth_write(dev, &response, NULL);

eth_packet_trace(dev, response.msg, response.len, "loopbackforward");

++dev->loopback_packets_processed;

return 1;
}

static void
_eth_callback(u_char* info, const struct pcap_pkthdr* header, const u_char* data)
{
ETH_DEV*  dev = (ETH_DEV*) info;
int to_me;
int from_me = 0;
int i;
int bpf_used;

if (LOOPBACK_PHYSICAL_RESPONSE(dev, data)) {
  u_char *datacopy = (u_char *)malloc(header->len);

  /* Since we changed the outgoing loopback packet to have the physical MAC address of the
     host's interface instead of the programmatically set physical address of this pseudo
     device, we restore parts of the modified packet back as needed */
  memcpy(datacopy, data, header->len);
  memcpy(datacopy, dev->physical_addr, sizeof(ETH_MAC));
  memcpy(datacopy+18, dev->physical_addr, sizeof(ETH_MAC));
  _eth_callback(info, header, datacopy);
  free(datacopy);
  return;
}
switch (dev->eth_api) {
  case ETH_API_PCAP:
#ifdef USE_BPF
    bpf_used = 1;
    to_me = 1;
    /* AUTODIN II hash mode? */
    if ((dev->hash_filter) && (data[0] & 0x01) && (!dev->promiscuous) && (!dev->all_multicast))
      to_me = _eth_hash_lookup(dev->hash, data);
    break;
#endif /* USE_BPF */
  case ETH_API_TAP:
  case ETH_API_VDE:
  case ETH_API_UDP:
  case ETH_API_NAT:
    bpf_used = 0;
    to_me = 0;
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
    break;
  default:
    bpf_used = to_me = 0;                           /* Should NEVER happen */
    abort();
    break;
  }

/* detect reception of loopback packet to our physical address */
if ((LOOPBACK_SELF_FRAME(dev->physical_addr, data)) ||
    (LOOPBACK_PHYSICAL_REFLECTION(dev, data))) {
#ifdef USE_READER_THREAD
  pthread_mutex_lock (&dev->self_lock);
#endif
  dev->loopback_self_rcvd_total++;
  /* lower reflection count - if already zero, pass it on */
  if (dev->loopback_self_sent > 0) {
    eth_packet_trace (dev, data, header->len, "ignored");
    dev->loopback_self_sent--;
    to_me = 0;
    }
  else
    if (!bpf_used)
      from_me = 0;
#ifdef USE_READER_THREAD
  pthread_mutex_unlock (&dev->self_lock);
#endif
  }

if (bpf_used ? to_me : (to_me && !from_me)) {
  if (header->len > ETH_MIN_JUMBO_FRAME) {
    if (header->len <= header->caplen) {/* Whole Frame captured? */
      u_char *datacopy = (u_char *)malloc(header->len);
      memcpy(datacopy, data, header->len);
      _eth_fix_ip_jumbo_offload(dev, datacopy, header->len);
      free(datacopy);
      }
    else
      ++dev->jumbo_truncated;
    return;
    }
  if (_eth_process_loopback(dev, data, header->len))
    return;  
#if defined (USE_READER_THREAD)
  if (1) {
    int crc_len = 0;
    uint8 crc_data[4];
    uint32 len = header->len;
    u_char *moved_data = NULL;

    if (header->len < ETH_MIN_PACKET) {   /* Pad runt packets before CRC append */
      moved_data = (u_char *)malloc(ETH_MIN_PACKET);
      memcpy(moved_data, data, len);
      memset(moved_data + len, 0, ETH_MIN_PACKET-len);
      len = ETH_MIN_PACKET;
      data = moved_data;
      }

    /* If necessary, fix IP header checksums for packets originated locally */
    /* but were presumed to be traversing a NIC which was going to handle that task */
    /* This must be done before any needed CRC calculation */
    _eth_fix_ip_xsum_offload(dev, (const u_char*)data, len);
    
    if (dev->need_crc)
      crc_len = eth_get_packet_crc32_data(data, len, crc_data);

    eth_packet_trace (dev, data, len, "rcvqd");

    pthread_mutex_lock (&dev->lock);
    ethq_insert_data(&dev->read_queue, ETH_ITM_NORMAL, data, 0, len, crc_len, crc_data, 0);
    ++dev->packets_received;
    pthread_mutex_unlock (&dev->lock);
    free(moved_data);
    }
#else /* !USE_READER_THREAD */
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

  ++dev->packets_received;

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

if ((!dev) || (dev->eth_api == ETH_API_NONE)) return 0;

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
  switch (dev->eth_api) {
#ifdef HAVE_PCAP_NETWORK
    case ETH_API_PCAP:
      status = pcap_dispatch((pcap_t*)dev->handle, 1, &_eth_callback, (u_char*)dev);
      break;
#endif
#ifdef HAVE_TAP_NETWORK
    case ETH_API_TAP:
      if (1) {
        struct pcap_pkthdr header;
        int len;
        u_char buf[ETH_MAX_JUMBO_FRAME];

        memset(&header, 0, sizeof(header));
        len = read(dev->fd_handle, buf, sizeof(buf));
        if (len > 0) {
          status = 1;
          header.caplen = header.len = len;
          _eth_callback((u_char *)dev, &header, buf);
          }
        else {
          if (len < 0)
            status = -1;
          else
            status = 0;
          }
        }
      break;
#endif /* HAVE_TAP_NETWORK */
#ifdef HAVE_VDE_NETWORK
    case ETH_API_VDE:
      if (1) {
        struct pcap_pkthdr header;
        int len;
        u_char buf[ETH_MAX_JUMBO_FRAME];

        memset(&header, 0, sizeof(header));
        len = vde_recv((VDECONN*)dev->handle, buf, sizeof(buf), 0);
        if (len > 0) {
          status = 1;
          header.caplen = header.len = len;
          _eth_callback((u_char *)dev, &header, buf);
          }
        else {
          if (len < 0)
            status = -1;
          else
            status = 0;
          }
        }
      break;
#endif /* HAVE_VDE_NETWORK */
    case ETH_API_UDP:
      if (1) {
        struct pcap_pkthdr header;
        int len;
        u_char buf[ETH_MAX_JUMBO_FRAME];

        memset(&header, 0, sizeof(header));
        len = (int)sim_read_sock (dev->fd_handle, (char *)buf, (int32)sizeof(buf));
        if (len > 0) {
          status = 1;
          header.caplen = header.len = len;
          _eth_callback((u_char *)dev, &header, buf);
          }
        else {
          if (len < 0)
            status = -1;
          else
            status = 0;
          }
        }
      break;
    }
  } while ((status > 0) && (0 == packet->len));
if (status < 0) {
  ++dev->receive_packet_errors;
  _eth_error (dev, "eth_reader");
  }

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

t_stat eth_bpf_filter (ETH_DEV* dev, int addr_count, ETH_MAC* const filter_address,
                       ETH_BOOL all_multicast, ETH_BOOL promiscuous, 
                       int reflections,
                       ETH_MAC* physical_addr,
                       ETH_MAC* host_nic_phy_hw_addr,
                       ETH_MULTIHASH* const hash,
                       char *buf)
{
int i;
char mac[20];
char *buf2;

/* setup BPF filters and other fields to minimize packet delivery */
strcpy(buf, "");

/* construct destination filters - since the real ethernet interface was set
   into promiscuous mode by eth_open(), we need to filter out the packets that
   our simulated interface doesn't want. */
if (!promiscuous) {
  for (i = 0; i < addr_count; i++) {
    eth_mac_fmt(&filter_address[i], mac);
    if (!strstr(buf, mac))    /* eliminate duplicates */
      sprintf(&buf[strlen(buf)], "%s(ether dst %s)", (*buf) ? " or " : "((", mac);
    }
  if (all_multicast || hash)
    sprintf(&buf[strlen(buf)], "%s(ether multicast)", (*buf) ? " or " : "((");
  if (strlen(buf) > 0)
    sprintf(&buf[strlen(buf)], ")");
  }

/* construct source filters - this prevents packets from being reflected back 
   by systems where WinPcap and libpcap cause packet reflections. Note that
   some systems do not reflect packets at all. This *assumes* that the 
   simulated NIC will not send out packets with multicast source fields. */
if ((addr_count > 0) && (reflections > 0)) {
  if (strlen(buf) > 0)
    sprintf(&buf[strlen(buf)], " and ");
  else
    if (promiscuous)
      sprintf(&buf[strlen(buf)], "(");
  sprintf (&buf[strlen(buf)], "not (");
  buf2 = &buf[strlen(buf)];
  for (i = 0; i < addr_count; i++) {
    if (filter_address[i][0] & 0x01) continue; /* skip multicast addresses */
    eth_mac_fmt(&filter_address[i], mac);
    if (!strstr(buf2, mac))   /* only process each address once */
      sprintf(&buf2[strlen(buf2)], "%s(ether src %s)", (*buf2) ? " or " : "", mac);
    }
  sprintf (&buf[strlen(buf)], ")");
  if (1 == strlen(buf2)) {          /* all addresses were multicast? */
    buf[strlen(buf)-6] = '\0';      /* Remove "not ()" */
    if (strlen(buf) > 0)
        buf[strlen(buf)-5] = '\0';  /* remove " and " */
    }
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
/* check for physical address in filters */
if ((!promiscuous) && (addr_count) && (reflections > 0)) {
  eth_mac_fmt(&physical_addr[0], mac);
  if (strcmp(mac, "00:00:00:00:00:00") != 0) {
    /* let packets through where dst and src are the same as our physical address */
    sprintf (&buf[strlen(buf)], " or ((ether dst %s) and (ether src %s))", mac, mac);
    if (host_nic_phy_hw_addr) {
      eth_mac_fmt(&host_nic_phy_hw_addr[0], mac);
      sprintf(&buf[strlen(buf)], " or ((ether dst %s) and (ether proto 0x9000))", mac);
      }
    }
  }
if ((0 == strlen(buf)) && (!promiscuous)) /* Empty filter means match nothing */
  strcpy(buf, "ether host fe:ff:ff:ff:ff:ff"); /* this should be a good match nothing filter */
sim_debug(dev->dbit, dev->dptr, "BPF string is: |%s|\n", buf);
return SCPE_OK;
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
char buf[116+66*ETH_FILTER_MAX];
char mac[20];
t_stat status;
#ifdef USE_BPF
struct bpf_program bpf;
#endif

/* make sure device exists */
if (!dev) return SCPE_UNATT;

/* filter count OK? */
if ((addr_count < 0) || (addr_count > ETH_FILTER_MAX))
  return SCPE_ARG;
else
  if (!addresses && (addr_count != 0)) 
     return SCPE_ARG;

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
  if (dev->all_multicast) {
    sim_debug(dev->dbit, dev->dptr, "All Multicast\n");
    }
  if (dev->promiscuous) {
    sim_debug(dev->dbit, dev->dptr, "Promiscuous\n");
    }
  }
#ifdef USE_READER_THREAD
  pthread_mutex_lock (&dev->self_lock);
#endif
/* Set the desired physical address */
memset(dev->physical_addr, 0, sizeof(ETH_MAC));
dev->loopback_self_sent = 0;
/* Find desired physical address in filters */
for (i = 0; i < addr_count; i++) {
  if (dev->filter_address[i][0]&1)
    continue;  /* skip all multicast addresses */
  eth_mac_fmt(&dev->filter_address[i], mac);
  if (strcmp(mac, "00:00:00:00:00:00") != 0) {
    memcpy(dev->physical_addr, &dev->filter_address[i], sizeof(ETH_MAC));
    break;
    }
  }
#ifdef USE_READER_THREAD
  pthread_mutex_unlock (&dev->self_lock);
#endif

/* setup BPF filters and other fields to minimize packet delivery */
eth_bpf_filter (dev, dev->addr_count, dev->filter_address, 
                dev->all_multicast, dev->promiscuous, 
                dev->reflections, &dev->physical_addr, 
                dev->have_host_nic_phy_addr ? &dev->host_nic_phy_hw_addr: NULL,
                (dev->hash_filter ? &dev->hash : NULL), buf);

/* get netmask, which is a required argument for compiling.  The value, 
   in our case isn't actually interesting since the filters we generate 
   aren't referencing IP fields, networks or values */

#ifdef USE_BPF
if (dev->eth_api == ETH_API_PCAP) {
  char errbuf[PCAP_ERRBUF_SIZE];
  bpf_u_int32  bpf_subnet, bpf_netmask;

  if (pcap_lookupnet(dev->name, &bpf_subnet, &bpf_netmask, errbuf)<0)
    bpf_netmask = 0;
  /* compile filter string */
  if ((status = pcap_compile((pcap_t*)dev->handle, &bpf, buf, 1, bpf_netmask)) < 0) {
    sprintf(errbuf, "%s", pcap_geterr((pcap_t*)dev->handle));
    sim_printf("Eth: pcap_compile error: %s\n", errbuf);
    /* show erroneous BPF string */
    sim_printf ("Eth: BPF string is: |%s|\n", buf);
    sim_printf ("Eth: Input to BPF string construction:\n");
    sim_printf ("Eth: Reflections: %d\n", dev->reflections);
    sim_printf ("Eth: Filter Set:\n");
    for (i = 0; i < addr_count; i++) {
      char mac[20];
      eth_mac_fmt(&dev->filter_address[i], mac);
      sim_printf ("Eth:   Addr[%d]: %s\n", i, mac);
      }
    if (dev->all_multicast)
      sim_printf ("Eth: All Multicast\n");
    if (dev->promiscuous)
      sim_printf ("Eth: Promiscuous\n");
    if (dev->hash_filter)
      sim_printf ("Eth: Multicast Hash: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
                  dev->hash[0], dev->hash[1], dev->hash[2], dev->hash[3], 
                  dev->hash[4], dev->hash[5], dev->hash[6], dev->hash[7]);
    if (dev->have_host_nic_phy_addr) {
      eth_mac_fmt(&dev->host_nic_phy_hw_addr, mac);
      sim_printf ("Eth: host_nic_phy_hw_addr: %s\n", mac);
      }
    }
  else {
    /* apply compiled filter string */
    if ((status = pcap_setfilter((pcap_t*)dev->handle, &bpf)) < 0) {
      sprintf(errbuf, "%s", pcap_geterr((pcap_t*)dev->handle));
      sim_printf("Eth: pcap_setfilter error: %s\n", errbuf);
      sim_printf ("Eth: BPF string is: |%s|\n", buf);
      }
    else {
      /* Save BPF filter string */
      dev->bpf_filter = (char *)realloc(dev->bpf_filter, 1 + strlen(buf));
      strcpy (dev->bpf_filter, buf);
#ifdef USE_SETNONBLOCK
      /* set file non-blocking */
      status = pcap_setnonblock (dev->handle, 1, errbuf);
#endif /* USE_SETNONBLOCK */
      }
    pcap_freecode(&bpf);
    }
#ifdef USE_READER_THREAD
  pthread_mutex_lock (&dev->lock);
  ethq_clear (&dev->read_queue); /* Empty FIFO Queue when filter list changes */
  pthread_mutex_unlock (&dev->lock);
#endif
  }
#endif /* USE_BPF */

return SCPE_OK;
}

void eth_show_dev (FILE *st, ETH_DEV* dev)
{
fprintf(st, "Ethernet Device:\n");
if (!dev) {
  fprintf(st, "-- Not Attached\n");
  return;
  }
fprintf(st, "  Name:                    %s\n", dev->name);
fprintf(st, "  Reflections:             %d\n", dev->reflections);
fprintf(st, "  Self Loopbacks Sent:     %d\n", dev->loopback_self_sent_total);
fprintf(st, "  Self Loopbacks Rcvd:     %d\n", dev->loopback_self_rcvd_total);
if (dev->have_host_nic_phy_addr) {
  char hw_mac[20];

  eth_mac_fmt(&dev->host_nic_phy_hw_addr, hw_mac);
  fprintf(st, "  Host NIC Address:        %s\n", hw_mac);
  }
if (dev->jumbo_dropped)
  fprintf(st, "  Jumbo Dropped:           %d\n", dev->jumbo_dropped);
if (dev->jumbo_fragmented)
  fprintf(st, "  Jumbo Fragmented:        %d\n", dev->jumbo_fragmented);
if (dev->jumbo_truncated)
  fprintf(st, "  Jumbo Truncated:         %d\n", dev->jumbo_truncated);
if (dev->packets_sent)
  fprintf(st, "  Packets Sent:            %d\n", dev->packets_sent);
if (dev->transmit_packet_errors)
  fprintf(st, "  Send Packet Errors:      %d\n", dev->transmit_packet_errors);
if (dev->packets_received)
  fprintf(st, "  Packets Received:        %d\n", dev->packets_received);
if (dev->receive_packet_errors)
  fprintf(st, "  Read Packet Errors:      %d\n", dev->receive_packet_errors);
if (dev->error_reopen_count)
  fprintf(st, "  Error ReOpen Count:      %d\n", dev->error_reopen_count);
if (dev->loopback_packets_processed)
  fprintf(st, "  Loopback Packets:        %d\n", dev->loopback_packets_processed);
#if defined(USE_READER_THREAD)
fprintf(st, "  Asynch Interrupts:       %s\n", dev->asynch_io?"Enabled":"Disabled");
if (dev->asynch_io)
  fprintf(st, "  Interrupt Latency:       %d uSec\n", dev->asynch_io_latency);
if (dev->throttle_count)
  fprintf(st, "  Throttle Delays:         %d\n", dev->throttle_count);
fprintf(st, "  Read Queue: Count:       %d\n", dev->read_queue.count);
fprintf(st, "  Read Queue: High:        %d\n", dev->read_queue.high);
fprintf(st, "  Read Queue: Loss:        %d\n", dev->read_queue.loss);
fprintf(st, "  Peak Write Queue Size:   %d\n", dev->write_queue_peak);
#endif
if (dev->bpf_filter)
  fprintf(st, "  BPF Filter: %s\n", dev->bpf_filter);
#if defined(HAVE_SLIRP_NETWORK)
if (dev->eth_api == ETH_API_NAT)
  sim_slirp_show ((SLIRP *)dev->handle, st);
#endif
}

static
t_stat eth_test_crc32 (DEVICE *dptr)
{
int errors = 0;
int val;
uint8 data[12];
static uint32 valcrc32[] = {
  0x7BD5C66F, 0x92C4D707, 0x7286E2FE, 0x9B97F396, 0x69738F4D, 0x80629E25, 0x6020ABDC, 0x8931BAB4,
  0x5E99542B, 0xB7884543, 0x57CA70BA, 0xBEDB61D2, 0x4C3F1D09, 0xA52E0C61, 0x456C3998, 0xAC7D28F0,
  0x314CE2E7, 0xD85DF38F, 0x381FC676, 0xD10ED71E, 0x23EAABC5, 0xCAFBBAAD, 0x2AB98F54, 0xC3A89E3C,
  0x140070A3, 0xFD1161CB, 0x1D535432, 0xF442455A, 0x06A63981, 0xEFB728E9, 0x0FF51D10, 0xE6E40C78,
  0xEEE78F7F, 0x07F69E17, 0xE7B4ABEE, 0x0EA5BA86, 0xFC41C65D, 0x1550D735, 0xF512E2CC, 0x1C03F3A4,
  0xCBAB1D3B, 0x22BA0C53, 0xC2F839AA, 0x2BE928C2, 0xD90D5419, 0x301C4571, 0xD05E7088, 0x394F61E0,
  0xA47EABF7, 0x4D6FBA9F, 0xAD2D8F66, 0x443C9E0E, 0xB6D8E2D5, 0x5FC9F3BD, 0xBF8BC644, 0x569AD72C,
  0x813239B3, 0x682328DB, 0x88611D22, 0x61700C4A, 0x93947091, 0x7A8561F9, 0x9AC75400, 0x73D64568,
  0x8AC0520E, 0x63D14366, 0x8393769F, 0x6A8267F7, 0x98661B2C, 0x71770A44, 0x91353FBD, 0x78242ED5,
  0xAF8CC04A, 0x469DD122, 0xA6DFE4DB, 0x4FCEF5B3, 0xBD2A8968, 0x543B9800, 0xB479ADF9, 0x5D68BC91,
  0xC0597686, 0x294867EE, 0xC90A5217, 0x201B437F, 0xD2FF3FA4, 0x3BEE2ECC, 0xDBAC1B35, 0x32BD0A5D,
  0xE515E4C2, 0x0C04F5AA, 0xEC46C053, 0x0557D13B, 0xF7B3ADE0, 0x1EA2BC88, 0xFEE08971, 0x17F19819,
  0x1FF21B1E, 0xF6E30A76, 0x16A13F8F, 0xFFB02EE7, 0x0D54523C, 0xE4454354, 0x040776AD, 0xED1667C5,
  0x3ABE895A, 0xD3AF9832, 0x33EDADCB, 0xDAFCBCA3, 0x2818C078, 0xC109D110, 0x214BE4E9, 0xC85AF581,
  0x556B3F96, 0xBC7A2EFE, 0x5C381B07, 0xB5290A6F, 0x47CD76B4, 0xAEDC67DC, 0x4E9E5225, 0xA78F434D,
  0x7027ADD2, 0x9936BCBA, 0x79748943, 0x9065982B, 0x6281E4F0, 0x8B90F598, 0x6BD2C061, 0x82C3D109,
  0x428FE8EC, 0xAB9EF984, 0x4BDCCC7D, 0xA2CDDD15, 0x5029A1CE, 0xB938B0A6, 0x597A855F, 0xB06B9437,
  0x67C37AA8, 0x8ED26BC0, 0x6E905E39, 0x87814F51, 0x7565338A, 0x9C7422E2, 0x7C36171B, 0x95270673,
  0x0816CC64, 0xE107DD0C, 0x0145E8F5, 0xE854F99D, 0x1AB08546, 0xF3A1942E, 0x13E3A1D7, 0xFAF2B0BF,
  0x2D5A5E20, 0xC44B4F48, 0x24097AB1, 0xCD186BD9, 0x3FFC1702, 0xD6ED066A, 0x36AF3393, 0xDFBE22FB,
  0xD7BDA1FC, 0x3EACB094, 0xDEEE856D, 0x37FF9405, 0xC51BE8DE, 0x2C0AF9B6, 0xCC48CC4F, 0x2559DD27,
  0xF2F133B8, 0x1BE022D0, 0xFBA21729, 0x12B30641, 0xE0577A9A, 0x09466BF2, 0xE9045E0B, 0x00154F63,
  0x9D248574, 0x7435941C, 0x9477A1E5, 0x7D66B08D, 0x8F82CC56, 0x6693DD3E, 0x86D1E8C7, 0x6FC0F9AF,
  0xB8681730, 0x51790658, 0xB13B33A1, 0x582A22C9, 0xAACE5E12, 0x43DF4F7A, 0xA39D7A83, 0x4A8C6BEB,
  0xB39A7C8D, 0x5A8B6DE5, 0xBAC9581C, 0x53D84974, 0xA13C35AF, 0x482D24C7, 0xA86F113E, 0x417E0056,
  0x96D6EEC9, 0x7FC7FFA1, 0x9F85CA58, 0x7694DB30, 0x8470A7EB, 0x6D61B683, 0x8D23837A, 0x64329212,
  0xF9035805, 0x1012496D, 0xF0507C94, 0x19416DFC, 0xEBA51127, 0x02B4004F, 0xE2F635B6, 0x0BE724DE,
  0xDC4FCA41, 0x355EDB29, 0xD51CEED0, 0x3C0DFFB8, 0xCEE98363, 0x27F8920B, 0xC7BAA7F2, 0x2EABB69A,
  0x26A8359D, 0xCFB924F5, 0x2FFB110C, 0xC6EA0064, 0x340E7CBF, 0xDD1F6DD7, 0x3D5D582E, 0xD44C4946,
  0x03E4A7D9, 0xEAF5B6B1, 0x0AB78348, 0xE3A69220, 0x1142EEFB, 0xF853FF93, 0x1811CA6A, 0xF100DB02,
  0x6C311115, 0x8520007D, 0x65623584, 0x8C7324EC, 0x7E975837, 0x9786495F, 0x77C47CA6, 0x9ED56DCE,
  0x497D8351, 0xA06C9239, 0x402EA7C0, 0xA93FB6A8, 0x5BDBCA73, 0xB2CADB1B, 0x5288EEE2, 0xBB99FF8A};

for (val=0; val <= 0xFF; val++) {
  memset (data, val, sizeof (data));
  if (valcrc32[val] != eth_crc32 (0, data, sizeof (data))) {
    printf("Unexpected CRC for %d byte buffer containing 0x%02X. Expected %08X, got %08X\n",
           (int)sizeof (data), val, valcrc32[val], eth_crc32 (0, data, sizeof (data)));
    ++errors;
    }
  }
return (errors == 0) ? SCPE_OK : SCPE_IERR;
}

static
t_stat eth_test_bpf (DEVICE *dptr)
{
int errors = 0;
#ifdef USE_BPF
t_stat r;
DEVICE eth_tst;
ETH_DEV dev;
int eth_num;
int eth_opened;
ETH_LIST  eth_list[ETH_MAX_DEVICE];
int eth_device_count;
int reflections, all_multicast, promiscuous;
char buf[116+66*ETH_FILTER_MAX];
char mac[20];
ETH_MAC filter_address[3] = {
    {0x04, 0x05, 0x06, 0x07, 0x08, 0x09},
    {0x09, 0x00, 0x2B, 0x02, 0x01, 0x07},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
  };
int addr_count;
ETH_MAC host_nic_phy_hw_addr = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
ETH_MAC *host_phy_addr_list[2] = {&host_nic_phy_hw_addr, NULL};
int host_phy_addr_listindex;
ETH_MULTIHASH hash = {0x01, 0x40, 0x00, 0x00, 0x48, 0x88, 0x40, 0x00};
ETH_MULTIHASH *hash_list[2] = {&hash, NULL};
int hash_listindex;
int bpf_count = 0;
int bpf_construct_error_count = 0;
int bpf_compile_error_count = 0;
int bpf_compile_skip_count = 0;
#define SIM_PRINT_BPF_ARGUMENTS                                 \
    if (1) {                                                    \
      sim_printf ("Eth: Input to BPF string construction:\n");  \
      sim_printf ("Eth: Reflections: %d\n", reflections);       \
      sim_printf ("Eth: Filter Set:\n");                        \
      for (i = 0; i < addr_count; i++) {                        \
        eth_mac_fmt(&filter_address[i], mac);                   \
        sim_printf ("Eth:   Addr[%d]: %s\n", i, mac);           \
        }                                                       \
      if (all_multicast)                                        \
        sim_printf ("Eth: All Multicast\n");                    \
      if (promiscuous)                                          \
        sim_printf ("Eth: Promiscuous\n");                      \
      if (hash_list[hash_listindex])                            \
        sim_printf ("Eth: Multicast Hash: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",\
                    (*hash_list[hash_listindex])[0], (*hash_list[hash_listindex])[1], (*hash_list[hash_listindex])[2], (*hash_list[hash_listindex])[3], \
                    (*hash_list[hash_listindex])[4], (*hash_list[hash_listindex])[5], (*hash_list[hash_listindex])[6], (*hash_list[hash_listindex])[7]);\
      if (host_phy_addr_list[host_phy_addr_listindex]) {        \
        eth_mac_fmt(host_phy_addr_list[host_phy_addr_listindex], mac);\
        sim_printf ("Eth: host_nic_phy_hw_addr: %s\n", mac);    \
        }                                                       \
      }


memset (&eth_tst, 0, sizeof(eth_tst));
eth_device_count = _eth_devices(ETH_MAX_DEVICE, eth_list);
eth_opened = 0;
for (eth_num=0; eth_num<eth_device_count; eth_num++) {
  char eth_name[32];

  if ((0 == memcmp (eth_list[eth_num].name, "nat:", 4)) ||
      (0 == memcmp (eth_list[eth_num].name, "tap:", 4)) ||
      (0 == memcmp (eth_list[eth_num].name, "vde:", 4)) ||
      (0 == memcmp (eth_list[eth_num].name, "udp:", 4)))
      continue;
  eth_name[sizeof (eth_name)-1] = '\0';
  snprintf (eth_name, sizeof (eth_name)-1, "eth%d", eth_num);
  r = eth_open(&dev, eth_name, &eth_tst, 1);
  if (r != SCPE_OK) {
    sim_printf ("%s: Eth: Error opening eth%d: %s\n", dptr->name, eth_num, sim_error_text (r));
    continue;
    }
  ++eth_opened;
  for (reflections=0; reflections<=1; reflections++) {
    for (all_multicast=0; all_multicast<=1; all_multicast++) {
      for (promiscuous=0; promiscuous<=1; promiscuous++) {
        for (addr_count=1; addr_count<=2; addr_count++) {
          for (hash_listindex=0; hash_listindex<=1; hash_listindex++) {
            for (host_phy_addr_listindex=0; host_phy_addr_listindex<=1; host_phy_addr_listindex++) {
              int i;
              char errbuf[PCAP_ERRBUF_SIZE];

              ++bpf_count;
              r = eth_bpf_filter (&dev, addr_count, &filter_address[0], 
                                  all_multicast, promiscuous, reflections, 
                                  &filter_address[0], 
                                  host_phy_addr_list[host_phy_addr_listindex],
                                  hash_list[hash_listindex],
                                  buf);
              if (r != SCPE_OK) {
                ++bpf_construct_error_count;
                sim_printf ("Eth: Error producing a BPF filter for:\n");
                SIM_PRINT_BPF_ARGUMENTS;
                }
              else {
                if (sim_switches & SWMASK('D')) {
                  SIM_PRINT_BPF_ARGUMENTS;
                  sim_printf ("Eth: BPF string is: |%s|\n", buf);
                  }
                if (dev.eth_api == ETH_API_PCAP) {
                  struct bpf_program bpf;

                  if (pcap_compile ((pcap_t*)dev.handle, &bpf, buf, 1, (bpf_u_int32)0) < 0) {
                    ++bpf_compile_error_count;
                    sprintf(errbuf, "%s", pcap_geterr((pcap_t*)dev.handle));
                    sim_printf("Eth: pcap_compile error: %s\n", errbuf);
                    if (!(sim_switches & SWMASK('D'))) {
                      /* show erroneous BPF string */
                      SIM_PRINT_BPF_ARGUMENTS;
                      sim_printf ("Eth: BPF string is: |%s|\n", buf);
                      }
                    }
                  pcap_freecode(&bpf);
                  }
                else
                  ++bpf_compile_skip_count;
                }
              }
            }
          }
        }
      }
    }
  eth_close(&dev);
  }

if (eth_opened == 0) {
  errors = 1;
  sim_printf ("%s: No testable LAN interfaces found\n", dptr->name);
  }
if (bpf_count)
  sim_printf ("BPF Filter Count:     %d\n", bpf_count);
if (bpf_construct_error_count)
  sim_printf ("BPF Construct Errors: %d\n", bpf_construct_error_count);
if (bpf_compile_error_count)
  sim_printf ("BPF Compile Errors:   %d\n", bpf_compile_error_count);
if (bpf_compile_skip_count)
  sim_printf ("BPF Compile Skipped:  %d\n", bpf_compile_skip_count);
#endif /* USE_BPF */
return (errors == 0) ? SCPE_OK : SCPE_IERR;
}

#include <setjmp.h>

t_stat sim_ether_test (DEVICE *dptr)
{
t_stat stat = SCPE_OK;
SIM_TEST_INIT;

sim_printf ("Testing %s device sim_ether APIs\n", dptr->name);

SIM_TEST(eth_test_crc32 (dptr));
SIM_TEST(eth_test_bpf (dptr));
return stat;
}
#endif /* USE_NETWORK */
