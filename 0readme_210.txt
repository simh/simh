Notes For V2.10-1

WARNING: V2.10 has reorganized and renamed some of the definition
files for the PDP-10, PDP-11, and VAX.  Be sure to delete all
previous source files before you unpack the Zip archive, or
unpack it into a new directory structure.

WARNING: V2.10 has a new, more comprehensive save file format.
Restoring save files from previous releases will cause 'invalid
register' errors and loss of CPU option flags, device enable/
disable flags, unit online/offline flags, and unit writelock
flags.

WARNING: If you are using Visual Studio .NET through the IDE,
be sure to turn off the /Wp64 flag in the project settings, or
dozens of spurious errors will be generated.

WARNING: Compiling Ethernet support under Windows requires
extra steps; see the Ethernet readme file.  Ethernet support is
currently available only for Windows, Linux, NetBSD, and OpenBSD.

1. New Features

1.1 SCP and Libraries

- The VT emulation package has been replaced by the capability
  to remote the console to a Telnet session.  Telnet clients
  typically have more complete and robust VT100 emulation.
- Simulated devices may now have statically allocated buffers,
  in addition to dynamically allocated buffers or disk-based
  data stores.
- The DO command now takes substitutable arguments (max 9).
  In command files, %n represents substitutable argument n.
- The initial command line is now interpreted as the command
  name and substitutable arguments for a DO command.  This is
  backward compatible to prior versions.
- The initial command line parses switches.  -Q is interpreted
  as quiet mode; informational messages are suppressed.
- The HELP command now takes an optional argument.  HELP <cmd>
  types help on the specified command.
- Hooks have been added for implementing GUI-based consoles,
  as well as simulator-specific command extensions.  A few
  internal data structures and definitions have changed.
- Two new routines (tmxr_open_master, tmxr_close_master) have
  been added to sim_tmxr.c.  The calling sequence for
  sim_accept_conn has been changed in sim_sock.c.
- The calling sequence for the VM boot routine has been modified
  to add an additional parameter.
- SAVE now saves, and GET now restores, controller and unit flags.
- Library sim_ether.c has been added for Ethernet support.

1.2 VAX

- Non-volatile RAM (NVR) can behave either like a memory or like
  a disk-based peripheral.  If unattached, it behaves like memory
  and is saved and restored by SAVE and RESTORE, respectively.
  If attached, its contents are loaded from disk by ATTACH and
  written back to disk at DETACH and EXIT.
- SHOW <device> VECTOR displays the device's interrupt vector.
  A few devices allow the vector to be changed with SET
  <device> VECTOR=nnn.
- SHOW CPU IOSPACE displays the I/O space address map.
- The TK50 (TMSCP tape) has been added.
- The DEQNA/DELQA (Qbus Ethernet controllers) have been added.
- Autoconfiguration support has been added.
- The paper tape reader has been removed from vax_stddev.c and
  now references a common implementation file, dec_pt.h.
- Examine and deposit switches now work on all devices, not just
  the CPU.
- Device address conflicts are not detected until simulation starts.

1.3 PDP-11

- SHOW <device> VECTOR displays the device's interrupt vector.
  Most devices allow the vector to be changed with SET
  <device> VECTOR=nnn.
- SHOW CPU IOSPACE displays the I/O space address map.
- The TK50 (TMSCP tape), RK611/RK06/RK07 (cartridge disk),
  RX211 (double density floppy), and KW11P programmable clock
  have been added.
- The DEQNA/DELQA (Qbus Ethernet controllers) have been added.
- Autoconfiguration support has been added.
- The paper tape reader has been removed from pdp11_stddev.c and
  now references a common implementation file, dec_pt.h.
- Device bootstraps now use the actual CSR specified by the
  SET ADDRESS command, rather than just the default CSR.  Note
  that PDP-11 operating systems may NOT support booting with
  non-standard addresses.
- Specifying more than 256KB of memory, or changing the bus
  configuration, causes all peripherals that are not compatible
  with the current bus configuration to be disabled.
- Device address conflicts are not detected until simulation starts.

1.4 PDP-10

- SHOW <device> VECTOR displays the device's interrupt vector.
  A few devices allow the vector to be changed with SET
  <device> VECTOR=nnn.
- SHOW CPU IOSPACE displays the I/O space address map.
- The RX211 (double density floppy) has been added; it is off
  by default.
- The paper tape now references a common implementation file,
  dec_pt.h.
- Device address conflicts are not detected until simulation starts.

1.5 PDP-1

- DECtape (then known as MicroTape) support has been added.
- The line printer and DECtape can be disabled and enabled.

1.6 PDP-8

- The RX28 (double density floppy) has been added as an option to
  the existing RX8E controller.
- SHOW <device> DEVNO displays the device's device number.  Most
  devices allow the device number to be changed with SET <device>
  DEVNO=nnn.
- Device number conflicts are not detected until simulation starts. 

1.7 IBM 1620

- The IBM 1620 simulator has been released.

1.8 AltairZ80

- A hard drive has been added for increased storage.
- Several bugs have been fixed.

1.9 HP 2100

- The 12845A has been added and made the default line printer (LPT).
  The 12653A has been renamed LPS and is off by default.  It also
  supports the diagnostic functions needed to run the DCPC and DMS
  diagnostics.
- The 12557A/13210A disk defaults to the 13210A (7900/7901).
- The 12559A magtape is off by default.
- New CPU options (EAU/NOEAU) enable/disable the extended arithmetic
  instructions for the 2116.  These instructions are standard on
  the 2100 and 21MX.
- New CPU options (MPR/NOMPR) enable/disable memory protect for the
  2100 and 21MX.
- New CPU options (DMS/NODMS) enable/disable the dynamic mapping
  instructions for the 21MX.
- The 12539 timebase generator autocalibrates.

1.10 Simulated Magtapes

- Simulated magtapes recognize end of file and the marker
  0xFFFFFFFF as end of medium.  Only the TMSCP tape simulator
  can generate an end of medium marker.
- The error handling in simulated magtapes was overhauled to be
  consistent through all simulators.

1.11 Simulated DECtapes

- Added support for RT11 image file format (256 x 16b) to DECtapes.

2. Release Notes

2.1 Bugs Fixed

- TS11/TSV05 was not simulating the XS0_MOT bit, causing failures
  under VMS.  In addition, two of the CTL options were coded
  interchanged.
- IBM 1401 tape was not setting a word mark under group mark for
  load mode reads.  This caused the diagnostics to crash.
- SCP bugs in ssh_break and set_logon were fixed (found by Dave
  Hittner).
- Numerous bugs in the HP 2100 extended arithmetic, floating point,
  21MX, DMS, and IOP instructions were fixed.  Bugs were also fixed
  in the memory protect and DMS functions.  The moving head disks
  (DP, DQ) were revised to simulate the hardware more accurately.
  Missing functions in DQ (address skip, read address) were added.
- PDP-10 tape wouldn't boot, and then wouldn't read (reported by
  Michael Thompson and Harris Newman, respectively)
- PDP-1 typewriter is half duplex, with only one shift state for
  both input and output (found by Derek Peschel)

2.2 HP 2100 Debugging

- The HP 2100 CPU nows runs all of the CPU diagnostics.
- The peripherals run most of the peripheral diagnostics.  There
  is still a problem in overlapped seek operation on the disks.
  See the file hp2100_diag.txt for details.

3. In Progress

These simulators are not finished and are available in a separate
Zip archive distribution.

- Interdata 16b/32b: coded, partially tested.  See the file
  id_diag.txt for details.
- SDS 940: coded, partially tested.
