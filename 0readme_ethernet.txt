This file contains information about the XQ/SIM_ETHER package.

-------------------------------------------------------------------------------

The XQ emulator is a host-independant software emulation of Digital's
DELQA (M7516) and DEQNA (M7504) Q-bus ethernet cards for the SIMH emulator.

The XQ emulator uses the Sim_Ether module to execute host-specific ethernet
packet reads and writes, since all operating systems talk to real ethernet
cards/controllers differently. The host-dependant Sim_Ether module currently
supports Windows, Linux, NetBSD, and OpenBSD.

Currently, the Sim_Ether module sets the selected ethernet card into
promiscuous mode to gather all packets, then filters out the packets that it
doesn't want. In Windows, packets having the same source MAC address as the
controller are ignored for WinPCAP compatibility (see Windows notes below).

If your ethernet card is plugged into a switch, the promiscuous mode setting
should not cause much of a problem, since the switch will still filter out
most of the undesirable traffic. You will only see "excessive" traffic if you
are on a direct or hub(repeater) segment.

-------------------------------------------------------------------------------

Windows notes:
 1. The Windows-specific code uses the WinPCAP 3.0 package from
    http://winpcap.polito.it. This package for windows simulates the libpcap
    package that is freely available for unix systems.
 2. You must *install* the WinPCAP runtime package.
 3. The first time the WinPCAP driver is used, it will be dynamically loaded,
    and the user must be an Administrator on the machine to do so. If you need
    to run as an unprivileged user, you must set the service to autostart. See
    the WinPCAP documentation for details on the static load workaround.
 4. WinPCAP loops packet writes back into the read queue. This causes problems
    since the XQ controller is not expecting to read it's own packet. A fix
    to the packet read filter was added to reject packets from the current MAC,
    but this defeats DECNET's duplicate node number detection scheme. A more
    correct fix for WinPCAP will be explored as time allows.

Building on Windows:
 1. Install WinPCAP 3.0.
 2. Put the required .h files (bittypes,devioctl,ip6_misc,packet32,pcap,
    pcap-stdinc).h from the WinPCAP 3.0 developer's kit in the compiler's path
 3. Put the required .lib files (packet,wpcap).lib from the WinPCAP 3.0
    developer's kit in the linker's path
 4. If you're using Borland C++, use COFF2OMF to convert the .lib files into
    a format that can be used by the compiler.
 5. Define USE_NETWORK if you want the network functionality.
 6. Build it!

-------------------------------------------------------------------------------

Linux, NetBSD, and OpenBSD notes:
 1. You must run SIMH(scp) as root so that the ethernet card can be set into
    promiscuous mode by the driver. Alternative methods for avoiding the
    'run as root' requirement will be welcomed.

Building on Linux, NetBSD, and OpenBSD:
 1. Get/install the libpcap package for your unix version. http://rpmfind.net
    might be a useful site for finding the linux variants.
 2. Use Make USE_NETWORK=1 if you want the network functionality.
 3. Build it!

-------------------------------------------------------------------------------

VAX simulator support:

An OpenVMS VAX v7.2 system with DECNET Phase IV, MultiNet 4.4a, and LAT 5.3 has
been successfully run. Other testers have reported success booting NetBSD also.


PDP11 simulator support:

An RT-11 v5.3 system with a freeware TCP/IP stack has been successfully run.
Other testers have reported that RSX with DECNET and the NetBSD operating
systems also work. RSTS/E v10.1 has preliminary support - RSTS/E boots and
enables the XH (XQ) device - DECNET and LAT software have not been tested.
 
-------------------------------------------------------------------------------

Things planned for future releases:
 1. PDP-11 bootstrap/bootrom
 2. Full MOP implementation
 3. DESQA support (if someone can get me the user manuals)
 4. DETQA support [DELQA-Turbo] (I have the manual)
 5. DEUNA/DELUA support

-------------------------------------------------------------------------------

Things which I need help with:
 1. Porting Sim_Ether packet driver to other host platforms, especially VMS.
 2. Information about Remote MOP processing
 3. VAX/PDP-11 hardware diagnotics image files and docs, to test XQ thoroughly.
 4. Feedback on operation with other VAX/PDP-11 OS's.

-------------------------------------------------------------------------------

Please send all patches, questions, feedback, clarifications, and help to:
  dhittner AT northropgrumman DOT com

Thanks, and Enjoy!!
Dave


===============================================================================
                               Change Log
===============================================================================

05-Jun-03 Release:
 1. Added SET/SHOW XQ STATS
 2. Added SHOW XQ FILTERS
 3. Added ability to split received packet into multiple buffers
 4. Added explicit runt & giant packet processing

-------------------------------------------------------------------------------

30-May-03 Release:
 1. Corrected bug in xq_setmac introduced in v3.0 (multiple people)
 2. Made XQ receive buffer allocation dynamic to reduce scp executable size
 3. Optimized some structs, removed legacy variables (Mark Pizzolato)
 4. Changed #ifdef WIN32 to _WIN32 for consistancy

-------------------------------------------------------------------------------

06-May-03 Release:
 1. Added second XQ controller
 2. Added SIMH v3.0 compatibility
 3. Removed SET ADDRESS functionality

-------------------------------------------------------------------------------

10-Apr-03 Release:
 1. Added preliminary support for RSTS/E
 2. Added PDP-11 bootrom load via CSR flags
 3. Support for SPARC linux (thanks to Mark Pizzolato)

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
