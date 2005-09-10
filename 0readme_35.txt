Notes For V3.5-0

The source set has been extensively overhauled.  For correct
viewing, set Visual C++ or Emacs to have tab stops every 4
characters.

1. New Features in 3.4-1

1.1 All Ethernet devices

- Added Windows user-defined adapter names (from Timothe Litt)

1.2 Interdata, SDS, HP, PDP-8, PDP-18b terminal multiplexors

- Added support for SET <unit>n DISCONNECT

1.3 VAX

- Added latent QDSS support
- Revised autoconfigure to handle QDSS

1.4 PDP-11

- Revised autoconfigure to handle more casees

2. Bugs Fixed in 3.4-1

2.1 SCP and libraries

- Trim trailing spaces on all input (for example, attach file names)
- Fixed sim_sock spurious SIGPIPE error in Unix/Linux
- Fixed sim_tape misallocation of TPC map array for 64b simulators

2.2 1401

- Fixed bug, CPU reset was clearing SSB through SSG

2.3 PDP-11

- Fixed bug in VH vector display routine
- Fixed XU runt packet processing (found by Tim Chapman)

2.4 Interdata

- Fixed bug in SHOW PAS CONN/STATS
- Fixed potential integer overflow exception in divide

2.5 SDS

- Fixed bug in SHOW MUX CONN/STATS

2.6 HP

- Fixed bug in SHOW MUX CONN/STATS

2.7 PDP-8

- Fixed bug in SHOW TTIX CONN/STATS
- Fixed bug in SET/SHOW TTOXn LOG

2.8 PDP-18b

- Fixed bug in SHOW TTIX CONN/STATS
- Fixed bug in SET/SHOW TTOXn LOG

2.9 Nova, Eclipse

- Fixed potential integer overflow exception in divide


