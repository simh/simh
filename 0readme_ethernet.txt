This file contains information about the XQ/SIM_ETHER package.

-------------------------------------------------------------------------------

16-Jan-03 Release:
 1. Added VMScluster support (thanks to Mark Pizzolato)
 2. Verified VAX remote boot functionality (>>>B XQA0)
 3. Added major performance enhancements (thanks to Mark Pizzolato again)
 4. Changed _DEBUG tracers to XQ_DEBUG and ETH_DEBUG 
 5. Added local packet processing
 6. Added system id broadcast

-------------------------------------------------------------------------------

GOLD Changes: (08-Nov-02)
 1. Added USE_NETWORK conditional to Sim_Ether
 2. Fixed behaviour of SHOW XQ ETH if no devices exist
 3. Added OpenBSD support to Sim_Ether (courtesy of Federico Schwindt)
 4. Added ethX detection simplification (from Megan Gentry)

-------------------------------------------------------------------------------

BETA 5 Changes: (23-Oct-02)
 1. Added all_multicast and promiscuous mode support
 2. Finished DEQNA emulation
 3. Verified LAT functionality
 4. Added NXM (Non-eXistant Memory) protection (suggested by Robert Supnik)
 5. Added NetBSD support to Sim_Ether (courtesy of Jason Thorpe)
 6. Fixed write buffer overflow bug (discovered by Jason Thorpe)
 7. Fixed unattached device behavior (discovered by Patrick Caulfield)
 8. Extensive rewrite of this README
 9. Debugged sanity timer

-------------------------------------------------------------------------------

BETA 4 Changes: (16-Oct-02)
 1. Added stub support for all_multicast and promiscuous modes
 2. Integrated with SIMH v2.10-0b1
 3. Added VAX network bootstrap support
 4. Added SET/SHOW XQ TYPE and SANITY commands
 5. Added stub support for DEQNA mode

-------------------------------------------------------------------------------

BETA 3 Changes: (10-Oct-02)
 1. Fixed off-by-one bug in setup address processing
 2. Added rejection of multicast addresses to SET XQ MAC
 3. Added linux support to Sim_Ether (courtesy of Patrick Caulfield)

-------------------------------------------------------------------------------

BETA 2 Changes: (08-Oct-02)
 1. Integrated with SIMH v2.10-0p4
 2. Added floating vectors; this also fixes pdp11 emulation problem
 3. Cleaned up codebase; 100% of packet driver code moved to Sim_Ether
 4. Verified TCP/IP functionality
 5. Added Copyrights

-------------------------------------------------------------------------------

BETA 1 Changes: (03-Oct-02)
 1. Moved most of packet driver functionality from XQ to Sim_Ether
 2. Verified DECNET functionality
 3. Added SET/SHOW MAC command
 4. Added SHOW ETH command

-------------------------------------------------------------------------------

The XQ emulator is a host-independant software emulation of Digital's
DELQA (M7516) and DEQNA (M7504) Q-bus ethernet cards for the SIMH emulator.

See the last section of this document for XQ usage instructions.

The XQ emulator uses the Sim_Ether module to execute host-specific ethernet
packet reads and writes, since all operating systems talk to real ethernet
cards/controllers differently. The host-dependant Sim_Ether module currently
supports Windows, Linux, and NetBSD.

Currently, the Sim_Ether module sets the selected ethernet card into
promiscuous mode to gather all packets, then filters out the packets that it
doesn't want. In Windows, Packets having the same source MAC address as the
controller are ignored for WinPCAP compatibility (see Windows notes below).

If your ethernet card is plugged into a switch, the promiscuous mode setting
should not cause much of a problem, since the switch will still filter out
most of the undesirable traffic. You will only see "excessive" traffic if you
are on a direct or hub(repeater) segment.

-------------------------------------------------------------------------------

Windows notes:
 1. The Windows-specific code uses the WinPCAP 3.0 (or 2.3) package from
    http://winpcap.polito.it. This package for windows simulates the libpcap
    package that is freely available for unix systems.
 2. You must *install* WinPCAP.
 3. Note that WinPCAP DOES NOT support dual CPU environments.
 4. WinPCAP loops packet writes back into the read queue. This causes problems
    since the XQ controller is not expecting to read it's own packet. A fix
    to the packet read filter was added to reject packets from the current MAC,
    but this defeats DECNET's duplicate node number detection scheme. A more
    correct fix for WinPCAP will be explored as time allows.
 5. The first time the WinPCAP driver is used, it will be dynamically loaded,
    and the user must be an Administrator on the machine to do so. If you need
    to run as an unprivileged user, you must set the service to autostart. See
    the WinPCAP documentation for details on the static load workaround.

Building on Windows:
 1. Install WinPCAP 3.0.
 2. Get the required .h files (bittypes.h, devioctl.h, ip6_misc.h, packet32.h,
    pcap.h, pcap-stdinc.h) from the WinPCAP 3.0 developer's kit
 3. Get the required .lib files (packet.lib, wpcap.lib) from the WinPCAP 3.0
    developer's kit. If you're using Borland C++, use COFF2OMF to convert
    the .lib files into a format that can be used by the compiler.
 4. Define USE_NETWORK if you want the network functionality.
 5. Build it!

-------------------------------------------------------------------------------

Linux, NetBSD, and OpenBSD notes:

 1. You must run SIMH(scp) as root so that the ethernet card can be set into
    promiscuous mode by the driver. Alternative suggestions will be welcomed.

Building on Linux, NetBSD, and OpenBSD:
 1. Define USE_NETWORK if you want the network functionality.

-------------------------------------------------------------------------------

THE XQ/SIM_ETHER modules have been successfully tested on a Windows 2000 host,
emulating an OpenVMS 7.2 VAX system with DECNET Phase IV, MultiNet 4.4a, and
LAT 5.3.

Regression test criteria:
 1. VAX shows device correctly                                      (passed)
 2. VMS boots successfully with new device emulation                (passed)
 3. VMS initializes device correctly                                (passed)
 4. DECNET loads successfully                                       (passed)
 5. DECNET line stays up                                            (passed)
 6. SET HOST x.y:: works from SIMH to real DECNET machine           (passed)
 7. SET HOST x.y:: works from real DECNET machine to SIMH           (passed)
 8. DECNET copy works from SIMH to real DECNET machine              (passed)
 9. DECNET copy works from real DECNET machine to SIMH              (passed)
10. MultiNet TCP/IP loads successfully                              (passed)
11. Multinet TCP/IP initializes device successfully                 (passed)
12. SET HOST/TELNET x.y.z.w works from SIMH to real VAX IP machine  (passed)
13. SET HOST/TELNET x.y.z.w works from real VAX IP machine to SIMH  (passed)
14. FTP GET from a real VAX IP machine                              (passed)
15. LAT loads sucessfully                                           (passed)
16. SET HOST/LAT <nodename> works from SIMH to real VAX LAT machine (passed)
17. SET HOST/LAT <nodename> works from real VAX LAT machine to SIMH (passed)
18. SIMH node joins VMSCluster                                      (passed)
19. SIMH node mounts other VMSCluster disks                         (passed)
20. SIMH node MSCP serves disks to other nodes                      (passed)
21. SIMH node remote boots into VMScluster (>>>B XQAO)              (passed)

The following are known to NOT work:
 1. PDP-11 using RSTS/E v10.1 (fails to enable device - needs bootrom support)

I have reports that the following work:
 1. PDP-11 using RSX
 2. VAX remote booting of NetBSD (via >>> B XQA0)
 
-------------------------------------------------------------------------------

Things planned for future releases:
 1. Full MOP implementation
 2. PDP-11 bootstrap/bootrom
 3. DESQA support (if someone can get me the user manuals)
 4. DETQA support [DELQA-Turbo] (I have the manual)

-------------------------------------------------------------------------------

Things which I need help with:
 1. Porting Sim_Ether packet driver to other platforms, especially VMS.
 2. Information about Remote MOP processing
 3. PDP-11 bootstrap code.
 4. VAX hardware diagnotics image file and docs, to test XQ thoroughly.
 5. Feedback on operation with other VAX OS's.
 6. Feedback on operation with PDP-11 OS's.

-------------------------------------------------------------------------------

Please send all patches, questions, feedback, clarifications, and help to:
  dhittner AT northropgrumman DOT com

Thanks, and Enjoy!!
Dave
