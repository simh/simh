Notes For V2.9-11

1. New Features

1.1 GRI-909

- This is a new simulator for the GRI-909.
- It has been hand-tested; so far, no software has been discovered.

1.2 VAX

- SET CPU CONHALT will cause a HALT instruction to return to the
  boot ROM console rather than to SIMH.  SET CPU SIMHALT restores
  the default behavior.
- BRB/W self at IPL 1F stops the simulator.  This is the default
  behavior of VMS at exit.

1.3 PDP-18b

- ATTACH -A PTR/PTP attaches the reader and punch in ASCII mode.
  In ASCII mode, the reader automatically sets the high order bit
  of incoming alphabetic data, and the punch clears the high order
  bit of outgoing data.

1.4 SCP

- DO -V echoes commands from the file as they are executed.
- Under Windows, execution priority is set BELOW_NORMAL when the
  simulator is running.

2. Release Notes

2.1 Bugs Fixed

- PDP-11 CPU: fixed updating of MMR0 on a memory management error.
- VAX FPA: changed function names to avoid conflict with C math library.
- 1401 MT: read end of record generates group mark without word mark.
- 1401 DP: fixed address generation and checking.
- SCP: an EXIT within a DO command will cause the simulator to exit.

3. In Progress

- Interdata 16b/32b: coded, not tested.
- SDS 940: coded, not tested.
- IBM 1620: coded, not tested.

If you would like to help with the debugging of the untested simulators,
they can be made available by special request.
