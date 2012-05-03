Notes For V3.9


The makefile now works for all *nix platforms and with cygwin and MinGW32 
on Windows.  It will automatically detect the availability of libpcap 
components and build network capable simulators if they are available.


1. New Features

1.1 3.9-0

1.1.1 SCP and libraries

	- added *nix READLINE support (Mark Pizzolato)
	- added "SHOW SHOW" and "SHOW <dev> SHOW" commands (Mark Pizzolato)
	- added support for BREAK key on Windows (Mark Pizzolato)
	- added ethernet support (Mark Pizzolato)
	     windows host <-> simulator NIC sharing
	     native tap interfaces on BSD, Linux and OSX
	     vde (Virtual Distributed Ethernet) networking
	     Large Send Offload support
	     UDP and TCP Checksum offload support
	     dynamic libpcap loading on *nix platforms

1.1.2 PDP-8

	- floating point processor is now enabled

1.1.3 HP2100 (Dave Bryan)

	- added support for 12821A HP-IB disk controller,
	  7906H/20H/25H disks

1.1.4 PDP11 and VAX (Mark Pizzolato)

        - added DELQA-Plus device

1.1.5 IA64 VMS Ethernet Support

        - identified compiler version issues and added IA64 support (Matt Burke)
        

2. Bugs Fixed

Please see the revision history on http://simh.trailing-edge.com or
in the source module sim_rev.h.


3. Status Report

This is the last release of SimH for which I will be lead editor. After this
release, the source is moving to a public repository:

	https://github.com/simh/simh

under the general editorship of Dave Hittner and Mark Pizzolato. The status
of the individual simulators is as follows:

3.1 PDP-1

Stable and working; runs available software.

3.2 PDP-4/7/9/15

Stable and working; runs available software.

3.3 PDP-8

Stable and working; runs available software.

3.4 PDP-10 [KS-10 only]

Stable and working; runs available software.

3.5 PDP-11

Stable and working; runs available system software. The emulation of individual
models has numerous errors of detail, which prevents many diagnostics from
running correctly.

3.6 VAX-11/780

Stable and working; runs available software.

3.7 MicroVAX 3900 (VAX)

Stable and working; runs available software. Thanks to the kind generosity of
Camiel Vanderhoeven, this simulator has been verified with AXE, the VAX
architectural exerciser.

3.8 Nova

Stable and working; runs available software.

3.9 Eclipse

Stable and working, but not really supported. There is no Eclipse-specific
software available under a hobbyist license.

3.10 Interdata 16b

Stable and working, but no software for it has been found, other than
diagnostics.

3.11 Interdata 32b

Stable and working; runs 32b UNIX and diagnostics.

3.12 IBM 1401

Stable and working; runs available software.

3.13 IBM 1620

Hand debug only. No software for it has been found or tested.

3.14 IBM 7094

Stable and working as a stock system; runs IBSYS. The CTSS extensions
have not been debugged.

3.15 IBM S/3

Stable and working, but not really supported. Runs available software.

3.16 IBM 1130

Stable and working; runs available software. Supported and edited by
Brian Knittel.

3.17 HP 2100/1000

Stable and working; runs available software. Supported and edited by
Dave Bryan.

3.18 Honeywell 316/516

Stable and working; runs available software.

3.19 GRI-909/99

Hand debug only. No software for it has been found or tested.

3.20 SDS-940

Hand debug only, and a few diagnostics.

3.21 LGP-30

Unfinished; hand debug only. Does not run available software, probably
due to my misunderstanding of the LGP-30 operational procedures.

3.22 Altair (original 8080 version)

Stable and working, but not really supported. Runs available software.

3.23 AltairZ80 (Z80 version)

Stable and working; runs available software. Supported and edited by
Peter Schorn.

3.24 SWTP 6800

Stable and working; runs available software. Supported and edited by
Bill Beech

3.25 Sigma 32b

Incomplete; more work is needed on the peripherals for accuracy.
Included in the beta simulators package.

3.26 Alpha

Incomplete; essentially just an EV-5 (21164) chip emulator. Included
in the beta simulators package.

3.27 SAGE

Incomplete. Included in the beta simulators package.

3.28 SC1

Internal simulator for SiCortex supercomputer; intended as an example
of implementing an SMP system in the current SimH structure. Included
in the beta simulators package.


4. Suggestions for Future Work

4.1 General Structure

	- Multi-threading, to allow true concurrency between SCP and the simulator
	- Graphics device support, particularly for the PDP-1 and PDP-11

4.2 Current Simulators

	- PDP-1 graphics, to run Space War
	- PDP-11 GT40 graphics, to run Lunar Lander
	- PDP-15 MUMPS-15
	- Interdata native OS debug, both 16b and 32b
	- SDS 940 timesharing operating system debug
	- IBM 7094 CTSS feature debug and operating system debug
	- IBM 1620 debug and software
	- GRI-909 software
	- Sigma 32b completion and debug
	- LGP-30 debug

4.3 Possible Future Simulators

	- Data General MV8000 (if a hobbyist license can be obtained for AOS)
	- Alpha simulator
	- HP 3000 (16b) simulator with MPE
