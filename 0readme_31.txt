Notes For V3.1-0

RESTRICTION: The FP15 and XVM features of the PDP-15 are only partially
debugged.  Do NOT enable these features for normal operations.

1. New Features in 3.1-0

1.1 SCP and libraries

- Added simulated Ethernet support for VMS, FreeBSD, Mac OS/X.
- Added status return to tmxr_putc_ln.
- Added sim_putchar_s to handle possible output stalls.

1.2 All DECtapes

- Added "DECtape off reel" error stop.

1.3 All Asynchronous Consoles

- Added support for output congestion stall if using a Telnet connection.

1.4 PDP-1

- Added Type 23 parallel drum support.

1.5 PDP-8

- Added instruction history.
- Added TSC8-75 option support for ETOS.
- Added TD8E DECtape support.

1.6 PDP-18b

- Added instruction history.
- Changed PDP-9, PDP-15 API default to enabled.

1.7 PDP-11

- Added support for 18b only Qbus devices.
- Formalized bus and addressing definitions.
- Added control to enable/disable autoconfiguration.
- Added stub support for second Unibus Ethernet controller.

1.7 Interdata 32b

- Added instruction history.

1.8 Eclipse

- Added floating point support.
- Added programmable interval timer support.

1.9 H316

- Added DMA/DMC support.
- Added fixed head disk support.
- Added moving head disk support.
- Added magtape support.

1.10 IBM 1130 (Brian Knittel)

- Added support for physical card reader, using the Cardread
interface (www.ibm1130.org/sim/downloads).
- Added support for physical printer (flushes output buffer after
each line).

2. Bugs Fixed in 3.1-0

2.1 SCP and libraries

- Fixed numerous bugs in Ethernet library.

2.2 All DECtapes

- Fixed reverse checksum value in 'read all' mode.
- Simplified (and sped up) timing.

2.3 PDP-8

- Fixed bug in RX28 read status (found by Charles Dickman).
- Fixed RX28 double density write.

2.4 PDP-18b

- Fixed autoincrement bug in PDP-4, PDP-7, PDP-9.

2.5 PDP-11/VAX

- Revised RQ MB->LBN conversion for greater accuracy.
- Fixed bug in IO configuration (found by David Hittner).
- Fixed bug with multiple RQ RAUSER drives.
- Fixed bug in second Qbus Ethernet controller interrupts.

2.6 Nova/Eclipse

- Fixed bugs in DKP flag clear, map setup, map usage (Charles Owen).
- Fixed bug in MT, reset completes despite I/O reset (Charles Owen).
- Fixed bug in MT, space operations return word count (Charles Owen).

2.7 IBM 1130 (Brian Knittel)

- Fixed bug in setting carry bit in subtract and subtract double.
- Fixed timing problem in console printer simulation.

2.8 1620

- Fixed bug in branch digit (found by Dave Babcock).

3. New Features in 3.0 vs prior releases

3.1 SCP and Libraries

- Added ASSIGN/DEASSIGN (logical name) commands.
- Changed RESTORE to unconditionally detach files.
- Added E11 and TPC format support to magtape library.
- Fixed bug in SHOW CONNECTIONS.
- Added USE_ADDR64 support.

3.2 All magtapes

- Magtapes support SIMH format, E11 format, and TPC format (read only).
- SET <tape_unit> FORMAT=format sets the specified tape unit's format.
- SHOW <tape_unit> FORMAT displays the specified tape unit's format.
- Tape format can also be set as part of the ATTACH command, using
  the -F switch.

3.3 VAX

- VAX can be compiled without USE_INT64.
- If compiled with USE_INT64 and USE_ADDR64, RQ and TQ controllers support
  files > 2GB.
- VAX ROM has speed control (SET ROM DELAY/NODELAY).

3.4 PDP-1

- Added block loader format support to LOAD.
- Changed BOOT PTR to allow loading of all of the first bank of memory.
- The LOAD command takes an optional argument specifying the memory field
  to be loaded.
- The PTR BOOT command takes its starting memory field from the TA (address
  switch) register.

3.5 PDP-18b Family

- Added PDP-4 EAE support.
- Added PDP-15 FP15 support.
- Added PDP-15 XVM support.
- Added PDP-15 "re-entrancy ECO".
- Added PDP-7, PDP-9, PDP-15 hardware RIM loader support in BOOT PTR.

4. Bugs Fixed in 3.0 vs prior releases

4.1 SCP and Libraries

- Fixed end of file problem in dep, idep.
- Fixed handling of trailing spaces in dep, idep.

4.2 VAX

- Fixed CVTfi bug: integer overflow not set if exponent out of range
- Fixed EMODx bugs:
  o First and second operands reversed
  o Separated fraction received wrong exponent
  o Overflow calculation on separated integer incorrect
  o Fraction not set to zero if exponent out of range
- Fixed interval timer and ROM access to pass power-up self-test even on very
  fast host processors (fixes from Mark Pizzolato).
- Fixed bug in user disk size (found by Chaskiel M Grundman).

4.3 1401

- Fixed mnemonic, instruction lengths, and reverse scan length check bug for MCS.
- Fixed MCE bug, BS off by 1 if zero suppress.
- Fixed chaining bug, D lost if return to SCP.
- Fixed H branch, branch occurs after continue.
- Added check for invalid 8 character MCW, LCA.
- Fixed magtape load-mode end of record response.
- Revised fetch to model hardware more closely.
- Fixed tape read end-of-record handling based on real 1401.
- Added diagnostic read (space forward).

4.4 Nova

- Fixed DSK variable size interaction with restore.
- Fixed bug in DSK set size routine.

4.5 PDP-1

- Fixed DT variable size interaction with restore.
- Updated CPU, line printer, standard devices to detect indefinite I/O wait.
- Fixed incorrect logical, missing activate, break in drum simulator.
- Fixed bugs in instruction decoding, overprinting for line printer.
- Fixed system hang if continue after PTR error.
- Fixed PTR to start/stop on successive rpa instructions.

4.6 PDP-11

- Fixed DT variable size interaction with restore.
- Fixed bug in MMR1 update (found by Tim Stark).
- Added XQ features and fixed bugs:
  o Corrected XQ interrupts on IE state transition (code by Tom Evans).
  o Added XQ interrupt clear on soft reset.
  o Removed XQ interrupt when setting XL or RL (multiple people).
  o Added SET/SHOW XQ STATS.
  o Added SHOW XQ FILTERS.
  o Added ability to split received packet into multiple buffers.
  o Added explicit runt and giant packet processing.
- Fixed bug in user disk size (found by Chaskiel M Grundman).

4.7 PDP-18B

- Fixed DT, RF variable size interaction with restore.
- Fixed MT bug in MTTR.
- Fixed bug in PDP-4 line printer overprinting.
- Fixed bug in PDP-15 memory protect/skip interaction.
- Fixed bug in RF set size routine.
- Increased PTP TIME for PDP-15 operating systems.
- Fixed priorities in PDP-15 API (differs from PDP-9).
- Fixed sign handling in PDP-15 EAE unsigned mul/div (differs from PDP-9).
- Fixed bug in CAF, clears API subsystem.

4.8 PDP-8

- Fixed DT, DF, RF, RX variable size interaction with restore.
- Fixed MT bug in SKTR.
- Fixed bug in DF, RF set size routine.

4.9 HP2100

- Fixed bug in DP (13210A controller only), DQ read status.
- Fixed bug in DP, DQ seek complete.
- Fixed DR drum sizes.
- Fixed DR variable capacity interaction with SAVE/RESTORE.

4.10 GRI

- Fixed bug in SC queue pointer management.

4.11 PDP-10

- Fixed bug in RP read header.

4.12 Ibm1130

- Fixed bugs found by APL 1130.

4.13 Altairz80

- Fixed bug in real-time clock on Windows host.

4.14 1620

- Fixed bug in immediate index add (found by Michael Short).

