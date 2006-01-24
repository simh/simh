Notes For V3.5-1

The source set has been extensively overhauled.  For correct
viewing, set Visual C++ or Emacs to have tab stops every 4
characters.

1. New Features

1.1 3.5-0

1.1.1 All Ethernet devices

- Added Windows user-defined adapter names (from Timothe Litt)

1.1.2 Interdata, SDS, HP, PDP-8, PDP-18b terminal multiplexors

- Added support for SET <unit>n DISCONNECT

1.1.3 VAX

- Added latent QDSS support
- Revised autoconfigure to handle QDSS

1.1.4 PDP-11

- Revised autoconfigure to handle more cases

1.2 3.5-1

No new features

1.3 3.5-2

1.3.1 All ASCII terminals

- Most ASCII terminal emulators have supported 7-bit and 8-bit
  operation; where required, they have also supported an upper-
  case only or KSR-emulation mode.  This release adds a new mode,
  7P, for 7-bit printing characters.  In 7P mode, non-printing
  characters in the range 0-31 (decimal), and 127 (decimal), are
  automatically suppressed.  This prevents printing of fill
  characters under Windows.

  The printable character set for ASCII code values 0-31 can be
  changed with the SET CONSOLE PCHAR command.  Code value 127
  (DELETE) is always suppressed.

1.3.2 VAX-11/780

- First release.  The VAX-11/780 has successfully run VMS V7.2.  The
  commercial instructions and compatability mode have not been
  extensively tested.  The Ethernet controller is not working yet
  and is disabled.

2. Bugs Fixed

2.1 3.5-0

2.1.1 SCP and libraries

- Trim trailing spaces on all input (for example, attach file names)
- Fixed sim_sock spurious SIGPIPE error in Unix/Linux
- Fixed sim_tape misallocation of TPC map array for 64b simulators

2.1.2 1401

- Fixed bug, CPU reset was clearing SSB through SSG

2.1.3 PDP-11

- Fixed bug in VH vector display routine
- Fixed XU runt packet processing (found by Tim Chapman)

2.1.4 Interdata

- Fixed bug in SHOW PAS CONN/STATS
- Fixed potential integer overflow exception in divide

2.1.5 SDS

- Fixed bug in SHOW MUX CONN/STATS

2.1.6 HP

- Fixed bug in SHOW MUX CONN/STATS

2.1.7 PDP-8

- Fixed bug in SHOW TTIX CONN/STATS
- Fixed bug in SET/SHOW TTOXn LOG

2.1.8 PDP-18b

- Fixed bug in SHOW TTIX CONN/STATS
- Fixed bug in SET/SHOW TTOXn LOG

2.1.9 Nova, Eclipse

- Fixed potential integer overflow exception in divide

2.2 3.5-1

2.2.1 1401

- Changed character encodings to be compatible with Pierce 709X simulator
- Added mode for old/new character encodings

2.2.2 1620

- Changed character encodings to be compatible with Pierce 709X simulator

2.2.3 PDP-10

- Changed MOVNI to eliminate GCC warning

2.2.4 VAX

- Fixed bug in structure definitions with 32b compilation options
- Fixed bug in autoconfiguration table

2.2.5 PDP-11

- Fixed bug in autoconfiguration table

2.3 3.5-2

2.3.1 PDP-10

- RP: fixed drive clear not to clear disk address

2.3.2 PDP-11 (VAX, VAX-11/780, for shared peripherals)

- HK: fixed overlap seek interaction with drive select, drive clear, etc
- RQ, TM, TQ, TS, TU: widened address display to 64b when USE_ADDR64 option selected
- TU: changed default adapter from TM02 to TM03 (required by VMS)
- RP: fixed drive clear not to clear disk address
- RP, TU: fixed device enable/disable to enabled/disable Massbus adapter as well
- XQ: fixed register access alignment bug (found by Doug Carman)

2.3.3 PDP-8

- RL: fixed IOT 61 decoding bug (found by David Gesswein)
- DF, DT, RF: fixed register access alignment bug (found by Doug Carman)

2.3.4 VAX

- Fixed CVTfi to trap on integer overflow if PSW<iv> is set
- Fixed breakpoint detection when USE_ADDR64 option selected
