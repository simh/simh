Notes For V3.11

3.X aka "SimH Classic" is my original code base, with mutually-agreed
extensions by Dave Bryan. It emphasizes continuity and upward compatibility 
of APIs and data structures so that the simulators can remain largely
untouched by improvements and extensions in the core libraries.

The current release is 3.11. 3.11 will provide the basis for my future
simulation work and for Dave Bryan's HP simulators.


1. New Features

1.1 SCP and libraries

- -n added in ATTACH, meaning "force empty file".
- SET <device|unit> APPEND.
- terminal mux log files are flushed at simulator stop
  and closed at simulator exit

1.2 1401

- Option to read cards from the console terminal window.
- Option to print line printer output in the console terminal window.

1.3 1620

- Tab stop support added to console terminal.
- Deferred IO added for console terminal and paper-tape reader/punch.

1.4 PDP-8

- LOAD command now supports multi-segment binary tapes.

1.5 PDP11

- Added RS03/RS04 Massbus fixed head disk support.
- Added KSR option for console terminal.

1.6 PDP10

- LOAD command now supports ITS RIM format.

1.7 PDP15

- Added DR15/UC15 support for PDP15/76.

1.8 UC15

- New simulator, variant on standard PDP11, for PDP15/76 configuration.

1.7 HP3000

- New release (from Dave Bryan). Moved to its own subsite.

1.8 HP2100

- New release (from Dave Bryan). Move to its own subsite.

1.9 AltairZ80, IBM 1130, SWTP 6800

- Removed; V4.X only.

1.10 Sigma

- New simulator (moved out of beta).

1.11 VAX

- Idle support added for VMS 5.0/5.1.


2. Bugs Fixed

Please see the revision history in the source module sim_rev.h.


