# DDCMP Framer

The "DDCMP framer" supported by the DUP and DMC device emulations, is a USB connected device that implements synchronous links for use by DDCMP.

When used with SIMH, the framer allows an emulated DMC-11 or DUP-11 (including KMC/DUP) to be connected to a system that uses a real synchronous link for DDCMP, for example an actual DMC-11 device.  The framer can support either RS-232 connectons for the DUP-11 or DMC-11 "remote" line cards, or the "integral modem" coaxial cable connection implemented in the DMC "local" line card.  Speeds in the range 500 to 56k bits per second (for RS-232) or 56k to 1M bits per second (for integral modem) are supported.

The DDCMP framer is an open source design.  All the design files (schematics, circuit board layout, firmware, and documentation) may be found on GitHub at  [this link](https://github.com/pkoning2/ddcmp "DDCMP Framer on GitHub").
