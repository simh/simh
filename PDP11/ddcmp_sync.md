# DDCMP synchronous line interface

The DDCMP synchronous interface supported by the DUP and DMC device emulations, is a USB connected device that implements synchronous links for use by DDCMP.

When used with SIMH, the synchronous interface allows an emulated DMC-11 or DUP-11 (including KMC/DUP) to be connected to a system that uses a real synchronous link for DDCMP, for example an actual DMC-11 device.  The interface can support either RS-232 connectons for the DUP-11 or DMC-11 "remote" line cards, or the "integral modem" coaxial cable connection implemented in the DMC "local" line card.  Speeds in the range 500 to 56k bits per second (for RS-232) or 56k to 1M bits per second (for integral modem) are supported.

The DDCMP synchronous line interface is an open source design.  All the design files (schematics, circuit board layout, firmware, and documentation) may be found on GitHub at  [this link](https://github.com/pkoning2/ddcmp "DDCMP Framer on GitHub").

Note that at the moment, no boards, kits, or assembled units are offered.  If you want a framer, you would need to obtain a circuit board and assemble it.  The documentation in the GitHub design files explains how to do this.  The design uses all "through hole" parts, so modest electronics assembly skills suffice to build a unit.
