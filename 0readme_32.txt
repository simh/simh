Notes For V3.2-1

RESTRICTION: The PDP-15 FPP is only partially debugged.  Do NOT
enable this feature for normal operations.

1. New Features in 3.2-1

1.1 SCP and libraries

- Added SET CONSOLE subhierarchy.
- Added SHOW CONSOLE subhierarchy.
- Added limited keyboard mapping capability.

1.2 HP2100 (new features from Dave Bryan)

- Added instruction printout to HALT message.
- Added M and T internal registers.
- Added N, S, and U breakpoints.

1.3 PDP-11 and VAX

- Added DHQ11 support (from John Dundas)

2. Bugs Fixed in 3.2-1

2.1 HP2100 (most fixes from Dave Bryan)

- SBT increments B after store.
- DMS console map must check dms_enb.
- SFS x,C and SFC x,C work.
- MP violation clears automatically on interrupt.
- SFS/SFC 5 is not gated by protection enabled.
- DMS enable does not disable mem prot checks.
- DMS status inconsistent at simulator halt.
- Examine/deposit are checking wrong addresses.
- Physical addresses are 20b not 15b.
- Revised DMS to use memory rather than internal format.
- Revised IBL facility to conform to microcode.
- Added DMA EDT I/O pseudo-opcode.
- Separated DMA SRQ (service request) from FLG.
- Revised peripherals to make SFS x,C and SFC x,C work.
- Revised boot ROMs to use IBL facility.
- Revised IBL treatment of SR to preserve SR<5:3>.
- Fixed LPS, LPT timing.
- Fixed DP boot interpretation of SR<0>.
- Revised DR boot code to use IBL algorithm.
- Fixed TTY input behavior during typeout for RTE-IV.
- Suppressed nulls on TTY output for RTE-IV.
- Added SFS x,C and SFC x,C to print/parse routines.
- Fixed spurious timing error in magtape reads.

2.2 All DEC console devices

- Removed SET TTI CTRL-C option.

2.3 PDP-11/VAX peripherals

- Fixed bug in TQ reporting write protect status (reported by Lyle Bickley).
- Fixed TK70 model number and media ID (found by Robert Schaffrath).
- Fixed bug in autoconfigure (found by John Dundas).

2.4 VAX

- Fixed bug in DIVBx and DIVWx (reported by Peter Trimmel).
