Notes For V2.8

1. New Features

1.1 Directory and documentation

- Only common files (SCP and libraries) are in the top level
  directory.  Individual simulator files are in their individual
  directories.
- simh_doc.txt has been split up.  simh_doc.txt now documents
  only SCP.  The individual simulators are documented in separate
  text files in their own directories.
- mingw_build.bat is a batch file for the MINGW/gcc environment
  that will build all the simulators, assuming the root directory
  structure is at c:\sim.
- Makefile is a UNIX make file for the gcc environment that will
  build all the simulators, assuming the root directory is at
  c:\sim.

1.2 SCP

- DO <file name> executes the SCP commands in the specified file.
- Replicated registers in unit structures can now be declared as
  arrays for examine, modify, save, and restore.  Most replicated
  unit registers (for example, mag tape position registers) have
  been changed to arrays.
- The ADD/REMOVE commands have been replaced by SET unit ONLINE
  and SET unit OFFLINE, respectively.
- Register names that are unique within an entire simulator do
  not have to be prefaced with the device name.
- The ATTACH command can attach files read only, either under
  user option (-r), or because the attached file is ready only.
- The SET/SHOW capabilities have been extended.  New forms include:

	SET <dev> param{=value}{ param ...}
	SET <unit> param{=value}{ param ...}
	SHOW <dev> {param param ...}
	SHOW <unit> {param param ...}

- Multiple breakpoints have been implemented.  Breakpoints are
  set/cleared/displayed by:

	BREAK addr_list{[count]}
	NOBREAK addr_list
	SHOW BREAK addr_list

1.3 PDP-11 simulator

- Unibus map implemented, with 22b RP controller (URH70) or 18b
  RP controller (URH11) (in debug).
- All DMA peripherals rewritten to use map.
- Many peripherals modified for source sharing with VAX.
- RQDX3 implemented.
- Bugs fixed in RK11 and RL11 write check.

1.4 PDP-10 simulator

- ITS 1-proceed implemented.
- Bugs fixed in ITS PC sampling and LPMR

1.5 18b PDP simulator

- Interrupts split out to multiple levels to allow easier
  expansion.

1.5 IBM System 3 Simulator

- Written by Charles (Dutch) Owen.

1.6 VAX Simulator (in debug)

- Simulates MicroVAX 3800 (KA655) with 16MB-64MB memory, RQDX3,
  RLV12, TSV11, DZV11, LPV11, PCV11.
- CDROM capability has been added to the RQDX3, to allow testing
  with VMS hobbyist images.

1.7 SDS 940 Simulator (not tested)

- Simulates SDS 940, 16K-64K memory, fixed and moving head
  disk, magtape, line printer, console.

1.8 Altair Z80

- Revised from Charles (Dutch) Owen's original by Peter Schorn.
- MITS 8080 with full Z80 simulation.
- 4K and 8K BASIC packages, Prolog package.

1.9 Interdata

The I4 simulator has been withdrawn for major rework.  Look for
a complete 16b/32b Interdata simulator sometime next year.

2. Release Notes

2.1 SCP

SCP now allows replicated registers in unit structures to be
modelled as arrays.  All replicated register declarations have
been replaced by register array declarations.  As a result,
save files from prior revisions will generate errors after
restoring main memory.

2.2 PDP-11

The Unibus map code is in debug.  The map was implemented primarily
to allow source sharing with the VAX, which requires a DMA map.
DMA devices work correctly with the Unibus map disabled.

The RQDX3 simulator has run a complete RSTS/E SYSGEN, with multiple
drives, and booted the completed system from scratch.

2.3 VAX

The VAX simulator will run the boot code up to the >>> prompt.  It
can successfully process a SHOW DEVICE command.  It runs the HCORE
instruction diagnostic.  It can boot the hobbyist CD through SYSBOOT
and through the date/time dialog and restore the hobbyist CD, using
standalone backup.  On the boot of the restored disk, it gets to the
date/time dialog, and then crashes.

2.4 SDS 940

The SDS 940 is untested, awaiting real code.

2.5 GCC Optimization

At -O2 and above, GCC does not correctly compile the simulators which
use setjmp-longjmp (PDP-11, PDP-10, VAX).  A working hypothesis is
that optimized state maintained in registers is being used in the
setjmp processing routine.  On the PDP-11 and PDP-10, all of this
state has been either made global, or volatile, to encourage GCC to
keep the state up to date in memory.  The VAX is still vulnerable.

3. Work list

3.1 SCP

- Better ENABLE/DISABLE.

3.2 PDP-11 RQDX3

Software mapped mode, RCT read simulation, VMS debug.


