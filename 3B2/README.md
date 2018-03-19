AT&T 3B2 Simulator
==================

This module contains a simulator for the AT&T 3B2 Model 400 microcomputer.

Devices
-------

The following devices are simulated. The SIMH names for the simulated
devices are given in parentheses:

  - 3B2 Model 400 System Board with 1MB, 2MB, or 4MB RAM (CSR, NVRAM)
  - WE32100 CPU (CPU)
  - WE32101 MMU (MMU)
  - PD8253 Interval Timer (TIMER)
  - AM9517 DMA controller (DMAC)
  - SCN2681A Integrated DUART (IU)
  - TMS2793 Integrated Floppy Controller (IF)
  - uPD7261A Integrated MFM Fixed Disk Controller (ID)
  - MM58174A Time Of Day Clock (TOD)

Usage
-----

To boot the 3B2 simulator into firmware mode, simply type:

    sim> BOOT CPU

You will be greeted with the message:

    FW ERROR 1-01: NVRAM SANITY FAILURE
                   DEFAULT VALUES ASSUMED
                   IF REPEATED, CHECK THE BATTERY

    FW ERROR 1-02: DISK SANITY FAILURE
                   EXECUTION HALTED

    SYSTEM FAILURE: CONSULT YOUR SYSTEM ADMINISTRATION UTILITIES GUIDE

NVRAM and Time of Day can be saved between boots by attaching both
devices to files.

    sim> ATTACH NVRAM <nvram-file>
    sim> ATTACH TOD <tod-file>

If you have no operating system installed on the hard drive, on
subsequent boots you will instead see the message

    SELF-CHECK


    FW ERROR 1-02: DISK SANITY FAILURE
                   EXECUTION HALTED

    SYSTEM FAILURE: CONSULT YOUR SYSTEM ADMINISTRATION UTILITIES GUIDE


Once you see the `SYSTEM FAILURE` message, this is actually an
invisible prompt. To access firmware mode, type the default 3B2
firmware password `mcp`, then press Enter or carriage return.

You should then be prompted with:

    Enter name of program to execute [  ]:

Here, you may type a question mark (?) and press Enter to see a list
of available firmware programs.

Booting UNIX SVR3
-----------------

UNIX SVR3 is the only operating system available for the 3B2.  To boot
UNIX, attach the first disk image from the 3B2 "Essential Utilities"
distribution.

    sim> ATTACH IF <floppy-image>
    sim> BOOT CPU

Once you reach the `SYSTEM FAILURE` message, type `mcp` to enter
firmware mode. When prompted for the name of a program to boot, enter
`unix`, and confirm the boot device is `FD5` by pressing Enter or
carriage return.

    Enter name of program to execute [  ]: unix
            Possible load devices are:

    Option Number    Slot     Name
    ---------------------------------------
           0          0     FD5

    Enter Load Device Option Number [0 (FD5)]:

Installing SVR3
---------------

To install SVR3 to the first hard disk, first, attach a new image
to the ID0 device:

    sim> ATTACH ID0 <hd-image>

Then, boot the file `idtools` from the "3B2 Maintenance Utilities -
Issue 4.0" floppy diskette.

From `idtools`, select the `formhard` option and low-level format
integrated disk 0. Parameters are:

                   Drive Id: 5
           Number cylinders: 925
          Number tracks/cyl: 9
       Number sectors/track: 18
        Number bytes/sector: 512

After low-level formatting integrated disk 0, boot the file `unix`
from the first diskette of the 3B2 "Essential Utilities" distribution,
and follow the prompts.
