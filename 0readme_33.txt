Notes For V3.3-2

1. New Features in 3.3-2

1.1 SCP and Libraries

- Added ASSERT command (from Dave Bryan)

1.2 PDP-11, VAX

- Added RA60, RA71, RA81 disks

2. Bugs Fixed in 3.3-2

2.1 H316

- Fixed IORETURN macro
- PT: fixed bug in OCP '0001 (found by Philipp Hachtmann)
- MT: fixed error reporting from OCP (found by Philipp Hachtmann)

2.2 Interdata 32b

- Fixed branches to mask new PC (from Greg Johnson)

2.3 PDP-11

- Fixed bugs in RESET for 11/70 (reported by Tim Chapman)
- Fixed bug in SHOW MODEL (from Sergey Okhapkin)
- Made SYSID variable for 11/70 (from Tim Chapman)
- Fixed MBRK write case for 11/70 (from Tim Chapman)
- RY: fixed bug in boot code (reported by Graham Toal)

2.4 VAX

- Fixed initial state of cpu_extmem

2.5 HP2100 (from Dave Bryan)

- Fixed missing MPCK on JRS target
- Removed EXECUTE instruction (is NOP in actual microcode)
- Fixed missing negative overflow renorm in StoreFP

2.6 I1401

- Fixed bug in line printer write line (reported by Van Snyder)


