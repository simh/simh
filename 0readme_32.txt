Notes For V3.2-0

RESTRICTION: The PDP-15 FPP is only partially debugged.  Do NOT
enable this feature for normal operations.

WARNING: The core simulator files (scp.c, sim_*.c) have been
reorganized.  Unzip V3.2-0 to an empty directory before attempting
to compile the source.

IMPORTANT: If you are compiling for UNIX, please read the notes
for Ethernet very carefully.  You may need to download a new
version of the pcap library, or make changes to the makefile,
to get Ethernet support to work.

1. New Features in 3.2-0

1.1 SCP and libraries

- Added SHOW <device> RADIX command.
- Added SHOW <device> MODIFIERS command.
- Added SHOW <device> NAMES command.
- Added SET/SHOW <device> DEBUG command.
- Added sim_vm_parse_addr and sim_vm_fprint_addr optional interfaces.
- Added REG_VMAD flag.
- Split SCP into separate libraries for easier modification.
- Added more room to the device and unit flag fields.
- Changed terminal multiplexor library to support unlimited.
  number of async lines.

1.2 All DECtapes

- Added STOP_EOR flag to enable end-of-reel error stop
- Added device debug support.

1.3 Nova and Eclipse

- Added QTY and ALM multiplexors (Bruce Ray).

1.4 LGP-30

- Added LGP-30/LGP-21 simulator.

1.5 PDP-11

- Added format, address increment inhibit, transfer overrun
  detection to RK.
- Added device debug support to HK, RP, TM, TQ, TS.
- Added DEUNA/DELUA (XU) support (Dave Hittner).
- Add DZ per-line logging.

1.6 18b PDP's

- Added support for 1-4 (PDP-9)/1-16 (PDP-15) additional
  terminals.

1.7 PDP-10

- Added DEUNA/DELUA (XU) support (Dave Hittner).

1.8 VAX

- Added extended memory to 512MB (Mark Pizzolato).
- Added RXV21 support.

2. Bugs Fixed in 3.2-0

2.1 SCP

- Fixed double logging of SHOW BREAK (found by Mark Pizzolato).
- Fixed implementation of REG_VMIO.

2.2 Nova and Eclipse

- Fixed device enable/disable support (found by Bruce Ray).

2.3 PDP-1

- Fixed bug in LOAD (found by Mark Crispin).

2.4 PDP-10

- Fixed bug in floating point unpack.
- Fixed bug in FIXR (found by Phil Stone, fixed by Chris Smith).

2.6 PDP-11

- Fixed bug in RQ interrupt control (found by Tom Evans).

2.6 PDP-18B

- Fixed bug in PDP-15 XVM g_mode implementation.
- Fixed bug in PDP-15 indexed address calculation.
- Fixed bug in PDP-15 autoindexed address calculation.
- Fixed bugs in FPP-15 instruction decode.
- Fixed clock response to CAF.
- Fixed bug in hardware read-in mode bootstrap.
- Fixed PDP-15 XVM instruction decoding errors.

2.7 VAX

- Fixed PC read fault in EXTxV.
- Fixed PC write fault in INSV.

