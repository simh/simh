Notes For V3.0-0

Because some key files have changed, V3.0 should be unzipped to a
clean directory.

1. New Features in 3.0-0

1.1 SCP and Libraries

- Added ASSIGN/DEASSIGN (logical name) commands.
- Changed RESTORE to unconditionally detach files.
- Added E11 and TPC format support to magtape library.
- Fixed bug in SHOW CONNECTIONS.
- Added USE_ADDR64 support

1.2 All magtapes

- Magtapes support SIMH format, E11 format, and TPC format (read only).
- SET <tape_unit> FORMAT=format sets the specified tape unit's format.
- SHOW <tape_unit> FORMAT displays the specified tape unit's format.
- Tape format can also be set as part of the ATTACH command, using
  the -F switch.

1.3 VAX

- VAX can be compiled without USE_INT64.
- If compiled with USE_INT64 and USE_ADDR64, RQ and TQ controllers support
  files > 2GB.
- VAX ROM has speed control (SET ROM DELAY/NODELAY).

2. Bugs Fixed in 3.01-0

2.1 VAX

- Fixed CVTfi bug: integer overflow not set if exponent out of range
- Fixed EMODx bugs:
  o First and second operands reversed
  o Separated fraction received wrong exponent
  o Overflow calculation on separated integer incorrect
  o Fraction not set to zero if exponent out of range
- Fixed interval timer and ROM access to pass power-up self-test even on very
  fast host processors (fixes from Mark Pizzolato).

2.2 1401

- Fixed mnemonic, instruction lengths, and reverse scan length check bug for MCS.
- Fixed MCE bug, BS off by 1 if zero suppress.
- Fixed chaining bug, D lost if return to SCP.
- Fixed H branch, branch occurs after continue.
- Added check for invalid 8 character MCW, LCA.
- Fixed magtape load-mode end of record response.

2.3 Nova

- Fixed DSK variable size interaction with restore.

2.4 PDP-1

- Fixed DT variable size interaction with restore.

2.5 PDP-11

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

2.6 PDP-18B

- Fixed DT, RF variable size interaction with restore.
- Fixed MT bug in MTTR.

2.7 PDP-8

- Fixed DT, DF, RF, RX variable size interaction with restore.
- Fixed MT bug in SKTR.

2.8 HP2100

- Fixed bug in DP (13210A controller only), DQ read status.
- Fixed bug in DP, DQ seek complete.

2.9 GRI

- Fixed bug in SC queue pointer management.

3. New Features in 3.0 vs prior releases

N/A

4. Bugs Fixed in 3.0 vs prior releases

N/A

5. General Notes

WARNING: The RESTORE command has changed.  RESTORE will now
detach an attached file on a unit, if that unit did not have
an attached file in the saved configuration.  This is required
to assure that the unit flags and the file state are consistent.

WARNING: The compilation scheme for the PDP-10, PDP-11, and VAX
has changed.  Use one of the supplied build files, or read the
documentation carefully, before compiling any of these simulators.
