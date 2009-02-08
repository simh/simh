Notes For V3.8


The makefile now works for Linux and most Unix's. Howevr, for Solaris
and MacOS, you must first export the OSTYPE environment variable:

> export OSTYPE
> make

Otherwise, you will get build errors.


1. New Features

1.1 3.8-0

1.1.1 SCP and Libraries

- BREAK, NOBREAK, and SHOW BREAK with no argument will set, clear, and
  show (respectively) a breakpoint at the current PC.

1.1.2 GRI

- Added support for the GRI-99 processor.

1.1.3 HP2100

- Added support for the BACI terminal interface.
- Added support for RTE OS/VMA/EMA, SIGNAL, VIS firmware extensions.

1.1.4 Nova

- Added support for 64KW memory (implemented in third-party CPU's).

1.1.5 PDP-11

- Added support for DC11, RC11, KE11A, KG11A.
- Added modem control support for DL11.
- Added ASCII character support for all 8b devices.

1.2 3.8-1

1.2.1 SCP and libraries

- Added capability to set line connection order for terminal multiplexers.

1.2.2 HP2100

- Added support for 12620A/12936A privileged interrupt fence.
- Added support for 12792C eight-channel asynchronous multiplexer.


2. Bugs Fixed

Please see the revision history on http://simh.trailing-edge.com or
in the source module sim_rev.h.
