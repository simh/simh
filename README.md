# SIMH v4.0 - 19-01 Current

[![Coverity Scan Build Status](https://scan.coverity.com/projects/11982/badge.svg)](https://scan.coverity.com/projects/simh)
[![Build Status](https://travis-ci.org/simh/simh.svg)](https://travis-ci.org/simh/simh)

## Table of Contents:
[WHAT'S NEW since simh v3.9](#whats-new-since-simh-v39)  
. . [New Simulators](#new-simulators)  
. . [Simulator Front Panel API](#simulator-front-panel-api)  
. . [New Functionality](#new-functionality)  
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
. . . . . . . . [Visual Studio (Standard or Express) 2008, 2010, 2012, 2013 or Visual Studio Community 2015](#visual-studio-standard-or-express-2008-2010-2012-2013-or-visual-studio-community-2015)  
. . . . . . . . [MinGW](#mingw)  
. . . . . . [VMS](#vms)  
. . [Problem Reports](#problem-reports)  

## WHAT'S NEW since simh v3.9

### New Simulators

#### Seth Morabito has implemented a AT&T 3B2 simulator.

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

#### Dave Bryan has implemented an HP-3000 Series III simulator.

#### Updated AltairZ80 simulator from Peter Schorn.

#### Updated HP2100 simulator from Dave Bryan.

#### Sigma 5, 6 & 7 simulator from Bob Supnik

#### Beta SAGE-II and PDQ-3 simulators from Holger Veit

#### Intel Systems 8010 and 8020 simulators from Bill Beech

#### CDC 1700 simulator from John Forecast

#### Hans-Åke Lund has implemented an SCELBI (SCientic-ELectronics-BIology) simulator.

### New Host Platform support - HP-UX and AIX

### Simulator Front Panel API

The sim_frontpanel API provides a programmatic interface to start and control any simulator without any special additions to the simulator code.

### New Functionality

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
Added support for monochrome displays with optional keyboards and mice.  
The VAXstation QVSS device (VCB01) simulation uses this capability.
Host platforms which have libSDL available can leverage this functionality.

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

#### Tape Extensions
    AWS format tape support
    TAR format tape support

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

Some simulators depend on external packages to provide the full scope of functionality they may be simulating.  These additional external packages may or may not be included in as part of the standard Operating System distributions.  

###### OS X - Dependencies

The MacPorts package manager is available to provide these external packages.  Once MacPorts is installed, these commands will install the required dependent packages:

    # port install vde2
    # port install libsdl2
    # port install libsdl2_ttf

OR

The HomeBrew package manager can be used to provide these packages:

    $ brew install vde
    $ brew install sdl2
    $ brew install sdl2_ttf

###### Linux - Dependencies

Different Linux distributions have different package management systems:

Ubuntu:

    # apt-get install libpcap-dev
    # apt-get install vde2
    # apt-get install libsdl2
    # apt-get install libsdl2_ttf

#### Windows

Compiling on windows is supported with recent versions of Microsoft Visual Studio (Standard or Express) and using GCC via the MinGW environment.  Things may also work under Cygwin, but that is not the preferred windows environment.  Not all features will be available as well as with either Visual Studio or MinGW.

##### Required related files
The file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt

##### Visual Studio (Standard or Express) 2008, 2010, 2012, 2013 or Visual Studio Community 2015

The file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt describes the required steps to use the setup your environment to build using Visual Studio.

##### MinGW

The file https://github.com/simh/simh/blob/master/Visual%20Studio%20Projects/0ReadMe_Projects.txt describes the required steps to use the setup your environment to build using MinGW.

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
 
