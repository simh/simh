Notes For V3.2-2

RESTRICTION: The PDP-15 FPP is only partially debugged.  Do NOT
enable this feature for normal operations.
RESTRICTION: The HP DS disk is not debugged.  DO NOT enable this
feature under any circumstances.

1. New Features in 3.2-2

None

2. Bugs Fixed in 3.2-2

2.1 SCP

- Fixed problem ATTACHing to read-only files (found by John Dundas)
- Fixed problems in Windows terminal code (found by Dave Bryan)
- Fixed problem in big-endian reads (reported by Scott Bailey)

2.2 GRI-909

- Updated MSR to include SOV
- Updated EAO to include additional functions

2.2 HP2100 (from Dave Bryan)

- Generalized control character handling for console terminal

2.3 VAX

- Fixed bad block initialization routine

