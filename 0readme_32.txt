Notes For V3.2-3

RESTRICTION: The PDP-15 FPP is only partially debugged.  Do NOT
enable this feature for normal operations.
RESTRICTION: The HP DS disk is not debugged.  DO NOT enable this
feature for normal operations.

1. New Features in 3.2-3

1.1 SCP

- Added ECHO command (from Dave Bryan)

2. Bugs Fixed in 3.2-2

2.1 SCP

- Qualified RESTORE detach with SIM_SW_REST
- Fixed OS/2 issues in sim_console.c and sim_sock.h

2.2 HP2100 (all from Dave Bryan)

- Changed CPU error stops to report PC not PC + 1

- Fixed CLC to DR to stop operation in progress

- Functional and timing fixes to DP
  > controller sets ATN for all commands except read status
  > controller resumes polling for ATN interrupts after read status
  > check status on unattached drive set busy and not ready
  > check status tests wrong unit for write protect status
  > drive on line sets ATN, will set FLG if polling

- Functional and timing fixes to MS
  > fixed erroneous execution of rejected command
  > fixed erroneous execution of select-only command
  > fixed erroneous execution of clear command
  > fixed odd byte handling for read
  > fixed spurious odd byte status on 13183A EOF
  > modified handling of end of medium
  > added detailed timing, with fast and realistic modes
  > added reel sizes to simulate end of tape
  > added debug printouts

- Modified MT handling of end of medium

- Added tab to TTY control char set

2.3 VAX

- VAX RQ controllers start LUNs at 0 (from Andreas Cejna)
- Added compatibility mode definitions
- Fixed EMODD, EMODG to probe second longword of quadword destination
