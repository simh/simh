Notes For V3.4-0

The memory layout for the Interdata simulators has been changed.
Do not use Interdata SAVE files from prior revisions with V3.4.

1. New Features in 3.4

1.1 SCP and Libraries

- Revised interpretation of fprint_sym, fparse_sym returns
- Revised syntax for SET DEBUG
- DO command nesting allowed to ten levels

1.2 Interdata

- Revised memory model to be 16b instead of 8b

1.3 HP2100

- Added Fast FORTRAN Processor instructions
- Added SET OFFLINE/ONLINE and SET UNLOAD/LOAD commands to tapes and disks

2. Bugs Fixed in 3.4-0

2.1 Interdata

- Fixed bug in show history routine (from Mark Hittinger)
- Fixed bug in initial memory allocation

2.2 PDP-10

- Fixed TU bug, ERASE and WREOF should not clear done (reported by
  Rich Alderson)
- Fixed TU error reporting

2.3 PDP-11

- Fixed TU error reporting
