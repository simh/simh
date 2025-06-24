# SIMH v4.0 - 19-01 Current

[![Coverity Scan Build Status](https://scan.coverity.com/projects/11982/badge.svg)](https://scan.coverity.com/projects/simh)
[![AppVeyor CI Build Status](https://ci.appveyor.com/api/projects/status/github/simh/simh)](https://ci.appveyor.com/project/simh/simh/history)

## Table of Contents:
[WHAT'S NEW since the Open SIMH fork](#whats-new-since-the-open-simh-fork)  
[WHAT'S NEW since simh v3.9](#whats-new-since-simh-v39)  
. . [New Simulators](#new-simulators)  
. . [Simulator Front Panel API](#simulator-front-panel-api)  
. . [New Functionality](#new-functionality)  
. . . . [DDCMP Synchronous host physical device support - framer](#ddcmp-synchronous-host-physical-device-support---framer)  
. . . . [Remote Console Facility](#remote-console-facility)  
. . . . [VAX/PDP11 Enhancements](#vaxpdp11-enhancements)  
. . . . [PDP11 Specific Enhancements](#pdp11-specific-enhancements)  
. . . . [PDP10 Enhancements](#pdp10-enhancements)  
. . . . [SDS 940 Enhancements](#sds-940-enhancements)  
. . . . [Terminal Multiplexer additions](#terminal-multiplexer-additions)  
. . . . [Video Display Capabilities](#video-display-capabilities)  
. . . . [Asynchronous I/O](#asynchronous-io)  
. . . . [Clock/Timer Enhancements](#clocktimer-enhancements)  
. . . . [Ethernet Transport Enhancements](#ethernet-transport-enhancements)  
. . . . [Disk Extensions](#disk-extensions)  
. . . . [Embedded ROM support](#embedded-rom-support)  
. . . . [Control Flow](#control-flow)  
. . . . [Scriptable interactions with running simulators](#scriptable-interactions-with-running-simulators)  
. . . . [Help](#help)  
. . . . [Generic SCP support Clock Coscheduling as opposed to per simulator implementations](#generic-scp-support-clock-coscheduling-as-opposed-to-per-simulator-implementations)  
. . . . [New SCP Commands](#new-scp-commands)  
. . . . [Command Processing Enhancements](#command-processing-enhancements)  
. . . . . . [Environment variable insertion](#environment-variable-insertion)  
. . . . . . [Command aliases](#command-aliases)  
. . . . . . [Do command argument manipulation](#do-command-argument-manipulation)  
. . [Building and running a simulator](#building-and-running-a-simulator)  
. . . . [Use Prebuilt Windows Simulators](#use-prebuilt-windows-simulators)  
. . . . [Building simulators yourself](#building-simulators-yourself)  
. . . . . . [Linux/OSX other *nix platforms](#linuxosx-other-nix-platforms)  
. . . . . . . . [Build Dependencies](#build-dependencies)  
. . . . . . . . . . [OS X - Dependencies](#os-x---dependencies)  
. . . . . . . . . . [Linux - Dependencies](#linux---dependencies)  
. . . . . . [Windows](#windows)  
. . . . . . . . [Required related files](#required-related-files)  
. . . . . . . . [Visual Studio (Standard or Express) 2008, 2010, 2012, 2013 or Visual Studio Community 2015, 2017, 2019](#visual-studio-standard-or-express-2008-2010-2012-2013-or-visual-studio-community-2015-2017-2019)  
. . . . . . . . [MinGW32](#mingw32)  
. . . . . . [VMS](#vms)  
. . [Problem Reports](#problem-reports)  

## WHAT'S NEW since the Open SIMH fork

All Simulator updates on Open SIMH will be present in this repository, and any changes to the master branch code in this repository authored by anyone except Mark Pizzolato may be posted as pull requests on the Open simh repo.

Simulator binaries for x86 Linus, x86 macOS, and Windows for all recent changes are available at https://github.com/simh/Development-Binaries

### Mark Pizzolato's changes only present in the simh/simh repo and not present in the Open SIMH repo:

#### Visible changes to SCP (the simulator framework or command execution environment)

- Add descriptive messages for cases when NOPARAM status is returned.
- Avoid excessive DO command context lines when commands produce multiple lines of output.
- Support has been added to allow for optional per device unit tests to exist and to invoke them at simulator startup.
- Add support for generic bit field packing and unpacking during buffer copying.
- Display count of units when all units are disabled.
- Support to display all SCP visible filenames via relative paths and use those in SAVEd state.
- ZAP command can be aborted by a Control-C.
- Display current NOAUTOSIZE status in SHOW VERSION output.
- Extend logical name support to include optional unique names for units as well as devices.
- Add extended register sanity checks including duplicate name detection.  Fixed simulator devices with duplicate register names.
- Simulators with video devices that may be enabled, no longer disable the screen saver until the video display is presented.  Optionally enabling or disabling the OS screen saver by an environment variable.
- More readable output of SHOW <dev>|<unit> with variable sized DEVICE and UNIT names.
- Automatic Cryllic Font detection in BESM6 simulator at runtime rather than build time.  More relevant for distribution binaries.
- Built-in command history and tab file name completion previously done by GPL readline now done by BSD licensed library available on all platforms (especially Windows).
- Robust register sanity checking for all register definition macros.
- When building on windows, the windows-build dependency libraries are automatically downloaded even if git is not available.
- Extended video component version information displayed in SHOW VERSION output.
- Add a global SET AUTOZAP command or per drive SET <unit> AUTOZAP which removes metadata from disk containers at detach time if the container has metadata.
- DISKINFO command displays disk container metadata (if present) and container size along with detected file system information if a known file system type is present.
- makefile builds which have potentially useful dependencies not found will prompt to install these components prior to building.  MacOS Brew dependencies can be directly done from within the makefile.  Other platforms (or package management systems) which require root access to install will display the appropriate package management commands and and exit.  Support for macOS (HomeBrew and MacPorts), Linux (Ubuntu/Debian, RedHat/Fedora, Chimera), NetBSD, FreeBSD, OpenBSD.
- SHOW VERSION show the host system type that build the runing simulator when it is not the same as the current host system.
- Support for building simulators without built-in boot or ROM code when building with DONT_USE_INTERNAL_ROM is defined, but to automatically and transparently fetch the needed ROM or other boot code when it is needed.  This is possibly useful for systems which don't want to distribute simulators with build-in binary code which may have unknown copyright status.
- Reasonable output produced for all simulators from HELP BOOT.
- Fix occasional hang of IBM1130 simulator while building with Visual Studio.
- Building with the simh makefile can optionally compile each source file separately and store the compiled result.   This approach lends itself to quicker building for folks who are developing new simulators or new simulator modules.  This was requested and discussed in #697.  Invoking make with BUILD_SEPARATE=1 on the make command line or as an exported environment variable will achieve separate compiles.  Invoking make with QUIET=1 on the make command line or as an exported environment variable will summary output of activities being performed instead of full compiler commands.
- TAPE and SCSI libraries have been extended to fully support partial record reads of fixed sized records which may contain multiple records in recorded data.  Images of this type are common for QIC tape archives generally available on bitsavers and elsewhere.  Attach time checking on simulated QIC tape devices reports possible problems that may occur.
- Appveyor CI/CD builds of all simulators for Linux, macOS and Windows platforms.
- All the available simulator defined environment variables are documented in the help and sim_doc document file.
- SET CONSOLE TELNET=CONNECT will start a telnet session to the simulator console in a separate window.
- Support for building on systems with the gameoftrees.org (got) source control system.
- Frontpanel API improvements, document clarifications and bug fixes.
- Added a SET CLOCK NOCALIBRATE mode.  
    NOCALIBRATE mode allows all activity of a simulator run to occur with precisely consistent event timing.  In this mode, every clock tick takes precisely the same number of instructions/cycles.  Likewise, the polling activities for MUX or other poll oriented devices occurs after precisely the same number of instructions/cycles executed.  As a consequence of this mode, no effort to align simulated clock ticks (and simulated access to wall clock time) is made.
    
    This mode will often be useful for running diagnostics which expect a particular relationship between perceived wall clock and instruction times.  It might also be useful for running test scripts which may want to compare output of previous executions to to current execution or to compare execution on arbitrarily different host computers.  It will also be useful when running under a host system debugger which might produce confusing results when various wall clock pause times when stopping at breakpoints.  Additionally, consistent logical timing exists when simulator debug or instruction history is being recorded which normally would impact wall clock times.
    
    In NOCALIBRATE mode, the operator gets to specify the pseudo execution rate along with the base wall clock time that access to pseudo wall clock accesses returns.
- All removable devices get detached on a media unload without regard to data access format (SIMH, VHD or RAW).  
- Various failing bugs in tape detach logic are fixed.
- Clean building on Android Termux.
- Proper line editing and tab filename completion behavior on all Unix and Windows platforms.
- Simulators with video displays have a working SCREENSHOT command.
- VHD Support for Differencing Disks has been corrected.
- Attach time disk container copy support between dissimilar storage formats (VHD<->SIMH).  Previously container copy operations were only supported between identical format containers (SIMH<->SIMH, and VHD<->VHD).
- DISKINFO command provides more useful metadata information and file system verification with full support for VHD Differencing Disks.
- Simulators which have instruction history support can now have SHOW HISTORY output be aborted by SIGINT (Control-C).
- Device REGister variables have been extended to support double precision data.
- More complete host system information displayed in SHOW VERSION output.
- Simulator THROTTLING can only be enabled once per simulator run.  This avoids potential errant behaviors when arbitrarily switching throttling settings.
- EXAMINE memory commands will now produce minimal output that summarizes multiple successive locations with the same contents and may be aborted by SIGINT (Control-C).
- Ethernet enhancements on all platforms and and much simplified platform support on macOS.
- Almost all existing simulators have useful SET CONSOLE SPEED=nnn behaviors in both the bare console session as well as TELNET console sessions.

#### All simulators build cleanly under OpenVMS on ia64 systems.

#### Changes to the PDP-11 and VAX simulators also not in the Open SIMH repo

- All VAXen: Correct HELP CPU to properly describe model specific LOAD options for ROM and NVRAM.
- Add 2.11 BSD and NetBSD file system recognizers.
- Add memory details and behavior description to the MicroVAX 3900 HELP CPU output.
- Unibus and Qbus autoconfiguration disabling has been relaxed somewhat.  Previously, any "SET <device> ADDRESS= (or VECTOR=)" command would automatically disable autoconfigure for the rest of the simulator session.  This behavior has been relaxed so that autoconfigure will only be disabled if the specified ADDRESS or VECTOR value is different from the value previously set by the initial autoconfigure.
- Aggressive validation of Unibus and Qbus ADDRESS and VECTOR values prior to execution starting due to a BOOT command.
- Fixed bug in devices that use sim_disk which deallocated a file transfer buffer on detach.
- Metadata is implemented on all VAX and PDP11 disk devices when NOAUTOSIZE is not specified.
- Full support for using disk containers with metadata between different system and device types where it makes sense.
- VHD disk formats are available on all disk types (including floppy or DECtape devices).
- Properly size RY drives which also don't have DEC144
- Properly name RQ extended units beyond the initial default units.
- HELP CPU shows supported breakpoint types.
- Add device support for DL11-C/DL11-D/DL11-E/DLV11-J in addition to the original KL11/DL11-A/DL11-B/DL11-E/DL11-F support.  These new devices have different bus address ranges and can coexist with the original DL devices.  The new devices are DLCJI and DLCJO and are managed identically to the original DLI and DLO devices.
- All improvements and fixes to the PDP11 simulator from simh Version 3.12-3 release and beyond.
- MicroVAX I has unsupported devices (TQ, TS, and VH) removed.
- VAX750, VAX780, VAX8600 and PDP11 support additional Massbus disks on DEVICE RPB.
- VAXStation I is now, once again, working.
- MicroVAX I/VAXStation I has been enhanced to dynamically look for its secondary bootstrap program (SYSBOOT.EXE) from both [SYSEXE]SYSBOOT.EXE and [SYS0.SYSEXE]SYSBOOT.EXE.
- PDP11 now has working support for RC and RF expandable platter based disk drives.
- Properly set asynchronous interrupt latency in all VAX simulators.
- MicroVAX I simulator boots from oldest MicroVMS media due to the addition of RQDX1 disk controller type.
- MSCP Media-Id information and drive geometry information is available for all attached disk containers.
- VAX Instruction history can be recorded to disk both for all instructions executed as well as every n instructions.
- VAX Unibus simulators (780, 750, 730, 8600, 8200) run DEC supplied diagnostics at the speed of the original systems and also run the privileged instruction diagnostic that was supported on the original systems.

### All relevant changes in the simh v3.12-4 release have been merged into this repo

### Bill Beech has made significant enhancements and bug fixes to the SWTP simulators along with a new disk controller from Roberto Sancho Villa


## WHAT'S NEW since simh v3.9

### New Simulators

#### Seth Morabito has implemented AT&T 3B2-400 and 3B2-700 simulators.

#### Leonid Broukhis and Serge Vakulenko have implemented a simulator for the Soviet mainframe BESM-6 computer.

#### Matt Burke has implemented new VAX model simulators:

    VAX-11/730
    VAX-11/750
    VAX 8200/8250
    VAX 8600/8650
    MicroVAX I & VAXstation I
    MicroVAX II & VAXstation II & VAXstation II/GPX
    rtVAX 1000 (or Industrial VAX 620)
    MicroVAX 2000 & VAXstation 2000
    MicroVAX 3100 M10/M20
    MicroVAX 3100 M10e/M20e
    InfoServer 100
    InfoServer 150 VXT
    VAXstation 3100 M30
    VAXstation 3100 M38
    VAXstation 3100 M76
    VAXstation 4000 VLC
    VAXstation 4000 M60
    MicroVAX 3100 M80
    InfoServer 1000

#### Howard Harte has implemented a Lincoln Labs TX-0 simulator.

#### Gerardo Ospina has implemented a Manchester University SSEM (Small Scale Experimental Machine) simulator.

#### Richard Cornwell has implemented a Burroughs B5500.

#### Richard Cornwell has implemented the IBM 701, IBM 704, IBM 7010/1410, IBM 7070/7074, IBM 7080/702/705/7053 and IBM 7090/7094/709/704 simulators.

#### Richard Cornwell has implemented the PDP6, PDP10-KA, PDP10-KI, PDP10-KL and PDP10-KS simulators.  With the differences merely being some device name changes, the PDP10-KS should be compatible with Bob Supnik's original PDP10 simulator.

#### Dave Bryan has implemented an HP-3000 Series III simulator.

#### Updated HP2100 simulator from Dave Bryan.

#### Updated AltairZ80 simulator from Peter Schorn.

#### Sigma 5, 6 & 7 simulator from Bob Supnik

#### Beta SAGE-II and PDQ-3 simulators from Holger Veit

#### Intel Systems 8010 and 8020 simulators from Bill Beech

#### CDC 1700 simulator from John Forecast

#### Hans-Åke Lund has implemented an SCELBI (SCientic-ELectronics-BIology) simulator.

#### IBM 650 simulator from Roberto Sancho Villa

#### Jim Bevier has implemented a SEL32 simulator.

#### Updates to the Unibus DUP & Qbus DPV device by Trevor Warwick

Support for Phase V DECnet connections on VAX Unibus and Qbus systems and the addition of support for the DPV11 for Qbus VAX systems.

### New Host Platform support - HP-UX and AIX

### Simulator Front Panel API

The sim_frontpanel API provides a programmatic interface to start and control any simulator without any special additions to the simulator code or changes to the SCP framework.

### New Functionality

#### DDCMP Synchronous host physical device support - framer
Paul Koning has implemented a USB hardware device which can interface transport DDCMP packets across a synchronous line 
to physical host systems with native synchronous devices or other simulators using framer devices.

#### Remote Console Facility
A new capability has been added which allows a TELNET Connection to a user designated port so that some out of band commands can be entered to manipulate and/or adjust a running simulator.  The commands which enable and control this capability are SET REMOTE TELNET=port, SET REMOTE CONNECTIONS=n, SET REMOTE TIMEOUT=seconds, and SHOW REMOTE.

The remote console facility has two modes of operation: 1) single command mode. and 2) multiple command mode.  
In single command mode you enter one command at a time and aren't concerned about what the simulated system is doing while you enter that command.  The command is executed once you've hit return.
In multiple command mode you initiate your activities by entering the WRU character (usually ^E).  This will suspend the current simulator execution.  You then enter commands as needed and when you are done you enter a CONTINUE command.  While entering Multiple Command commands, if you fail to enter a complete command before the timeout (specified by "SET REMOTE TIMEOUT=seconds"), a CONTINUE command is automatically processed and simulation proceeds.

A subset of normal simh commands are available for use in remote console sessions.
The Single Command Mode commands are: ATTACH, DETACH, PWD, SHOW, DIR, LS, ECHO, HELP
The Multiple Command Mode commands are: EXAMINE, IEXAMINE, DEPOSIT, EVALUATE, ATTACH, DETACH, ASSIGN, DEASSIGN, STEP, CONTINUE, PWD, SAVE, SET, SHOW, DIR, LS, ECHO, HELP

A remote console session will close when an EOF character is entered (i.e. ^D or ^Z).

#### VAX/PDP11 Enhancements
    RQ has new disk types: RC25, RCF25, RA80
    RQ device has a settable controller type (RQDX3, UDA50, KLESI, RUX50)
    RQ disks default to Autosize without regard to disk type
    RQ disks on PDP11 can have RAUSER size beyond 2GB
    DMC11/DMR11 DDCMP DECnet device simulation.  Up to 8 DMC devices are supported.  Packet transport is via TCP or UDP connections.
    KDP11 on PDP11 for DECnet
    DUP11 on PDP11 for DECnet connectivity to talk to DMC, KDP or other DUP devices
    CH11 on PDP11 and VAX780 for Chaosnet (from Lars Brinkhoff)
    DZ on Unibus systems can have up to 256 ports (default of 32), on 
        Qbus systems 128 port limit (default of 16).
    DZ devices optionally support full modem control (and port speed settings 
        when connected to serial ports).
    TU58 device support for all PDP11 and VAX systems.
    DHU11 (device VH) on Unibus systems now has 16 ports per multiplexer.
    XQ devices (DEQNA, DELQA and DELQA-T) are bootable on Qbus PDP11 simulators
    XQ and XU devices (DEQNA, DELQA, DELQA-T, DEUNA and DELQA) devices can now 
        directly communicate to a remote device via UDP (i.e. a built-in HECnet bridge).
    XQ and XU devices (DEQNA, DELQA, DELQA-T, DEUNA and DELQA) devices can now 
        optionally throttle outgoing packets which is useful when communicating with
        legacy systems (real hardware) on a local LAN which can easily get over run 
        when packets arrive too fast.
    MicroVAX 3900 has QVSS (VCB01) board available.
    MicroVAX 3900 and MicroVAX II have SET CPU AUTOBOOT option
    MicroVAX 3900 has a SET CPU MODEL=(MicroVAX|VAXserver|VAXstation) command to change between system types
    MicroVAX I has a SET CPU MODEL=(MicroVAX|VAXSTATION) command to change between system types
    MicroVAX II has a SET CPU MODEL=(MicroVAX|VAXSTATION) command to change between system types

#### PDP11 Specific Enhancements
    ROM (from Lars Brinkhoff) I/O page ROM support
    NG (from Lars Brinkhoff) Knight vector display
    DAZ (from Lars Brinkhoff) Dazzle Dart Input device

#### PDP10 Enhancements
    KDP11 (from Timothe Litt) for DECnet connectivity to simulators with DMC, DUP or KDP devices
    DMR11 for DECnet connectivity to simulators with DMC, DUP or KDP devices on TOPS10.
    CH11 (from Lars Brinkhoff) Chaosnet interface.

#### SDS 940 Enhancements
    Support for SDS internal ASCII character encoding during display and data entry.
    Allow breakpoints to be qualified by normal, monitor or user mode.
    Fix CPU, RAD, MUX and I/O bugs that prevented SDS Time Share System Monitor and Executive from executing properly.

#### Terminal Multiplexer additions
    Added support for TCP connections using IPv4 and/or IPv6.
    Logging - Traffic going out individual lines can be optionally logged to 
            files
    Buffering - Traffic going to a multiplexor (or Console) line can 
            optionally be buffered while a telnet session is not connected
            and the buffered contents will be sent out a newly connecting 
            telnet session.  This allows a user to review what may have 
            happened before they connect to that session.

    Serial Port support based on work by J David Bryan and Holger Veit
    Serial Console Support
    Separate TCP listening ports per line
    Outgoing connections per line (virtual Null Modem cable).
    Packet sending and reception semantics for simulated network device support using either TCP or UDP transport.
    Input character rates reflect the natural character arrival time based on the line speed.

#### Video Display Capabilities
Added support for monochrome and color displays with optional keyboards and mice.  
The VAXstation QVSS device (VCB01) and QDSS device (VCB02) simulations use these capabilities.
Host platforms which have libSDL2 available can leverage this functionality.

#### Asynchronous I/O
    * Disk and Tape I/O can be asynchronous.  Asynchronous support exists 
      for pdp11_rq, pdp11_rp and pdp11_tq devices (used by VAX and PDP11 
      simulators).
    * Multiplexer I/O (Telnet and/or Serial) can be asynchronous.  
      Asynchronous support exists for console I/O and most multiplexer 
      devices.  (Still experimental - not currently by default)

#### Clock/Timer Enhancements
    * Asynchronous clocks ticks exist to better support modern processors 
      that have variable clock speeds.  The initial clock calibration model 
      presumed a constant simulated instruction execution rate.  
      Modern processors have variable processor speeds which breaks this 
      key assumption.  
    * Strategies to make up for missed clock ticks are now available
      (independent of asynchronous tick generation).  These strategies
      generate catch-up clock ticks to keep the simulator passage of 
      time consistent with wall clock time.  Simulator time while idling 
      or throttling is now consistent.  Reasonable idling behavior is 
      now possible without requiring that the host system clock tick be
      10ms or less.
    * Simulator writers have access to timing services and explicit wall 
      clock delays where appropriate.

#### Ethernet Transport Enhancements
	* UDP packet transport.  Direct simulator connections to HECnet can be 
	  made without running a local packet bridge program.
	* NAT packet transport.  Simulators which only speak TCP/IP (No DECnet)
	  and want to communicate with their host systems and/or directly to 
	  the Internet can use NAT packet transport.  This also works for WiFi 
	  connected host systems.
	* Packet Transmission Throttling.  When connected to a LAN which has 
	  legacy network adapters (DEQNA, DEUNA) on legacy systems, it is very
	  easy for a simulated system to overrun the receiving capacity of the
	  older systems.  Throttling of simulated traffic delivered to the LAN 
	  can be used to mitigate this problem.
	* Reliable MAC address conflict detection.  
	* Automatic unique default MAC address assignment.  

#### Disk Extensions
    RAW Disk Access (including CDROM)
    Virtual Disk Container files, including differencing disks
    File System type detection to accurately autosize disks.
    Recognized file systems are: DEC ODS1, DEC ODS2, DEC RT11, DEC RSTS, DEC RSX11, Ultrix Partitions, ISO 9660, BSD 2.11 partitions and NetBSD partitions

#### Tape Extensions
    AWS format tape support
    TAR format tape support
    ANSI-VMS, ANSI-RSX11, ANSI-RSTS, ANSI-RT11 format tape support

#### Embedded ROM support
    Simulators which have boot commands which load constant files as part of 
    booting have those files imbedded into the simulator executable.  The 
    imbedded files are used if the normal boot file isn't found when the 
    simulator boots.  Specific examples are:
    
		VAX (MicroVAX 3900 - ka655x.bin)
		VAX8600 (VAX 8600 - vmb.exe)
		VAX780 (VAX 11/780 - vmb.exe)
		VAX750 (VAX 11/750 - vmb.exe, ka750_old.bin, ka750_new.bin), 
		VAX730 (VAX 11/730 - vmb.exe)
		VAX610 (MicroVAX I - ka610.bin)
		VAX620 (rtVAX 1000 - ka620.bin)
		VAX630 (MicroVAX II - ka630.bin)

#### Control Flow

The following extensions to the SCP command language without affecting prior behavior:

    GOTO <Label>                 Command is now available.  Labels are lines 
                                 in which the first non whitespace character 
                                 is a ":".  The target of a goto is the first 
                                 matching label in the current do command 
                                 file which is encountered.  Since labels 
                                 don't do anything else besides being the 
                                 targets of goto's, they could be used to 
                                 provide comments in do command files, for 
                                 example (":: This is a comment")
    RETURN {status}              Return from the current do command file 
                                 execution with the specified status or
                                 the status from the last executed command 
                                 if no status is specified.  Status can be
                                 a number or a SCPE_<conditionname> name 
                                 string.
    SET ON                       Enables error trapping for currently defined 
                                 traps (by ON commands)
    SET NOON                     Disables error trapping for currently 
                                 defined traps (by ON commands)
    ON <statusvalue> commandtoprocess{; additionalcommandtoprocess}
                                 Sets the action(s) to take when the specific 
                                 error status is returned by a command in the 
                                 currently running do command file.  Multiple 
                                 actions can be specified with each delimited 
                                 by a semicolon character (just like 
                                 breakpoint action commands).
    ON ERROR commandtoprocess{; additionalcommandtoprocess}
                                 Sets the default action(s) to take when any 
                                 otherwise unspecified error status is returned 
                                 by a command in the currently running do 
                                 command file.  Multiple actions can be 
                                 specified with each delimited by a semicolon 
                                 character (just like breakpoint action 
                                 commands).
    ON CONTROL_C commandtoprocess{; additionalcommandtoprocess}
                                 Specifies particular actions to perform when
                                 the operator enters CTRL+C while a command
                                 procedure is running.  The default action is 
                                 to exit the current and any nested command 
                                 procedures and return to the sim> input prompt.
    ON <statusvalue>             Clears the action(s) to take when condition occurs
    ON ERROR                     Clears the default actions to take when any 
                                 otherwise unspecified error status is 
                                 returned by a command in the currently 
                                 running do command file.
    ON CONTROL_C
                                 Restores the default CTRL+C behavior for the
                                 currently running command procedure.

    DO <stdin>
                                 Invokes a nested DO command with input from the 
                                 running console.

Error traps can be taken for any command which returns a status other than SCPE_STEP, SCPE_OK, and SCPE_EXIT.   

ON Traps can specify any status value from the following list: NXM, UNATT, IOERR, CSUM, FMT, NOATT, OPENERR, MEM, ARG, STEP, UNK, RO, INCOMP, STOP, TTIERR, TTOERR, EOF, REL, NOPARAM, ALATT, TIMER, SIGERR, TTYERR, SUB, NOFNC, UDIS, NORO, INVSW, MISVAL, 2FARG, 2MARG, NXDEV, NXUN, NXREG, NXPAR, NEST, IERR, MTRLNT, LOST, TTMO, STALL, AFAIL, NOTATT, AMBREG.  These values can be indicated by name or by their internal numeric value (not recommended).

Interactions with ASSERT command and "DO -e":

    DO -e		is equivalent to SET ON, which by itself it equivalent 
                to "SET ON; ON ERROR RETURN".
    ASSERT		failure have several different actions:
       * If error trapping is not enabled then AFAIL causes exit from 
         the current do command file.
       * If error trapping is enabled and an explicit "ON AFAIL" 
         action is defined, then the specified action is performed.
       * If error trapping is enabled and no "ON AFAIL" action is 
         defined, then an AFAIL causes exit from the current do 
         command file.

Other related changes/extensions:
The "!" command (execute a command on the local OS), now returns the command's exit status as the status from the "!" command.  This allows ON conditions to handle error status responses from OS commands and act as desired.

#### Scriptable interactions with running simulators

The EXPECT command now exists to provide a means of reacting to simulator output and the SEND command exists to inject data into programs running within a simulator.

    EXPECT {HALTAFTER=n,}"\r\nPassword: "
    SEND {AFTER=n,}{DELAY=m,}"mypassword\r"
    
    or
    
    EXPECT {HALTAFTER=n,}"\r\nPassword: " SEND {AFTER=n,}{DELAY=m,}"mypassword\r"; GO
    

#### Help

The built-in help system provides a heirarchical oriented help command interface.  
In addition, there is explicit support for per device help:

    HELP dev
    HELP dev ATTACH
    HELP dev SET  (aka HELP SET dev)
    HELP dev SHOW (aka HELP SHOW dev)
    HELP dev REGISTERS

#### Generic SCP support Clock Coscheduling as opposed to per simulator implementations

Device simulator authors can easily schedule their device polling activities to allow for efficient simulator execution when polling for device activity while still being well behaved when their simulated system is actually idle.

#### New SCP Commands:

    SCREENSHOT filename.bmp          Save video window to the specified file
    SET ENV Name=Value               Set Environment variable
    SET ENV -p "Prompt" Name=Default Gather User input into an Environment Variable
    SET ENV -a Name=Expression       Evaluate an expression and store result in an Environment Variable
    SET ASYNCH                       Enable Asynchronous I/O
    SET NOASYNCH                     Disable Asynchronous I/O
    SET VERIFY                       Enable command display while processing DO command files
    SET NOVERIFY                     Enable command display while processing DO command files
    SET MESSAGE                      Enable error message output when commands complete (default)
    SET NOMESSAGE                    Disable error message output when commands complete
    SET QUIET                        Set minimal output mode for command execution
    SET NOQUIET                      Set normal output mode for command execution
    SET PROMPT                       Change the prompt used by the simulator (default sim>)
    SET THROTTLE x/t                 Throttle t ms every x cycles
    SET REMOTE TELNET=port           Specify remote console telnet port
    SET REMOTE NOTELNET              Disables remote console
    SET REMOTE CONNECTIONS=n         Specify the number of concurrent remote console sessions
    SHOW FEATURES                    Displays the devices descriptions and features
    SHOW ASYNCH                      Display the current Asynchronous I/O status
    SHOW SERIAL                      Display the available and/or open serial ports
    SHOW ETHERNET                    Display the available and/or open ethernet connections
    SHOW MULTIPLEXER                 Display the details about open multiplexer devices
    SHOW CLOCKS                      Display the details about calibrated timers
    SHOW REMOTE                      Display the remote console configuration
    SHOW ON                          Display ON condition dispatch actions
    SET ON                           Enable ON condition error dispatching
    SET NOON                         Disable ON condition error dispatching
    GOTO                             Transfer to label in the current DO command file
    CALL                             Call subroutine at indicated label
    RETURN                           Return from subroutine call
    SHIFT                            Slide argument parameters %1 thru %9 left 1
    NOOP                             A no-op command
    ON                               Establish or cancel an ON condition dispatch
    IF                               Test some simulator state and conditionally execute commands
    IF (C-style-expression)          Test some simulator state and conditionally execute commands
    ELSE                             commands to execute when the previous IF wasn't true
    CD                               Change working directory
    SET DEFAULT                      Change working directory
    PWD                              Show working directory
    SHOW DEFAULT                     Show working directory
    DIR {path|file}                  Display file listing
    LS {path|file}                   Display file listing
    NEXT                             Step across a subroutine call or step a single instruction.
    EXPECT                           React to output produced by a simulated system
    SEND                             Inject input to a simulated system's console
    SLEEP time                       Pause command execution for specified time
    SCREENSHOT                       Snapshot the current video display window
    RUN UNTIL breakpoint             Establish the breakpoint specified and run until it is encountered
    RUN UNTIL "output-string" ...    Establish the specified "output-string" as an EXPECT and run until it is encountered.
    GO UNTIL breakpoint              Establish the breakpoint specified and go until it is encountered
    GO UNTIL "output-string" ...     Establish the specified "output-string" as an EXPECT and go until it is encountered.
    RUNLIMIT						 Bound simulator execution time
    TAR                              Manipulate file archives
    CURL                             Access URLs from the web

#### Command Processing Enhancements

##### Environment variable insertion
Built In variables %DATE%, %TIME%, %DATETIME%, %LDATE%, %LTIME%, %CTIME%, %DATE_YYYY%, %DATE_YY%, %DATE_YC%, %DATE_MM%, %DATE_MMM%, %DATE_MONTH%, %DATE_DD%, %DATE_D%, %DATE_WYYYY%, %DATE_WW%, %TIME_HH%, %TIME_MM%, %TIME_SS%, %STATUS%, %TSTATUS%, %SIM_VERIFY%, %SIM_QUIET%, %SIM_MESSAGE%

   Token "%0" expands to the command file name. 
   Token %n (n being a single digit) expands to the n'th argument
   Token %* expands to the whole set of arguments (%1 ... %9)

   The input sequence "%%" represents a literal "%".  All other 
   character combinations are rendered literally.

   Omitted parameters result in null-string substitutions.

   Tokens preceded and followed by % characters are expanded as environment
   variables, and if an environment variable isn't found then it can be one of 
   several special variables: 
   
          %DATE%              yyyy-mm-dd
          %TIME%              hh:mm:ss
          %DATETIME%          yyyy-mm-ddThh:mm:ss
          %LDATE%             mm/dd/yy (Locale Formatted)
          %LTIME%             hh:mm:ss am/pm (Locale Formatted)
          %CTIME%             Www Mmm dd hh:mm:ss yyyy (Locale Formatted)
          %UTIME%             nnnn (Unix time - seconds since 1/1/1970)
          %DATE_YYYY%         yyyy        (0000-9999)
          %DATE_YY%           yy          (00-99)
          %DATE_MM%           mm          (01-12)
          %DATE_MMM%          mmm         (JAN-DEC)
          %DATE_MONTH%        month       (January-December)
          %DATE_DD%           dd          (01-31)
          %DATE_WW%           ww          (01-53)     ISO 8601 week number
          %DATE_WYYYY%        yyyy        (0000-9999) ISO 8601 week year number
          %DATE_D%            d           (1-7)       ISO 8601 day of week
          %DATE_JJJ%          jjj         (001-366) day of year
          %DATE_19XX_YY%      yy          A year prior to 2000 with the same
                                          calendar days as the current year
          %DATE_19XX_YYYY%    yyyy        A year prior to 2000 with the same 
                                          calendar days as the current year
          %TIME_HH%           hh          (00-23)
          %TIME_MM%           mm          (00-59)
          %TIME_SS%           ss          (00-59)
          %STATUS%            Status value from the last command executed
          %TSTATUS%           The text form of the last status value
          %SIM_VERIFY%        The Verify/Verbose mode of the current Do command file
          %SIM_VERBOSE%       The Verify/Verbose mode of the current Do command file
          %SIM_QUIET%         The Quiet mode of the current Do command file
          %SIM_MESSAGE%       The message display status of the current Do command file
          %SIM_NAME%          The name of the current simulator
          %SIM_BIN_NAME%      The program name of the current simulator
          %SIM_BIN_PATH%      The program path that invoked the current simulator
          %SIM_OSTYPE%        The Operating System running the current simulator
          %SIM_RUNTIME%       The Number of simulated instructions or cycles performed
          %SIM_RUNTIME_UNITS% The units of the SIM_RUNTIME value
          %SIM_REGEX_TYPE%    The regular expression type available
          %SIM_MAJOR%         The major portion of the simh version
          %SIM_MINOR%         The minor portion of the simh version
          %SIM_PATCH%         The patch portion of the simh version
          %SIM_DELTA%         The delta portion of the simh version
          %SIM_VM_RELEASE%    An optional VM specific release version
          %SIM_VERSION_MODE%  The release mode (Current, Alpha, Beta)
          %SIM_GIT_COMMIT_ID% The git commit id of the current build
          %SIM_GIT_COMMIT_TIME%  The git commit time of the current build
          %SIM_RUNLIMIT%      The current execution limit defined
          %SIM_RUNLIMIT_UNITS% The units of the SIM_RUNLIMIT value (instructions, cycles or time)
          
   Environment variable lookups are done first with the precise name between 
   the % characters and if that fails, then the name between the % characters
   is upcased and a lookup of that values is attempted.

   The first Space delimited token on the line is extracted in uppercase and 
   then looked up as an environment variable.  If found it the value is 
   substituted for the original string before expanding everything else.  If 
   it is not found, then the original beginning token on the line is left 
   untouched.

##### Command aliases

Commands can be aliases with environment variables.  For example:
   
      sim> set env say=echo
      sim> say Hello there
      Hello there

##### Do command argument manipulation

The SHIFT command will shift the %1 thru %9 arguments to the left one position.

## Building and running a simulator

### Use Prebuilt Windows Simulators

Simulators for the Windows platform are built and made available on a regular basis (at least once a week if substantive changes have been made to the codebase).  

The prebuilt Windows binaries will run on all versions of Microsoft Windows from Windows XP onward.

They can be accessed at https://github.com/simh/Win32-Development-Binaries

Several relatively recent versions should be available which you can download and use directly.

### Building simulators yourself

First download the latest source code from the github repository's master branch at https://github.com/simh/simh/archive/master.zip

Depending on your host platform one of the following steps should be followed:

#### Linux/OSX other *nix platforms

If you are interested in using a simulator with Ethernet networking support (i.e. one of the VAX simulators or the PDP11), then you should make sure you have the correct networking components available.  The instructions in https://github.com/simh/simh/blob/master/0readme_ethernet.txt describe the required steps to get ethernet networking components installed and how to configure your environment.

See the 0readme_ethernet.txt file for details about the required network components for your platform.  Once your operating system build environment has the correct networking components available the following command will build working simulators:

   $ make {simulator-name (i.e. vax)}

The makefile provided requires GNU make, which is the default make facility for most systems these days.  Any host system which doesn't have GNU make available as the default make facility may have it installed as 'gmake'.  GNU make (gmake) is generally available an installation package for all current operating systems which have a package installation system.

##### Build Dependencies

Some simulators depend on external packages to provide the full scope of 
functionality they may be simulating.  These additional external packages 
may or may not be included in as part of the standard Operating System 
distributions.  If simulators are being built that could provide more 
functionality than the currently installed packages will provide, the build
will succeed with reduced functionality (i.e. limited network or no video
support), but suggestions will be provided as to what could provide full 
functionality.


###### OS X - Dependencies

The HomeBrew package manager can be used to provide these packages:

    $ brew install vde pcre libedit sdl2 libpng zlib sdl2_ttf make

OR

The MacPorts package manager is available to provide these external packages.  Once MacPorts is installed, this commands will install the required dependent packages:

    # port install vde2 pcre libedit libsdl2 libpng zlib libsdl2_ttf gmake

###### Linux - Dependencies

Different Linux distributions have different package management systems:

Ubuntu/Debian:

    # apt-get install gcc libpcap-dev libvdeplug-dev libpcre3-dev libedit-dev libsdl2-dev libpng-dev libsdl2-ttf-dev

Fedora/RedHat:

    # yum install gcc libpcap-devel pcre-devel libedit-devel SDL2-devel libpng-devel zlib-devel SDL2_ttf-devel

###### NetBSD - Dependencies

    # pkgin install pcre editline SDL2 png zlib SDL2_ttf gmake

###### FreeBSD - Dependencies

    # pkg install pcre libedit sdl2 png sdl2_ttf gmake

###### OpenBSD - Dependencies

    # pkg_add pcre sdl2 png sdl2-ttf gmake

#### Windows

Compiling on windows is supported with recent versions of Microsoft Visual Studio (Standard or Express) and deprecated using GCC via the MinGW32 environment.  Things may also work under Cygwin, but that is not the preferred windows environment.  Not all features will be available when building with MinGW32 or Cygwin.

##### Required related files
The file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt

##### Visual Studio (Standard or Express) 2008, 2010, 2012, 2013 or Visual Studio Community 2015, 2017, 2019

The file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt describes the required steps to use the setup your environment to build using Visual Studio.

##### MinGW32


Building with MinGW32 is deprecated and may be removed in the future since the original motivation for MinGW32 builds was due to there not being a free compiler environment on Windows.  That hasn't been the case for at least 15 years.
Building with MinGW32 requires the same directory organization and the dependent package support described for Visual Studio in the file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt.  Building with MinGW64 is not supported.

#### VMS

Download the latest source code as a zip file from: https://github.com/simh/simh/archive/master.zip

Unzip it in the directory that you want SIMH to reside in.  Unpack it and 
set the file attributes as follows:

    $ unzip simh-master.zip
    $ set default [.simh-master]
    $ set file/attri=RFM:STM makefile,*.mms,[...]*.c,[...]*.h,[...]*.txt

Simulators with ethernet network devices (All the VAX simulators and the 
PDP11) can have functioning networking when running on Alpha or IA64 OpenVMS.

In order to build and run simulators with networking support, the VMS-PCAP 
package must be available while building your simulator.  The simh-vms-pcap.zip 
file can be downloaded from https://github.com/simh/simh/archive/vms-pcap.zip   
This link will return a file called simh-vms-pcap.zip which should be unpacked as follows:

    $ unzip -a simh-vms-pcap.zip
    $ rename [.simh-vms-pcap]pcap-vms.dir []

The PCAP-VMS components are presumed (by the descript.mms file) to be 
located in a directory at the same level as the directory containing the 
simh source files.  For example, if these exist here:

[]descrip.mms
[]scp.c
etc.

Then the following should exist: 
[-.PCAP-VMS]BUILD_ALL.COM
[-.PCAP-VMS.PCAP-VCI]
[-.PCAP-VMS.PCAPVCM]
etc.

To build simulators:

On a VAX use:

    $ MMx

On a Alpha & IA64 hosts use:

    $ MMx                        ! With Ethernet support
    $ MMx/MACRO=(NONETWORK=1)    ! Without Ethernet support

UNZIP can be found on the VMS freeware CDs, or from www.info-zip.org
MMS (Module Management System) can be licensed from HP/Compaq/Digital as part of the VMS Hobbyist program (it is a component of the DECSET product).
MMK can be found on the VMS freeware CDs, or from http://www.kednos.com/kednos/Open_Source/MMK
DEC C can be licensed from HP/Compaq/Digital as part of the VMS Hobbyist program.

## Problem Reports

If you find problems or have suggestions relating to any simulator or the simh package as a whole, please report these using the github "Issue" interface at https://github.com/simh/simh/issues.

Problem reports should contain;
 - a description of the problem
 - the simulator you experience the problem with
 - your host platform (and OS version)
 - how you built the simulator or that you're using prebuilt binaries
 - the simulator build description should include the output produced by while building the simulator
 - the output of SHOW VERSION while running the simulator which is having an issue
 - the simulator configuration file (or commands) which were used when the problem occurred.
 
