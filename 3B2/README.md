AT&T 3B2 Simulator
==================

This module contains the source for two simulators:

1. A simulator for the AT&T 3B2/400 computer (3b2-400 or 3B2-400.EXE)
2. A simulator for the AT&T 3B2/700 computer (3b2-700 or 3B2-700.EXE)

Full documentation for the 3B2 simulator is available here:

  - https://loomcom.com/3b2/emulator.html

3B2/400 Simulator Devices
-------------------------

The following devices are simulated. The SIMH names for the simulated
devices are given in parentheses:

  - 3B2 Model 400 System Board with 1MB, 2MB, or 4MB RAM
  - Configuration and Status Register (CSR)
  - WE32100 CPU (CPU)
  - WE32101 MMU (MMU)
  - WE32106 Math Accelerator Unit (MAU)
  - PD8253 Interval Timer (TMR)
  - AM9517 DMA controller (DMAC)
  - SCN2681A Integrated DUART (IU)
  - TMS2793 Integrated Floppy Controller (IFLOPPY)
  - uPD7261A Integrated MFM Fixed Disk Controller (IDISK)
  - Non-Volatile Memory (NVRAM)
  - MM58174A Time Of Day Clock (TOD)
  - CM195A Ethernet Network Interface (NI)
  - CM195B 4-port Serial MUX (PORTS)
  - CM195H Cartridge Tape Controller (CTC)

3B2/700 Simulator Devices
-------------------------

The following devices are simulated. The SIMH names for the simulated
devices are given in parentheses:

  - 3B2 Model 700 System Board with 8MB, 16MB, 32MB, or 64MB RAM
  - Configuration and Status Registers (CSR)
  - WE32200 CPU (CPU)
  - WE32201 MMU (MMU)
  - WE32106 Math Accelerator Unit (MAU)
  - PD8253 Interval Timer (TMR)
  - AM9517 DMA controller (DMAC)
  - SCN2681A Integrated DUART (IU)
  - TMS2793 Integrated Floppy Controller (IFLOPPY)
  - Non-Volatile Memory (NVRAM)
  - MM58274C Time Of Day Clock (TOD)
  - CM195W SCSI Host Adapter (SCSI)
  - CM195A Ethernet Network Interface (NI)
  - CM195B 4-port Serial MUX (PORTS)
