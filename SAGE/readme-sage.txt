This is version 0.5 of a simulator for the SAGE-68K computer. See www.sageandstride.org for details.

This is called Version 0.5 because it still lacks a few things and has a number of known bugs.

Features and problems
- currently is at the level of a SAGE-II system with two floppy drives
- Currently runs CP/M-68K 1.2 (IMD-Disk included)
- Console and SIO can be redirected to a telnet session
- does not run UCSD-Pascal yet (in progress)
- does not support IEEE-interface yet (and maybe won't ever - not really useful)
- does not support Winchester operation yet, although BIOS ROMs are included
- m68k_cpu.c has a number of not yet implemented instructions (although sufficient for CP/M-68K!)
  (implementation in progress)
- does not yet fully support 68010 CPU (in progress, not needed for Sage, though)
- does not implement 68881 FPU (in progress, not needed for Sage, though)
- has stubs for MMU integration, but does not yet implement one - passthrough (in progress, not needed for Sage, though)

- still contains some timing bug in floppy operation (timing loop, 8253 emulation, IRQ speed) which
  results in rather long floppy recognition time (disk change), after that I/O is at acceptable speed
- probably there is still a bug in console/sio telnet handling when the character buffer
  is full (no automatic draining, will be investigated)
- no optimization of simulation speed at all, but runs acceptable with current PCs.
- not yet tested under anything else than MINGW

Holger Veit, March 2011



$ BIN/sage

Sage-II/IV 68k simulator V3.8-2
sim> show dev
Sage-II/IV 68k simulator configuration

CPU, BIOS=sage-ii.hex
PIC, I/O=0xFFC041-0xFFC043
TIMER1, I/O=0xFFC001-0xFFC007
TIMER2, I/O=0xFFC081-0xFFC087
DIP, I/O=0xFFC021-0xFFC027, GROUPA=11100111, GROUPB=11111000
FD, I/O=0xFFC051-0xFFC053, 2 units
CONS, I/O=0xFFC071-0xFFC073, 2 units
SIO, I/O=0xFFC031-0xFFC033, 2 units
LP, I/O=0xFFC061-0xFFC067
sim> quit
Goodbye
Debug output disabled

$ cp SAGE/FILES/68k.sim .
$ cp SAGE/FILES/cpm68k12.imd .
$ cp SAGE/sage-ii.hex .
$ BIN/sage 68k.sim

Sage-II/IV 68k simulator V3.8-2
Debug output to "debug.log"
Loading boot code from sage-ii.hex

SAGE II Startup Test [1.2]

RAM Size = 512K

 Booting from Floppy

SAGE CP/M-68k Bootstrap v2.1

SAGE CP/M-68k v1.2     447K TPA

A>STARTUP

A>SETENV TERM TVI950

A>SETENV PATH |A0:

A>dir
A: MINCE    SWP : MINCE    68K : CPM      SYS : SAGEBIOS SYS : PIP      68K
A: STAT     68K : AR68     68K : LO68     68K : AS68     68K : MIND     SUB
A: DDT      68K : SAGE4UTL 68K : INIT     68K : DUMP     68K : COPY     68K
A: DDT68000 68K : P        SUB : ASGO     SUB : PE       SUB : AS       SUB
A: LNK      SUB : M        SUB : ARMATH   SUB : FIND     68K : RED      SUB
A: SCREEN   68K : MCC      SUB : LINKCORE SUB : SETPRNTR 68K : AS68SYMB DAT
A: E        SUB : REDASM   SUB : CORE     SUB : PRINT    68K : SETENV   68K
A: STARTUP  SUB : HALT     68K : SPACE    SUB : SIG      TXT : SPACEM   SUB
A: ORBIT    SUB : TLNK     SUB : BRWNIES  TXT
A>stat a:

A: RW, FREE SPACE:         0K
A>^E
Simulation stopped, PC: 0007C8C4 (stop #2000)
sim>quit
Goodbye
Debug output disabled

