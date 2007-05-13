Notes For V3.7


1. New Features

1.1 3.7-0

1.1.1 SCP

- Added SET THROTTLE and SET NOTHROTTLE commands to regulate simulator
  execution rate and host resource utilization.
- Added idle support (based on work by Mark Pizzolato).
- Added -e to control error processing in nested DO commands (from
  Dave Bryan).

1.1.2 HP2100

- Added Double Integer instructions, 1000-F CPU, and Floating Point
  Processor (from Dave Bryan).
- Added 2114 and 2115 CPUs, 12607B and 12578A DMA controllers, and
  21xx binary loader protection (from Dave Bryan).

1.1.3 Interdata

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state.

1.1.4 PDP-11

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state (WAIT instruction executed).
- Added TA11/TU60 cassette support.

1.1.5 PDP-8

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state (keyboard poll loop or jump-to-self).
- Added TA8E/TU60 cassette support.

1.1.6 PDP-1

- Added support for 16-channel sequence break system.
- Added support for PDP-1D extended features and timesharing clock.
- Added support for Type 630 data communications subsystem.

1.1.6 PDP-4/7/9/15

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state (keyboard poll loop or jump-to-self).

1.1.7 VAX, VAX780

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state (more than 200 cycles at IPL's 0, 1, or 3 in kernel mode).

1.1.8 PDP-10

- Added SET IDLE and SET NOIDLE commands to idle the simulator in wait
  state (operating system dependent).
- Added CD20 (CD11) support.


2. Bugs Fixed

Please see the revision history on http://simh.trailing-edge.com or
in the source module sim_rev.h.
