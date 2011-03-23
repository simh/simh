Notes For V3.8


The makefile now works for Linux and most Unix's. However, for Solaris
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

1.3 3.8-2

1.3.1 SCP and libraries

- Added line history capability for *nix hosts.
- Added "SHOW SHOW" and "SHOW <dev> SHOW" commands.

1.3.2 1401

- Added "no rewind" option to magtape boot.

1.3.3 PDP-11

- Added RD32 support to RQ
- Added debug support to RL

1.3.4 PDP-8

- Added FPP support (many thanks to Rick Murphy for debugging the code)

1.3.5 VAX-11/780

- Added AUTORESTART switch support, and VMS REBOOT command support


2. Bugs Fixed

Please see the revision history on http://simh.trailing-edge.com or
in the source module sim_rev.h.
