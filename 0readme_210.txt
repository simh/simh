Notes For V2.10-4

1. New Features in 2.10-4

1.1 SCP and Libraries

- Added .ini startup file capability (suggested by Hans Pufal).
- Added multiple switch evaluation points (suggested by Hans Pufal).
- Added multiple command per action.
- Added new library, sim_tape.c, for magtape emulation.

1.2 PDP-11

- Added user-defined disk capacity to RQ.
- Addec choice of controllers to TQ.
- Added user-defined tape capacity to TQ.

1.3 Interdata

- Added SHOW SELCH n command to display selector channel state.

1.4 Line Frequency Clocks (H316, Interdata, Nova, PDP-8, PDP-11,
    PDP-18B, SDS)

- Added SET <device> {50HZ/60HZ}, to set the line frequency.

1.5 DEC Console Input (PDP-8, PDP-11, PDP-18B, VAX)

- Added SET TTI CTRL-C, to generate ^C from SIMH prompt (^C
  crashes simulators compiled with Windows Visual C++).

1.6 Magtapes

- Revised to use magtape library for consistency.

2. Bugs Fixed in 2.10-4

- SCP: fixed bug in multiword deposits to files
- Interdata disks: fixed bug in cylinder overflow on writes
- Interdata tape: fixed bug, read error did not stop selector
  channel
- Interdata precision clock: improved autocalibrate algorithm
  for UNIX V7.
- Nova fixed head disk: fixed autosize algorithm.
- PDP-11 RQ and TQ: fixed bugs in queue process and in vector
  calculation for VAXen.
- PDP-11 TQ: fixed overly strict implementation of illegal
  modifiers check.
- PDP-11 RY: fixed autosize algorithm.
- PDP-18B CPU: fixed three EAE bugs (found by Hans Pufal).
- PDP-18B MT: fixed bugs in interrupt handling, BOT error handling.
- PDP-18B RF: removed extra bit from disk address, fixed autosize
  algorithm.
- PDP-18B SYS: fixed bug in FMTASC usage (found by Hans Pufal).
- PDP-8 MT: fixed bug in BOT error handling.
- PDP-8 DF, RF, RX: fixed autosize algorithm.

3. New Features in 2.10 vs prior releases

3.1 SCP and Libraries

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
- The EVAL command will evaluate a symbolic type-in and display
  it in numeric form.
- The ! command (with no arguments) will launch the host operating
  system command shell.  The ! command (with an argument) executes
  the argument as a host operating system command.  (Code from
  Mark Pizzolato)
- Telnet sessions now recognize BREAK.  How a BREAK is transmitted
  dependent on the particular Telnet client.  (Code from Mark
  Pizzolato)
- The sockets library includes code for active connections as
  well as listening connections.
- The RESTORE command will restore saved memory size, if the
  simulator supports dynamic memory resizing.
- Added dynamic extension of the breakpoint table.
- Added breakpoint actions.
- Added VMS support for ! (from Mark Pizzolato).

3.2 VAX

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
- If the VAX console is attached to a Telnet session, BREAK is
  interpreted as console halt.
- The SET/SHOW HISTORY commands enable and display a history of
  the most recently executed instructions.  (Code from Mark
  Pizzolato)

3.3 PDP-11

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
- The PDP-11 implements a stub DEUNA/DELUA (XU).  The real XU
  module will be included in a later release.

3.4 PDP-10

- SHOW <device> VECTOR displays the device's interrupt vector.
  A few devices allow the vector to be changed with SET
  <device> VECTOR=nnn.
- SHOW CPU IOSPACE displays the I/O space address map.
- The RX211 (double density floppy) has been added; it is off
  by default.
- The paper tape now references a common implementation file,
  dec_pt.h.
- Device address conflicts are not detected until simulation starts.
- The PDP-10 implements a stub DEUNA/DELUA (XU).  The real XU
  module will be included in a later release.

3.5 PDP-1

- DECtape (then known as MicroTape) support has been added.
- The line printer and DECtape can be disabled and enabled.
- The PDP-1 supports the Type 24 serial drum (based on recently
  discovered documents).

3.6 18b PDP's

- The PDP-4 supports the Type 24 serial drum (based on recently
  discovered documents).
- Added RB09 fixed head disk for the PDP-9.
- Added LP09 line printer for the PDP-9 and PDP-15.
- Added variable size support and autosizing to the RF15/RF09.

3.7 PDP-8

- The RX28 (double density floppy) has been added as an option to
  the existing RX8E controller.
- SHOW <device> DEVNO displays the device's device number.  Most
  devices allow the device number to be changed with SET <device>
  DEVNO=nnn.
- Device number conflicts are not detected until simulation starts.
- Added variable size support and autosizing to the DF32 and RF08.

3.8 Nova

- Added variable size support and autosizing to the Novadisk.

3.9 AltairZ80

- A hard drive has been added for increased storage.
- Several bugs have been fixed.

3.10 HP 2100

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
- The IOP microinstruction set is supported for the 21MX as well
  as the 2100.
- The HP2100 supports the Access Interprocessor Link (IPL).

3.11 Simulated Magtapes

- Simulated magtapes recognize end of file and the marker
  0xFFFFFFFF as end of medium.  Only the TMSCP tape simulator
  can generate an end of medium marker.
- The error handling in simulated magtapes was overhauled to be
  consistent through all simulators.

3.12 Simulated DECtapes

- Added support for RT11 image file format (256 x 16b) to DECtapes.

3.13 Terminals Multiplexors

- BREAK detection was added to the HP, DEC, and Interdata terminal
  multiplexors.

4. Bugs Fixed in 2.10 vs prior releases

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
- PDP-11 console must default to 7b for early UNIX compatibility.
- PDP-11/VAX TMSCP emulator was using the wrong packet length for
  read/write end packets.
- Telnet IAC+IAC processing was fixed, both for input and output
  (found by Mark Pizzolato).
- PDP-11/VAX Ethernet setting flag bits wrong for chained
  descriptors (found by Mark Pizzolato).
- 18b PDP RF15/RF09: fixed IOT decoding and address wraparound
  logic (found by Hans Pufal).
- 18b PDP RP15: fixed IOT decoding and command initiation.
- HP2100 IPL: changed to full duplex (found by Mike Gemeny).
- HP2100 CPU: fixed last cycle bug in DMA outpout (found by Mike
  Gemeny).
- Interdata 16b CPU: fixed bug in SETM, SETMR (found by Mark
  Pizzolato).


5. General Notes

WARNING: The build procedures have changed.  There is only one UNIX
makefile.  To compile without Ethernet support, simply type

	gmake {target|all}

To compile with Ethernet support, type

	gmake USE_NETWORK=1 {target|all}

The Mingw batch files require Mingw release 2 and invoke the Unix
makefile.  There are still separate batch files for compilation
with or without Ethernet support.

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
