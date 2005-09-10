This file contains information about the SIMH Ethernet package.

-------------------------------------------------------------------------------

The XQ emulator is a host-independant software emulation of Digital's
DELQA (M7516) and DEQNA (M7504) Q-bus ethernet cards for the SIMH emulator.

The XU emulator is a host-independant software emulation of Digital's DEUNA
(M7792/M7793) and DELUA (M7521) Unibus ethernet cards for the SIMH emulator.

The XQ and XU simulators use the Sim_Ether module to execute host-specific
packet reads and writes, since all operating systems talk to real ethernet
cards/controllers differently. See the comments at the top of sim_ether.c
for the list of currently supported host platforms.

The Sim_Ether module sets the selected ethernet card into
promiscuous mode to gather all packets, then filters out the packets that it
doesn't want. In Windows, packets having the same source MAC address as the
controller are ignored for WinPCAP compatibility (see Windows notes below).

If your ethernet card is plugged into a switch, the promiscuous mode setting
should not cause much of a problem, since the switch will still filter out
most of the undesirable traffic. You will only see "excessive" traffic if you
are on a direct or hub(repeater) segment.

Using the libpcap/WinPcap interface, the simulated computer cannot "talk" to
the host computer via the selected interface, since the packets are not
reflected back to the host. The workaround for this is to use a second NIC in
the host and connect them both into the same network; then the host and the
simulator can communicate over the physical LAN.

Universal TUN/TAP support provides another solution for the above dual-NIC
problem for systems that support Universal TUN/TAP. Since the TUN/TAP interface
is at a different network level, the host can create a TAP device for the
simulator and then bridge or route packets between the TAP device and the real
network interface. Note that the TAP device and any bridging or routing must be
established before running the simulator; SIMH does not create, bridge, or
route TAP devices for you.

-------------------------------------------------------------------------------

Windows notes:
 1. The Windows-specific code uses the WinPCAP 3.0 package from
    http://www.winpcap.org. This package for windows simulates the libpcap
    package that is freely available for un*x systems.

 2. You must *install* the WinPCAP runtime package.

 3. The first time the WinPCAP driver is used, it will be dynamically loaded,
    and the user must be an Administrator on the machine to do so. If you need
    to run as an unprivileged user, you must set the service to autostart. See
    the WinPCAP documentation for details on the static loading workaround.

 4. If you want to use TAP devices, they must be created before running SIMH.
    (TAP component from the OpenVPN project; http://openvpn.sourceforge.net)

 5. Compaq PATHWORKS 32 v7.2 also enabled bridging for the ethernet adapters
    when the DECNET and LAT drivers were installed; TAP was not needed.


Building on Windows:
 1. Install WinPCAP 3.0 runtime and the WinPCAP Developer's kit.

 2. Put the required .h files (bittypes,devioctl,ip6_misc,packet32,pcap,
    pcap-stdinc).h from the WinPCAP 3.0 developer's kit in the compiler's path

 3. Put the required .lib files (packet,wpcap).lib from the WinPCAP 3.0
    developer's kit in the linker's path

 4. If you're using Borland C++, use COFF2OMF to convert the .lib files into
    a format that can be used by the compiler.

 5. Define USE_NETWORK.

 6. Build it!

-------------------------------------------------------------------------------

Linux, {Free|Net|Open}BSD, OS/X, and Un*x notes:

----- WARNING ----- WARNING ----- WARNING ----- WARNING ----- WARNING -----

Sim_Ether has been reworked to be more universal; because of this, you will
need to get a version of libpcap that is 0.9 or greater. This can be
downloaded from www.tcpdump.org - see the comments at the top of Sim_ether.c
for details.

At the time of this release, the "Current Version" available at:
http://www.tcpdump.org/daily/libpcap-current.tar.gz is the 
latest checked-in source code that is actually higher than the released
0.8.3 version number. Specifically, for all platforms, it contains code that 
opens the ethernet device in Read/Write mode instead of the Read-Only mode 
that previous libpcap versions for platforms which use one of pcap-bpf.c, 
pcap-pf.c, or pcap-snit.c.  This capabiligy now exists to support a newly 
provided generic packet sending capability.

----- WARNING ----- WARNING ----- WARNING ----- WARNING ----- WARNING -----

 1. For all platforms, you must run SIMH(scp) with sufficient privilege to
    allow the ethernet card can be set into promiscuous mode and to write
    packets through the driver. For most Unix/Unix-like platforms this will 
    mean running as root.  For systems which use bpf devices (NetBSD, 
    OpenBSD, FreeBSD and OS/X) it is possible to set permissions on the bpf 
    devices to allow read and write access to users other than root (For
    example: chmod 666 /dev/bpf*).  Doing this, has its own security issues.
    Additional alternative methods for avoiding the 'run as root' requirement 
    will be welcomed.

 2. If you want to use TAP devices, they must be created before running SIMH.

Building on Linux, {Free|Net|Open}BSD, OS/X, Un*x:

 1. Get/make/install the libpcap package for your operating system. Sources:
      All    : http://www.tcpdump.org/
      Older versions of libpcap can be found, for various systems, at:
      Linux  : search for your variant on http://rpmfind.net
      OS/X   : Apple Developer's site?

	    NOTE: These repositories will not likely contain a version
	          of libpcap greater than 0.8.1 for several years since 
	          other packages in these repositories don't depend on a 
	          later version than they currently have.

 2. Use 'make USE_NETWORK=1'

 3. Build it!

-------------------------------------------------------------------------------

OpenVMS Alpha notes:
  1. Ethernet support will only work on Alpha VMS 7.3-1 or later, which is
     when required VCI promiscuous mode support was added. Hobbyists can
     get the required version of VMS from the OpenVMS Alpha Hobbyist Kit 3.0.

     Running a simulator built with ethernet support on a version of VMS prior
     to 7.3-1 will behave as if there is no ethernet support built in due to
     the inability of the software to set the PCAPVCM into promiscuous mode.

     An example display of fully functional ethernet support:
       sim> SHOW XQ ETH
       ETH devices:
         0  we0 (VMS Device: _EWA0:)
         1  we1 (VMS Device: _EWB0:)

     An example display when the simulator was built without ethernet support
     or is not running the required version of VMS:
       sim> SHOW XQ ETH
       ETH devices:
         no network devices are available

  2. You must place the PCAPVCM.EXE execlet in SYS$LOADABLE_IMAGES before
     running a simulator with ethernet support.  Note: This is done by the
     build commands in descrip.mms.

  3. You must have CMKRNL privilege to SHOW or ATTACH an ethernet device;
     alternatively, you can INSTALL the simulator with CMKRNL privilege.

  4. If you use a second adapter to communicate to the host, SOME protocol
     that creates an I/O structure (SCS, DECNET, TCP) must be running on the
     adapter prior trying to connect with SIMH, or the host may crash.
     The execlet is not written to create an I/O structure for the device.

Building on OpenVMS Alpha:
  The current descrip.mms file will build simulators capable of using
  ethernet support with them automatically.  These currently are: VAX, 
  PDP11, and PDP10.  The descrip.mms driven builds will also build the
  pcap library and build and install the VCI execlet.
  
  1. Fetch the VMS-PCAP zip file from:  
	    http://simh.trailing-edge.com/sources/vms-pcap.zip
  2. Unzip it into the base of the simh distribution directory.
  3. Build the simulator(s) with MMS or MMK:
         $ MMx {VAX,PDP11,PDP10, etc...}

-------------------------------------------------------------------------------

VAX simulator support:

An OpenVMS VAX v7.2 system with DECNET Phase IV, MultiNet 4.4a, and LAT 5.3 has
been successfully run. Other testers have reported success booting NetBSD and
OpenVMS VAX 5.5-2 also.


PDP11 simulator support:

An RT-11 v5.3 system with a freeware TCP/IP stack has been successfully run.
Other testers have reported that RSX with DECNET and the NetBSD operating
systems also work. RSTS/E v10.1 has preliminary support - RSTS/E boots and
enables the XH (XQ) device - DECNET and LAT software have not been tested.

The XU module has been tested by a third party for basic packet functionality 
under a modified RSX11M environment. I am unable to test it in-house until
someone can arrange to send me a disk image containing a stock RSTS/E or
RSX11M+ system image that also contains DECNET, LAT, and/or TCP/IP software.

-------------------------------------------------------------------------------

How to debug problems with the ethernet subsystems:

PLEASE read the host-specific notes in sim_ether.c!

While running SCP, the following commands can be used to enable debug messages:

  scp> SET DEBUG STDERR
  scp> SET XQ DEBUG={ETH|TRC|REG|WRN|CSR|VAR|SAN|SET|PCK}
  scp> SET XU DEBUG={ETH|TRC|REG|WRN}

Documentation of the functionality of these debug modifiers can be found in
pdp11_xq.h and pdp11_xu.h. Inline debugging has replaced the previous #ifdef
style of debugging, which required recompilation before debugging.

-------------------------------------------------------------------------------

Things planned for future releases:
 1. PDP-11 bootstrap/bootrom
 2. Full MOP implementation
 3. DESQA support (if someone can get me the user manuals)
 4. DETQA support [DELQA-Turbo] (I have the manual)

-------------------------------------------------------------------------------

Things which I need help with:
 1. Information about Remote MOP processing
 2. VAX/PDP-11 hardware diagnotics image files and docs, to test XQ thoroughly.
 3. Feedback on operation with other VAX/PDP-11 OS's.

-------------------------------------------------------------------------------

Please send all patches, questions, feedback, clarifications, and help to:
  david DOT hittner AT ngc DOT com

Thanks, and Enjoy!!
Dave


===============================================================================
                               Change Log
===============================================================================

19-Mar-04 Release:
 1. Genericized Sim_Ether code, reduced #ifdefs                (David Hittner)
 2. Further refinement of sim_ether, qualified more platforms (Mark Pizzolato)
 3. Added XU module                                            (David Hittner)
 4. Corrected XQ interrupt signalling for PDP11s               (David Hittner)
 5. Added inline debugging support                             (David Hittner)

-------------------------------------------------------------------------------

26-Nov-03 Release:
 1. Added VMS support to Sim_Ether; created pcap-vms port      (Anders Ahgren)
 2. Added DECNET duplicate detection for Windows              (Mark Pizzolato)
 3. Added BPF filtering to increase efficiency                (Mark Pizzolato)
 4. Corrected XQ Runt processing                              (Mark Pizzolato)
 5. Corrected XQ Sofware Reset                               (Mark Pizzolato)
 6. Corrected XQ Multicast/Promiscuous mode setting/resetting (Mark Pizzolato)
 7. Added Universal TUN/TAP support                           (Mark Pizzolato)
 8. Added FreeBSD support                                  (Edward Brocklesby)
 9. Corrected interrupts on XQB device                         (David Hittner)

-------------------------------------------------------------------------------

05-Jun-03 Release:
 1. Added SET/SHOW XQ STATS                                    (David Hittner)
 2. Added SHOW XQ FILTERS                                      (David Hittner)
 3. Added ability to split rcv packets into multiple buffers   (David Hittner)
 4. Added explicit runt & giant packet processing              (David Hittner)

-------------------------------------------------------------------------------

30-May-03 Release:
 1. Corrected bug in xq_setmac introduced in v3.0            (multiple people)
 2. Made XQ rcv buffer allocation dynamic to reduce scp size   (David Hittner)
 3. Optimized some structs, removed legacy variables          (Mark Pizzolato)
 4. Changed #ifdef WIN32 to _WIN32 for consistancy            (Mark Pizzolato)

-------------------------------------------------------------------------------

06-May-03 Release:
 1. Added second XQ controller                                 (David Hittner)
 2. Added SIMH v3.0 compatibility                              (David Hittner)
 3. Removed SET ADDRESS functionality                          (David Hittner)

-------------------------------------------------------------------------------

10-Apr-03 Release:
 1. Added preliminary support for RSTS/E                       (David Hittner)
 2. Added PDP-11 bootrom load via CSR flags                    (David Hittner)
 3. Support for SPARC linux                                   (Mark Pizzolato)

-------------------------------------------------------------------------------

11-Mar-03 Release:
 1. Added support for RT-11 TCP/IP
 2. Corrected interrupts (thanks to Tom Evans and Bob Supnik)
 3. Moved change log to the bottom of the readme file, cleaned up document

-------------------------------------------------------------------------------

16-Jan-03 Release:
 1. Added VMScluster support (thanks to Mark Pizzolato)
 2. Verified VAX remote boot functionality (>>>B XQA0)
 3. Added major performance enhancements (thanks to Mark Pizzolato again)
 4. Changed _DEBUG tracers to XQ_DEBUG and ETH_DEBUG 
 5. Added local packet processing
 6. Added system id broadcast

-------------------------------------------------------------------------------

08-Nov-02 Release:
 1. Added USE_NETWORK conditional to Sim_Ether
 2. Fixed behaviour of SHOW XQ ETH if no devices exist
 3. Added OpenBSD support to Sim_Ether (courtesy of Federico Schwindt)
 4. Added ethX detection simplification (from Megan Gentry)

===============================================================================
