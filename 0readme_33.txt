Notes For V3.3

RESTRICTION: The HP DS disk is not debugged.  DO NOT enable this
feature for normal operations.
WARNING: Massive changes in the PDP-11 make all previous SAVEd
file obsolete.  Do not attempt to use a PDP-11 SAVE file from a
prior release with V3.3!

1. New Features in 3.3

1.1 SCP

- Added -p (powerup) qualifier to RESET
- Changed SET <unit> ONLINE/OFFLINE to SET <unit> ENABLED/DISABLED
- Moved SET DEBUG under SET CONSOLE hierarchy
- Added optional parameter value to SHOW command
- Added output file option to SHOW command

1.2 PDP-11

- Separated RH Massbus adapter from RP controller
- Added TU tape support
- Added model emulation framework
- Added model details

1.3 VAX

- Separated out CVAX-specific features from core instruction simulator
- Implemented capability for CIS, octaword, compatibility mode instructions
- Added instruction display and parse for compatibility mode
- Changed SET CPU VIRTUAL=n to SHOW CPU VIRTUAL=n
- Added =n optional parameter to SHOW CPU HISTORY

1.4 Unibus/Qbus simulators (PDP-11, VAX, PDP-10)

- Simplified DMA API's
- Modified DMA peripherals to use simplified API's

1.5 HP2100 (all changes from Dave Bryan)

CPU	- moved MP into its own device; added MP option jumpers
	- modified DMA to allow disabling
	- modified SET CPU 2100/2116 to truncate memory > 32K
	- added -F switch to SET CPU to force memory truncation
	- modified WRU to be REG_HRO
	- added BRK and DEL to save console settings

DR	- provided protected tracks and "Writing Enabled" status bit
	- added "parity error" status return on writes for 12606
	- added track origin test for 12606
	- added SCP test for 12606
	- added "Sector Flag" status bit
	- added "Read Inhibit" status bit for 12606
	- added TRACKPROT modifier

LPS	- added SET OFFLINE/ONLINE, POWEROFF/POWERON
	- added fast/realistic timing
	- added debug printouts

LPT	- added SET OFFLINE/ONLINE, POWEROFF/POWERON

PTR	- added paper tape loop mode, DIAG/READER modifiers to PTR
	- added PV_LEFT to PTR TRLLIM register

CLK	- modified CLK to permit disable

1.6 IBM 1401, IBM 1620, Interdata 16b, SDS 940, PDP-10

- Added instruction history

1.7 H316, PDP-15, PDP-8

- Added =n optional value to SHOW CPU HISTORY

2. Bugs Fixed in 3.3

2.1 SCP

- Fixed comma-separated SET options (from Dave Bryan)
- Fixed duplicate HELP displays with user-specified commands

2.2 PDP-10

- Replicated RP register state per drive
- Fixed TU to set FCE on short record
- Fixed TU to return bit<15> in drive type
- Fixed TU format specification, 1:0 are don't cares
- Fixed TU handling of TMK status
- Fixed TU handling of DONE, ATA at end of operation
- Implemented TU write check

2.3 PDP-11

- Replicated RP register state per drive
- Fixed RQ, TQ to report correct controller type and stage 1 configuration
  flags on a Unibus system
- Fixed HK CS2<output_ready> flag

2.4 VAX

- Fixed parsing of indirect displacement modes in instruction input

2.5 HP2100 (all fixes from Dave Bryan)

CPU	- fixed S-register behavior on 2116
	- fixed LIx/MIx behavior for DMA on 2116 and 2100
	- fixed LIx/MIx behavior for empty I/O card slots

DP	- fixed enable/disable from either device
	- fixed ANY ERROR status for 12557A interface
	- fixed unattached drive status for 12557A interface
	- status cmd without prior STC DC now completes (12557A)
	- OTA/OTB CC on 13210A interface also does CLC CC
	- fixed RAR model
	- fixed seek check on 13210 if sector out of range

DQ	- fixed enable/disable from either device
	- shortened xtime from 5 to 3 (drive avg 156KW/second)
	- fixed not ready/any error status
	- fixed RAR model

DR	- fixed enable/disable from either device
	- fixed sector return in status word
	- fixed DMA last word write, incomplete sector fill value
	- fixed 12610 SFC operation
	- fixed current-sector determination

IPL	- fixed enable/disable from either device

LPS	- fixed status returns for error conditions
	- fixed handling of non-printing characters
	- fixed handling of characters after column 80
	- improved timing model accuracy for RTE

LPT	- fixed status returns for error conditions
	- fixed TOF handling so form remains on line 0

SYS	- fixed display of CCA/CCB/CCE instructions

2.5 PDP-15

FPP	- fixed URFST to mask low 9b of fraction
	- fixed exception PC setting
