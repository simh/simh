This file contains information about the SIMH Ethernet package.

-------------------------------------------------------------------------------

The XQ emulator is a host-independent software emulation of Digital's
DELQA-T (M7516-YM), DELQA (M7516) and DEQNA (M7504) Q-bus Ethernet cards 
for the SIMH emulator.

The XU emulator is a host-independent software emulation of Digital's DEUNA
(M7792/M7793) and DELUA (M7521) Unibus Ethernet cards for the SIMH emulator.

The XQ and XU simulators use the Sim_Ether module to execute host-specific
packet reads and writes, since all operating systems talk to real Ethernet
cards/controllers differently. See the comments at the top of sim_ether.c
for the list of currently supported host platforms.

The Sim_Ether module sets the selected Ethernet card into
promiscuous mode to gather all packets, then filters out the packets that it
doesn't want. In Windows, packets having the same source MAC address as the
controller are ignored for WinPCAP compatibility (see Windows notes below).

If your Ethernet card is plugged into a switch, the promiscuous mode setting
should not cause much of a problem, since the switch will still filter out
most of the undesirable traffic. You will only see "excessive" traffic if you
are on a direct or hub(repeater) segment.

On Windows using the WinPcap interface, the simulated computer can "talk" to
the host computer on the same interface.  On other platforms with libpcap 
(*nix), the simulated computer can not "talk" to the host computer via the 
selected interface, since simulator transmitted packets are not received
by the host's network stack. The workaround for this is to use a second NIC 
in the host and connect them both into the same network; then the host and 
the simulator can communicate over the physical LAN.

Integrated Universal TUN/TAP support provides another solution for the above 
dual-NIC problem for systems that support Universal TUN/TAP. Since the TUN/TAP
interface is a pseudo network interface, the host can create a TAP device for 
the simulator and then bridge or route packets between the TAP device and the 
real network interface. Note that the TAP device and any bridging or routing 
must be established before running the simulator; SIMH does not create, 
bridge, or route TAP devices for you.

Integrated Universal TUN/TAP support can be used for host<->simulator network
traffic (on the platforms where it is available) by using the SIMH command:
"attach xq tap:tapN" (i.e. attach xq tap:tap0).  Platforms that this has been
tested on include: Linux, FreeBSD, OpenBSD, NetBSD and OSX.  Each of these 
platforms has some way to create a tap pseudo device (and possibly then to 
bridge it with a physical network interface).

The following steps were performed to get a working SIMH vax simulator 
sharing a physical NIC and allowing Host<->SIMH vax communications:

Linux (Ubuntu 10.04):
    apt-get install make
    apt-get install libpcap-dev
    apt-get install bridge-utils
    apt-get install uml-utilities


    #!/bin/sh
    HOSTIP=`/sbin/ifconfig eth0 | grep "inet addr" | gawk -- '{ print $2 }' | gawk -F : -- '{ print $2 }'`
    HOSTNETMASK=`/sbin/ifconfig eth0 | grep "inet addr" | gawk -- '{ print $4 }' | gawk -F : -- '{ print $2 }'`
    HOSTBCASTADDR=`/sbin/ifconfig eth0 | grep "inet addr" | gawk -- '{ print $3 }' | gawk -F : -- '{ print $2 }'`
    HOSTDEFAULTGATEWAY=`/sbin/route -n | grep ^0.0.0.0 | gawk -- '{ print $2 }'`
    #
    /usr/sbin/tunctl -t tap0 [-u someuser]
    /sbin/ifconfig tap0 up
    #
    # Now convert eth0 to a bridge and bridge it with the TAP interface
    /usr/sbin/brctl addbr br0
    /usr/sbin/brctl addif br0 eth0
    /usr/sbin/brctl setfd br0 0
    /sbin/ifconfig eth0 0.0.0.0
    /sbin/ifconfig br0 $HOSTIP netmask $HOSTNETMASK broadcast $HOSTBCASTADDR up
    # set the default route to the br0 interface
    /sbin/route add -net 0.0.0.0/0 gw $HOSTDEFAULTGATEWAY
    # bridge in the tap device
    /usr/sbin/brctl addif br0 tap0
    /sbin/ifconfig tap0 0.0.0.0

    # Run simulator and "attach xq tap:tap0"
    
OpenBSD (OpenBSD 4.6)

    /sbin/ifconfig tun0 create
    /sbin/ifconfig tun0 link0
    /sbin/ifconfig tun0 up

    /sbin/ifconfig bridge0 create
    /sbin/brconfig bridge0 fwddelay 4
    /sbin/brconfig bridge0 add em0 add tun0  # Change em0 to reflect your physical NIC name
    /sbin/brconfig bridge0 up

    # Run simulator and "attach xq tap:tun0"
    
FreeBSD (FreeBSD 8.0)

    /sbin/ifconfig tap0 create
    /sbin/ifconfig tap0 up

    /sbin/ifconfig bridge0 create
    /sbin/ifconfig bridge0 addm em0 addm tap0 # Change em0 to reflect your physical NIC name
    /sbin/ifconfig bridge0 up

    # Run simulator and "attach xq tap:tap0"
    # Note: it seems that on FreeBSD you may have to 
    #       "/sbin/ifconfig tap0 up" and "/sbin/ifconfig bridge0 up" prior to each 
    #       time simh "attach"es the tap:tap0 device

NetBSD (NetBSD 5.0.2)

    /sbin/ifconfig tap0 create
    /sbin/ifconfig tap0 up

    /sbin/ifconfig bridge0 create
    /sbin/brconfig bridge0 fwddelay 1
    /sbin/brconfig bridge0 add wm0 add tap0  # Change wm0 to reflect your physical NIC name
    /sbin/brconfig bridge0 up

    # Run simulator and "attach xq tap:tap0"

OSX (Snow Leopard)
    OSX Does NOT have native support for tun/tap interfaces.  It also does not have native
    support for bridging.
    
    Mattias Nissler has created tun/tap functionality available at http://tuntaposx,sourceforge.net/
    
    We'll punt on bridging for the sake of this example and move on to use a basic tap
    based internal network so a host and guest can communicate directly.
    
    Download the install package from: 
    http://sourceforge.net/projects/tuntaposx/files/tuntap/20111101/tuntap_20111101.tar.gz
    
    Expand the tarball to a directory.
    Invoke the package installer tuntap_20111101.pkg
    Click through the various prompts accepting things and eventually installing the package.
    
    # Build and Run simulator and:
       sim> attach xq tap:tap0
       sim> ! ifconfig tap0 192.168.6.1 netmask 255.255.255.0

    Simulated system uses IP address 192.168.6.2 and host uses 192.168.6.1 
    and things work.
    You must run as root for this to work.
    
-------------------------------------------------------------------------------
An alternative to direct pcap and tun/tap networking on *nix environments is 
VDE (Virtual Distributed Ethernet).

Note 1: Using vde based networking is likely more flexible, but it isn't 
        nearly as efficient.  Host OS overhead will always be higher when 
        vde networking is used as compared to native pcap and/or tun/tap 
        networking.
Note 2: Root access will likely be needed to configure or start the vde 
        environment prior to starting a simulator which may use it.
Note 3: Simulators running using VDE networking can run without root 
        privilege.

Linux (Ubuntu 10.04):
    apt-get install make
    apt-get install libvdeplug-dev
    apt-get install vde2

    vde_switch -s /tmp/switch1 -tap tap0 -m 666
    ifconfig tap0 192.168.6.1 netmask 255.255.255.0 up
    
    # Build and Run simulator and:
       sim> attach xq vde:/tmp/switch1  #simulator uses IP address 192.168.6.2

-------------------------------------------------------------------------------

Windows notes:
 1. The Windows-specific code uses the WinPCAP 4.x package from
    http://www.winpcap.org. This package for windows simulates the libpcap
    package that is freely available for un*x systems.

 2. You must *install* the WinPCAP runtime package.

 3. The first time the WinPCAP driver is used, it will be dynamically loaded,
    and the user must be an Administrator on the machine to do so. If you need
    to run as an unprivileged user, you must set the "npf" driver to autostart. 
    Current WinPcap installers provide an option to configure this at 
    installation time, so if that choice is made, then there is no need for
    administrator privileged to run simulators with network support.


Building on Windows:
 You should be able to build with any of the free compiler environments 
 available on the Windows platform.  If you want to use the Visual C++ 
 Express 2008 or 2010 interactive development environments, read the file 
 ".\Visual Studio Projects\0ReadMe_Projects.txt" for details about the
 required dependencies.  Alternatively, you can build simh with networking
 support using the MinGW GCC compiler environment or the cygwin environment.
 Each of these Visual C++, MinGW and cygwin build environments require 
 WinPcap and Posix packages being available.  These should be located in a 
 directory structure parallel to the current simulator source directory.
 
 For Example, the directory structure should look like:

    .../simh/simhv38-2-rc1/VAX/vax_cpu.c
    .../simh/simhv38-2-rc1/scp.c
    .../simh/simhv38-2-rc1/Visual Studio Projects/simh.sln
    .../simh/simhv38-2-rc1/Visual Studio Projects/VAX.vcproj
    .../simh/simhv38-2-rc1/BIN/Nt/Win32-Release/vax.exe
    .../simh/windows-build/pthreads/pthread.h
    .../simh/windows-build/winpcap/WpdPack/Include/pcap.h

 The contents of the windows-build directory can be downloaded from:

    https://github.com/downloads/markpizz/simh/windows-build.zip


 There are Windows batch files provided to initiate compiles using the MinGW
 compiler tool chain.  These batch files are located in the same directory 
 as this file and are called: build_mingw.bat, build_mingw_ether.bat, and 
 build_mingw_noasync.bat.  These batch files each presume that the MinGW 
 toolchain is either in the current path or, if not that it is located at
 C:\MinGW\bin.  These batch files merely invoke the MinGW make (GNU make)
 passing some specific arguments along with the optional arguments the batch 
 file is invoked with.
 
 The current windows network built binaries will run on any system without 
 regard to whether or not WinPcap is installed, and will provide 
 Network functionality when WinPcap is available.

-------------------------------------------------------------------------------

Linux, {Free|Net|Open}BSD, OS/X, and Un*x notes:

----- WARNING ----- WARNING ----- WARNING ----- WARNING ----- WARNING -----

Sim_Ether has been reworked to be more universal; because of this, you will
need to get a version of libpcap that is 0.9 or greater. All current Linux
distributions provide a libpcap-dev package which has the needed version
of libpcap and the required components to build applications using it.  
If you are running an older Linux OS, you can download and build the required 
library from www.tcpdump.org - see the comments at the top of Sim_ether.c 
for details.  

----- WARNING ----- WARNING ----- WARNING ----- WARNING ----- WARNING -----

 1. For all platforms, you must run SIMH(scp) with sufficient privilege to
    allow the Ethernet card can be set into promiscuous mode and to write
    packets through the driver. 
      a) For Windows systems this means having administrator privileges to 
         start the "npf" driver.  The current WinPcap installer offers an 
         option to autostart the "npf" driver when the system boots. 
         Starting the "npf" driver at boot time means that simulators do
         not need to run with administrator privileges.
      b) For more recent Linux systems, The concepts leveraging "Filesystem
         Capabilities" can be used to specifically grant the simh binary
         the needed privileges to access the network.  The article at:
         http://packetlife.net/blog/2010/mar/19/sniffing-wireshark-non-root-user/
         describes how to do this for wireshark.  The exact same capabilities
         are needed by SIMH for network support.  Use that article as a guide.
      c) For Unix/Unix-like systems which use bpf devices (NetBSD, 
         OpenBSD, FreeBSD and OS/X) it is possible to set permissions on 
         the bpf devices to allow read and write access to users other 
         than root (For example: chmod 666 /dev/bpf*).  Doing this, has 
         its own security issues.
      d) For other platforms this will likely mean running as root.
    Additional alternative methods for avoiding the 'run as root' requirement 
    will be welcomed.

 2. If you want to use TAP devices, and any surrounding system network/bridge
    setup must be done before running SIMH.  However, once that is done 
    (possibly at system boot time), using the TAP devices can be done without
    root privileges.

Building on Linux, {Free|Net|Open}BSD, OS/X, Solaris, other *nix:

 1. Get/make/install the libpcap-dev package for your operating system. Sources:
      All    : http://www.tcpdump.org/
      Older versions of libpcap can be found, for various systems, at:
      Linux  : search for your variant on http://rpmfind.net
      OS/X   : Apple Developer's site?

	    NOTE: The repositories for older versions of these platforms
	          don't contain a version of libpcap greater than 0.8.1.
	          However, most(all) recent releases of *nix environments 
	          ship with sufficiently recent versions of libpcap either 
	          automatically installed or available for installation as 
	          part of the distribution.  
	          The OS provided libpcap-dev components will be prefereable 
	          to a package built from www.tcpdump.org sources.  This is
	          due to the fact that various OS supplied packages will 
	          depend on the OS supplied libpcap.  The improper build or 
	          install of the www.tcpdump.org source package can conflict 
	          with the OS provided one and break the OS provided 
	          applications (i.e. tcpdump and/or wireshark) as well as
	          not working correctly for use by simh.

 2. If you install the vendor supplied libpcap-dev package then the simh
    makefile will automatically use the vendor supplied library without any 
    additional arguments.  If you have downloaded and built libpcap from 
    www.tcpdump.org, then the existing makefile will detect that this is
    the case and try to use that.

 3. The makefile defaults to building simulators with network support which
    dynamically load the libpcap library.  This means that the same simulator
    binaries will run on any system whether or not libpcap is installed.  If 
    you want to force direct libpcap linking during a build you do so by 
    typing 'make USE_NETWORK=1'

 4. Build it!

-------------------------------------------------------------------------------

OpenVMS Alpha and OpenVMS Integrety (IA64) notes:
  1. Ethernet support will only work on Alpha VMS 7.3-1 or later, which is
     when required VCI promiscuous mode support was added. Hobbyists can
     get the required version of VMS from the OpenVMS Alpha Hobbyist Kit 3.0.

     Running a simulator built with Ethernet support on a version of VMS prior
     to 7.3-1 will behave as if there is no Ethernet support built in due to
     the inability of the software to set the PCAPVCM into promiscuous mode.

     An example display of fully functional Ethernet support:
       sim> SHOW XQ ETH
       ETH devices:
         0  we0 (VMS Device: _EWA0:)
         1  we1 (VMS Device: _EWB0:)

     An example display when the simulator was built without Ethernet support
     or is not running the required version of VMS:
       sim> SHOW XQ ETH
       ETH devices:
         no network devices are available

  2. You must place the PCAPVCM.EXE execlet in SYS$LOADABLE_IMAGES before
     running a simulator with Ethernet support.  Note: This is done by the
     build commands in descrip.mms.

  3. You must have CMKRNL privilege to SHOW or ATTACH an Ethernet device;
     alternatively, you can INSTALL the simulator with CMKRNL privilege.

  4. If you use a second adapter to communicate to the host, SOME protocol
     that creates an I/O structure (SCS, DECNET, TCP) must be running on the
     adapter prior trying to connect with SIMH, or the host may crash.
     The execlet is not written to create an I/O structure for the device.

Building on OpenVMS Alpha and OpenVMS Integrety (IA64):
  The current descrip.mms file will build simulators capable of using
  Ethernet support with them automatically.  These currently are: VAX, 
  VAX780, and PDP11.  The descrip.mms driven builds will also build the
  pcap library and build and install the VCI execlet.
  
  1. Fetch the VMS-PCAP zip file from:  
	    http://simh.trailing-edge.com/sources/vms-pcap.zip
  2. Unzip it into the base of the SIMH distribution directory.
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

How to debug problems with the Ethernet subsystems:

PLEASE read the host-specific notes in sim_ether.c!

While running SCP, the following commands can be used to enable debug messages:

  sim> SET DEBUG STDERR
  sim> SET XQ DEBUG=TRACE;CSR;VAR;WARN;SETUP;SANITY;REG;PACKET;DATA;ETH
  sim> SET XU DEBUG=ETH;TRACE;REG;WARN;PACKET;DATA

Documentation of the functionality of these debug modifiers can be found in
pdp11_xq.h and pdp11_xu.h. Inline debugging has replaced the previous #ifdef
style of debugging, which required recompilation before debugging.

-------------------------------------------------------------------------------

Things planned for future releases:
 1. Full MOP implementation

-------------------------------------------------------------------------------

Things which I need help with:
 1. Information about Remote MOP processing
 2. VAX/PDP-11 hardware diagnostics image files and docs, to test XQ thoroughly.
 3. Feedback on operation with other VAX/PDP-11 OS's.

-------------------------------------------------------------------------------

Please send all patches, questions, feedback, clarifications, and help to:
  david DOT hittner AT ngc DOT com

Thanks, and Enjoy!!
Dave


===============================================================================
                               Change Log
===============================================================================
  
  01-Mar-12  AGN  Added support for building using Cygwin on Windows
  01-Mar-12  MP   Made host NIC address detection more robust on *nix platforms 
                  and mad it work when compiling under Cygwin
  29-Feb-12  MP   Fixed MAC Address Conflict detection support
  28-Feb-12  MP   Fixed overrun bug in eth_devices which caused SEGFAULTs
  28-Feb-12  MP   Fixed internal loopback processing to only respond to loopback
                  packets addressed to the physical MAC or appropriate Multicast
                  or Broadcast addresses.
  17-Nov-11  MP   Added dynamic loading of libpcap on *nix platforms
  30-Oct-11  MP   Added support for vde (Virtual Distributed Ethernet) networking
  29-Oct-11  MP   Added support for integrated Tap networking interfaces on OSX
  17-Aug-11  RMS  Fix from Sergey Oboguev relating to XU and XQ Auto Config and 
                  vector assignments
  12-Aug-11  MP   Cleaned up payload length determination
                  Fixed race condition detecting reflections when threaded 
                  reading and writing is enabled
  07-Jul-11  MB   VMS Pcap (from Mike Burke)
                     - Fixed Alpha issues
                     - Added OpenVMS Integrety support
  20-Apr-11  MP   Fixed save/restore behavior
  12-Jan-11  DTH  Added SHOW XU FILTERS modifier
  11-Jan-11  DTH  Corrected DEUNA/DELUA SELFTEST command, enabling use by
                  VMS 3.7, VMS 4.7, and Ultrix 1.1
  09-Jan-11  MP   Fixed missing crc data when USE_READER_THREAD is defined and 
                  crc's are needed (only the pdp11_xu)
  16-Dec-10  MP   added priority boost for read and write threads when 
                  USE_READER_THREAD does I/O in separate threads.  This helps
                  throughput since it allows these I/O bound threads to preempt 
                  the main thread (which is executing simulated instructions).                  
  09-Dec-10  MP   allowed more flexible parsing of MAC address strings
  09-Dec-10  MP   Added support to determine if network address conflicts exist
  07-Dec-10  MP   Reworked DECnet self detection to the more general approach
                  of loopback self when any Physical Address is being set.
  06-Dec-10  MP   Added loopback processing support to pdp11_xu.c
  06-Dec-10  MP   Fixed loopback processing to correctly handle forward packets.
  04-Dec-10  MP   Changed eth_write to do nonblocking writes when 
                  USE_READER_THREAD is defined.
  30-Nov-10  MP   Fixed the fact that no broadcast packets were received by the DEUNA
  29-Nov-10  MP   Fixed interrupt dispatch issue which caused delivered packets 
                  (in and out) to sometimes not interrupt the CPU after processing.
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
                  Send Offload capabilities to fragment and/or segment outgoing 
                  network traffic.  Additionally a simulated network device can 
                  reasonably exist on a LAN which is configured to use Jumbo frames.
  21-May-10  MP   Added functionality to fix up IP header checksums to accommodate 
                  packets from a host with a NIC which has TOE (TCP Offload Engine)
                  enabled which is expected to implement the checksum computations
                  in hardware.  Since we catch packets before they arrive at the
                  NIC the expected checksum insertions haven't been performed yet.
                  This processing is only done for packets sent from the host to 
                  the guest we're supporting.  In general this will be a relatively 
                  small number of packets so it is done for all IP frame packets
                  coming from the host to the guest.  In order to make the 
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
                  is responsible for interfacing to the physical layer 
                  devices, AND it belongs here to get CRC processing right.
  15-Aug-08  MP   Fixed transmitted packets to have the correct source MAC address.
                  Fixed incorrect address filter setting calling eth_filter().
  07-Mar-08  MP   Fixed the SCP visible SA registers to always display the 
                  ROM MAC address, even after it is changed by SET XQ MAC=.
  07-Mar-08  MP   Added changes so that the Console DELQA diagnostic (>>>TEST 82) 
                  will succeed.
  03-Mar-08  MP   Added DELQA-T (aka DELQA Plus) device emulation support.
  06-Feb-08  MP   Added dropped frame statistics to record when the receiver discards
                  received packets due to the receiver being disabled, or due to the
                  XQ device's packet receive queue being full.
                  Fixed bug in receive processing when we're not polling.  This could
                  cause receive processing to never be activated again if we don't 
                  read all available packets via eth_read each time we get the 
                  opportunity.
  31-Jan-08  MP   Added the ability to Coalesce received packet interrupts.  This
                  is enabled by SET XQ POLL=DELAY=nnn where nnn is a number of 
                  microseconds to delay the triggering of an interrupt when a packet
                  is received.
  29-Jan-08  MP   Added SET XQ POLL=DISABLE (aka SET XQ POLL=0) to operate without 
                  polling for packet read completion.
  29-Jan-08  MP   Changed the sanity and id timer mechanisms to use a separate timer
                  unit so that transmit and receive activities can be dealt with
                  by the normal xq_svc routine.
                  Dynamically determine the timer polling rate based on the 
                  calibrated tmr_poll and clk_tps values of the simulator.
  25-Jan-08  MP   Enabled the SET XQ POLL to be meaningful if the simulator currently
                  doesn't support idling.
  25-Jan-08  MP   Changed xq_debug_setup to use sim_debug instead of printf so that
                  all debug output goes to the same place.
  25-Jan-08  MP   Restored the call to xq_svc after all successful calls to eth_write
                  to allow receive processing to happen before the next event
                  service time.  This must have been inadvertently commented out 
                  while other things were being tested.
  23-Jan-08  MP   Added debugging support to display packet headers and packet data
  18-Jun-07  RMS  Added UNIT_IDLE flag
  29-Oct-06  RMS  Synced poll and clock
  27-Jan-06  RMS  Fixed unaligned accesses in XQB (found by Doug Carman)
  07-Jan-06  RMS  Fixed unaligned access bugs (found by Doug Carman)
  07-Sep-05  DTH  Removed unused variable
  16-Aug-05  RMS  Fixed C++ declaration and cast problems

  05-Mar-08  MP   Added optional multicast filtering support for doing
                  LANCE style AUTODIN II based hashed filtering.
  07-Feb-08  MP   Added eth_show_dev to display Ethernet state
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
                  Fixed up #ifdef's relating to USE_SHARED so that setting 
                  USE_SHARED or USE_NETWORK will build a working network 
                  environment.
  23-Jan-08  MP   Reworked eth_packet_trace and eth_packet_trace_ex to allow
                  only output Ethernet header+crc and provide a mechanism for
                  the simulated device to display full packet data debugging.
  17-May-07  DTH  Fixed non-Ethernet device removal loop (from Naoki Hamada)
  15-May-07  DTH  Added dynamic loading of wpcap.dll;
                  Corrected exceed max index bug in ethX lookup
  04-May-07  DTH  Corrected failure to look up Ethernet device names in
                  the registry on Windows XP x64
  10-Jul-06  RMS  Fixed linux conditionalization (from Chaskiel Grundman)
  02-Jun-06  JDB  Fixed compiler warning for incompatible sscanf parameter
  15-Dec-05  DTH  Patched eth_host_devices [remove non-Ethernet devices]
                  (from Mark Pizzolato and Galen Tackett, 08-Jun-05)
                  Patched eth_open [tun fix](from Antal Ritter, 06-Oct-05)
  30-Nov-05  DTH  Added option to regenerate CRC on received packets; some
                  Ethernet devices need to pass it on to the simulation, and by
                  the time libpcap/winpcap gets the packet, the host OS network
                  layer has already stripped CRC out of the packet
  01-Dec-04  DTH  Added Windows user-defined adapter names (from Timothe Litt)



19-Mar-04 Release:
 1. Genericized Sim_Ether code, reduced #ifdefs                (David Hittner)
 2. Further refinement of sim_ether, qualified more platforms (Mark Pizzolato)
 3. Added XU module                                            (David Hittner)
 4. Corrected XQ interrupt signaling for PDP11s               (David Hittner)
 5. Added inline debugging support                             (David Hittner)

-------------------------------------------------------------------------------

26-Nov-03 Release:
 1. Added VMS support to Sim_Ether; created pcap-vms port      (Anders Ahgren)
 2. Added DECNET duplicate detection for Windows              (Mark Pizzolato)
 3. Added BPF filtering to increase efficiency                (Mark Pizzolato)
 4. Corrected XQ Runt processing                              (Mark Pizzolato)
 5. Corrected XQ Software Reset                               (Mark Pizzolato)
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
 4. Changed #ifdef WIN32 to _WIN32 for consistency            (Mark Pizzolato)

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
 2. Fixed behavior of SHOW XQ ETH if no devices exist
 3. Added OpenBSD support to Sim_Ether (courtesy of Federico Schwindt)
 4. Added ethX detection simplification (from Megan Gentry)

===============================================================================
