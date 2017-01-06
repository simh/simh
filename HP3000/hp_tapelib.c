/* hp_tapelib.c: HP magnetic tape controller simulator library

   Copyright (c) 2013-2016, J. David Bryan
   Copyright (c) 2004-2011, Robert M. Supnik

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   01-Jul-16    JDB     Changed tl_attach to reset the event delay times pointer
   09-Jun-16    JDB     Added casts for ptrdiff_t to int32 values
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     TAPELIB_PROPERTIES.action is now a pointer-to-constant
   13-May-16    JDB     Modified for revised SCP API function parameter types
   03-May-16    JDB     Changed clear/attach/on/offline trace from INCO to CMD
   24-Mar-16    JDB     Changed the buffer element type from uint8 to TL_BUFFER
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   20-Nov-15    JDB     First release version
   24-Mar-13    JDB     Created tape controller common library from MS simulator

   References:
     - 13181B Digital Magnetic Tape Unit Interface Kit Operating and Service Manual
         (13181-90901, November 1982)
     - 13183B Digital Magnetic Tape Unit Interface Kit Operating and Service Manual
         (13183-90901, November 1983)
     - 30115A Nine-Track (NRZI-PE) Magnetic Tape Subsystem Maintenance Manual
         (30115-90001, June 1976)
     - 30115A Nine-Track (NRZI-PE) Magnetic Tape Subsystem Microprogram Listing
         (30115-90005, January 1974)
     - Standard ECMA-12 for Data Interchange on 9-Track Magnetic Tape
         (June 1970)


   This library provides the common functions required by the device controllers
   for the HP 7970B and 7970E tape drives.  It implements the command sets of
   the 13181 and 13183 controllers for the HP 1000, and the 30215 controller for
   the HP 3000.

   The library is an adaptation of the code originally written by Bob Supnik for
   the HP2100 MS simulator.  MS simulates an HP 13181 or 13183 controller
   interface for an HP 1000 computer.  To create the library, the functions of
   the controller were separated from the functions of the interface.  This
   allows the library to work with other CPUs, such as the 30215 interface for
   the HP 3000, that use substantially different communication protocols.  The
   library functions implement the controller command set for the drive units.
   The interface functions handle the transfer of commands and data to and from
   the CPU.

   In contrast with the 13037 MAC disc controller, HP had no standard tape
   controller that was shared between computer families.  Instead, there were
   several similar but incompatible controller implementations.  Command sets,
   command encoding, status encoding, status responses, and controller hardware
   design varied from implementation to implementation, with each tied tightly
   to the specific machine for which it was created.

   Therefore, to provide a "universal" tape controller that may be shared
   between the 1000 and 3000 simulators, this library implements an abstract
   controller modeled on the interface design of the 13037.  A tape interface
   interacts with the controller over 16-bit data, flag, and function buses.
   Commands, status, and data are exchanged across the data bus, with the flag
   bus providing indications of the state of the interface and the function bus
   indicating what actions the interface must take in response to command
   processing by the controller.

   A device interface simulator interacts with the tape controller simulator via
   the "tl_controller" routine, which simulates the signal interconnections
   between the interface and controller.  Utility routines are also provided to
   attach and detach tape image files from drive units, set units offline or
   online, set drive model and protection status, and select the controller
   timing mode (real or fast).  The controller routine is called by the
   interface whenever the state of the flag bus changes, either due to a request
   from the CPU or a service event, and the controller responds by potentially
   changing the data and function buses.  The interface merely responds to those
   changes without requiring any other knowledge of the internal state of the
   controller.

   The interface initiates controller action by placing data on the data bus and
   then changing the state of the flag bus, and the controller responds by
   asserting one or more functions on the function bus.  For example, the
   interface starts a "Write Record" tape command by placing the command on the
   data bus and asserting CMRDY (Command Ready) and CMXEQ (Command Execute) on
   the flag bus.  The controller responds by decoding the command, initiating
   processing, and then placing the IFGTC (Interface Get Command) and RQSRV
   (Request Service) functions on the function bus.  The interface responds by
   clearing the transfer flags in preparation for the data transfer and
   requesting service from the DMA channel.  Once the controller determines that
   the tape is moving and is positioned correctly for the write, an event
   service entry calls the controller with the DTRDY (Data Ready) flag and the
   first word of data to write.  The controller responds by saving the word to
   the record buffer and asserting the IFOUT (Interface Out) and RQSRV
   functions.  The interface responds by clearing the DTRDY flag and requesting
   the next word from the DMA channel.

   Hardware wait loops in the abstract controller that wait for tape motion are
   simulated by timed events.  The tl_controller routine activates the selected
   unit and returns to the caller until the expected external event occurs.  In
   the Write Record example, the controller will wait for the simulated tape to
   start and position the record for writing and then will wait for each data
   word to be supplied by the interface.

   The controller supports realistic and optimized (fast) timing modes.
   Realistic timing attempts to model the actual controller and tape unit motion
   delays inherent in tape operations.  For example, in REALTIME mode, reads of
   longer records take more time than reads of shorter records, rewinds take
   times proportional to the distance from the load point, and an erase gap is
   written before the first record after the load point.  In FASTTIME mode, all
   timings are reduced to be "just long enough" to satisfy software
   requirements, and movement across erase gaps takes no additional time.

   Typically, a total operation time consists of a controller overhead delay, a
   tape transport start time, an optional gap traverse time, a data record
   traverse time, and a transport stop time.  These times are modeled by the
   service activation times for the various command phases.

   A controller instance is represented by a CNTLR_VARS structure, which
   maintains the controller's internal state.  Each 13181/3 and 30215 interface
   has a single controller instance that controls up to four drive units,
   whereas an HP-IB interface will have one controller instance per drive unit.
   The minor differences in controller action between the two are handled
   internally.

   The 1000/3000 interface simulators must declare one unit for each tape drive
   to be controlled by the library, plus one additional unit for the controller
   itself.  For an HP-IB controller, only one unit is required.

   The controller maintains five values in each drive's unit structure:

     u3 (PROP)   -- the current drive properties
     u4 (STATUS) -- the current drive status
     u5 (OPCODE) -- the current drive operation in progress
     u6 (PHASE)  -- the current drive operation phase
     pos         -- the current byte offset into the tape image file

   These and other definitions are in the file hp_tapelib.h, which must be
   included in the interface simulator.

   The drive status field contains only a subset of the status maintained by
   drives in hardware.  Specifically, the Write Protected, Write Status, and
   1600-bpi Density bits are stored in the status field.  The other bits (End of
   Tape, Unit Busy, Unit Ready, Load Point, Rewinding, and Unit Offline) are set
   dynamically whenever status is requested.

   Per-drive opcode and phase values allow rewinds to be overlapped with
   operations on other drives.  For example, a Rewind issued to unit 0 may be
   followed by a Read Record issued to unit 1.  When the rewind completes on
   unit 0, its opcode and phase values will let the controller set the
   appropriate completion status without disturbing the values currently in use
   by unit 1.

   The simulation defines these command phases:

     Idle     -- waiting for the next command to be issued
     Wait     -- waiting for the channel data transfer
     Error    -- waiting to interrupt for a command abort
     Start    -- waiting for the drive to come up to speed after starting
     Traverse -- waiting for the drive to traverse an erase gap
     Data     -- waiting for the drive to traverse a data record
     Stop     -- waiting for the drive to slow to a stop

   A value represents the current state of the unit.  If a unit is active, the
   phase will end when the unit is serviced.

   During reads and forward/backward spacing commands, each data record may be
   optionally preceded in the direction of motion by an erase gap.  If no gap is
   present, the associated traversal phase is skipped, and service proceeds
   directly from the start phase to the data phase.

   In-progress operations may be cancelled by issuing a controller clear.  In
   hardware, this causes tape motion to cease immediately, except for rewind
   operations, which continue to the load point.  Under simulation, a write-in-
   progress is terminated with the data received so far written as a truncated
   record.  Additionally, for a NRZI drive, the record is marked as "bad" due to
   the lack of a CRC character.  A read- or spacing-in-progress is abandoned if
   the clear occurs during the controller overhead or transport start period or
   is terminated with partial movement if it occurs during the gap traverse
   period.  A read- or spacing-in-progress is completed normally if the clear
   occurs during the data record traverse, as the SIMH magnetic tape format has
   no provision for recovery when positioned within a record.

   In addition to the controller structure(s), an interface declares a data
   buffer to be used for record transfers.  The buffer is an array containing
   TL_BUFSIZE 8-bit elements; the address of the buffer is stored in the
   controller state structure.  The controller maintains the current index into
   the buffer, as well as the length of valid data stored there.  Only one
   buffer is needed per interface, regardless of the number of controllers or
   units handled, as a single interface cannot perform data transfers on one
   drive concurrently with a command directed to another drive.

   An interface is also responsible for declaring a structure of type
   DELAY_PROPS that contains the timing values for the controller when in
   FASTTIME mode.  The values are event counts for these actions:

     - the time from rewind initiation to controller idle
     - the travel time per inch during rewinding
     - the time from BOT detection to load point search completion
     - the time starting from the BOT marker to the data block
     - the time starting from the IR gap to the data block
     - the travel time from one data byte to the next
     - the controller execution overhead time from command to first motion

   These values are typically exposed via the interface's register set and so
   may be altered by the user.  A macro, DELAY_INIT, is provided to initialize
   the structure.

   In addition, certain actions are enabled only when the controller is in
   realistic or optimized timing mode.  Specifically, when in REALTIME mode, the
   controller will:

     - stop at a proportional position if it is cleared during a gap traversal
     - calculate and add the CRCC and LRCC to the record buffer for 13181 reads

   In FASTTIME mode, the controller will:

     - omit writing the gap normally required at the beginning of the tape
     - omit writing the gap for the "Write Gap and File Mark" command
     - omit the gap traversal time for reads and spacings

   The controller library provides a macro, TL_MODS, that initializes MTAB
   entries.  A macro, CNTLR_INIT, is provided to initialize the controller
   structure from the following parameters:

     - the type of the controller
     - the simulation DEVICE structure on which the controller operates
     - the data buffer array
     - the structure containing the FASTTIME values

   A macro, TL_REGS, is also provided that initializes a set of REG structures
   to provide a user interface to the controller structure.

   In hardware, the 3000 controller detects a tape unit offline-to-online
   transition and responds by requesting a CPU interrupt.  In simulation, SET
   <dev> OFFLINE and SET <dev> ONLINE commands represent these actions.  The
   controller must be notified by calling the tl_onoffline routine in response
   to changes in a tape drive's ONLINE switch.

   Finally, the controller library provides extensive tracing of its internal
   operations via debug logging.  Six debug flags are declared for use by the
   interface simulator.  When enabled, these report controller actions at
   various levels of detail.

   The simulator tape support library (sim_tape) routines are used to implement
   the low-level tape image file handling.  Fatal errors from these routines are
   reported to the simulation console and cause a simulation stop.  If
   simulation is resumed, the controller command in progress is aborted with an
   uncorrectable data error indication.  Typically, the program executing under
   simulation reacts to this by retrying the operation.


   Implementation notes:

    1. The library does not simulate the optional OFF-0-1-2-3 Unit Select
       buttons of the 7970-series drives.  Instead, each drive adopts the unit
       address corresponding to the simulator UNIT number associated with the
       drive.  For example, unit MS2 responds to unit address 2.

       A set of bits to represent the Unit Select switch setting has been
       allocated in the PROP field of the UNIT structure, but it is currently
       unused.  This feature could be added by reading the unit number from
       there, rather than deriving it from the position in the device's UNIT
       array, and adding SET <unit> UNIT=OFF|0|1|2|3 and SHOW <unit> UNIT
       commands and associated validation and print routines.  However, the
       operational benefit of this, compared to the current approach, is nil, as
       the only utility derived would be a somewhat cleaner set of test commands
       for the magnetic tape diagnostic.

       The current unit select diagnostic approach moves the scratch tape
       attachment from drive to drive in lieu of changing the Unit Select switch
       of a single drive.  This passes both the 1000 and 3000 diagnostics,
       although reattaching does reset the tape position to the load point.
       Fortunately, continuance of tape position isn't tested.

    2. The library does not properly simulate the HP 30215 tape controller's
       Write_Record_without_Parity (WRZ) and Read_Record_with_CRCC (RDC)
       commands, which are used only with NRZI 7970B drives.  In hardware, WRZ
       forces the parity of data bytes written to the tape to zero, allowing the
       creation of records containing dropouts, bad parity, and spacing gaps,
       and RDC returns two extra bytes containing the CRCC and its parity bit
       read from the tape.

       Because the tape image format contains only the data content of records,
       under simulation WRZ writes all received bytes to a data record, except
       that a parity error is indicated if a byte has even parity, and zero
       bytes (dropouts) are omitted.  RDC is simulated by returning the
       calculated CRC of the record.

       It would be possible to simulate these functions correctly if the tape
       image format is extended to allow private data.  For example, the WRZ
       command would write a private data record containing all bytes, and RDC
       would read the record and interpret the data accordingly.  Reading a
       standard record with RDC would return a calculated CRCC.  However, the
       operational benefit of this is nil, as only the diagnostic uses these
       commands, and then only to verify that the CRCC, LRCC, and dropout
       detections are working properly.  These functions are irrelevant in a
       simulation environment.

    3. In hardware, the transport stop time will be eliminated if another
       command of the proper type is received in time.  If a new command is in
       the same command group (read forward, read reverse, or write forward),
       and it follows within 103 microseconds of the previous command, then tape
       motion will continue.  Otherwise, it will stop and restart.

       In simulation, all tape commands complete the stop phase before the next
       command is started.

    4. The SAVE command does not save the "wait" and "pos" fields of the UNIT
       structure automatically.  To ensure that they are saved, they are
       referenced by read-only registers.
*/



#include "hp_tapelib.h"



/* Program constants */

#define NO_EVENT            -1                  /* do not schedule an event */

#define NO_ACTION           (CNTLR_IFN_IBUS) (NO_FUNCTIONS | NO_DATA)
#define SCP_STATUS(w)       (CNTLR_IFN_IBUS) (SCPE | (w))


/* Controller unit pointer */

#define CNTLR_UPTR          (cvptr->device->units + TL_CNTLR_UNIT)


/* Unit flags accessor */

#define GET_MODEL(f)        (DRIVE_TYPE) ((f) >> UNIT_MODEL_SHIFT & UNIT_MODEL_MASK)


/* Per-unit property flags and accessors.

   The property value (PROP) contains several fields that describe the drive and
   its currently mounted tape reel:

                 15| 14  13  12| 11  10  9 | 8   7   6 | 5   4   3 | 2   1   0
           +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     [...]   - |   model   | unit num  | reel  |     property array index      |
           +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The property array index is the element number of the drive_props array that
   describes the drive associated with the unit number.  The reel size value is
   one of the REEL_SIZE enumeration constants that reflects the specified length
   of the tape reel mounted to the unit.

   The unit number and model are not currently used but are reserved for future
   expansion.


   Implementation notes:

    1. Unit attention could be kept in the property value, rather than as a flag
       in the bitmap controller field "unit_attention".  However, keeping the
       attention flags together means that they can be checked very quickly,
       rather than having to isolate and check a bit in each UNIT structure.
*/

#define PROP_INDEX_WIDTH    8                   /* tape drive property array index */
#define PROP_REEL_WIDTH     2                   /* mounted tape reel size */
#define PROP_UNIT_WIDTH     3                   /* tape drive unit number */
#define PROP_MODEL_WIDTH    3                   /* tape drive model */

#define PROP_INDEX_SHIFT    0                   /* bits 7-0 */
#define PROP_REEL_SHIFT     8                   /* bits 9-8 */
#define PROP_UNIT_SHIFT     10                  /* bits 12-10 */
#define PROP_MODEL_SHIFT    13                  /* bits 15-13 */

#define PROP_INDEX_MASK     ((1u << PROP_INDEX_WIDTH) - 1 << PROP_INDEX_SHIFT)
#define PROP_REEL_MASK      ((1u << PROP_REEL_WIDTH)  - 1 << PROP_REEL_SHIFT)
#define PROP_UNIT_MASK      ((1u << PROP_UNIT_WIDTH)  - 1 << PROP_UNIT_SHIFT)
#define PROP_MODEL_MASK     ((1u << PROP_MODEL_WIDTH) - 1 << PROP_MODEL_SHIFT)

#define PROP_INDEX(u)       (((u)->PROP & PROP_INDEX_MASK) >> PROP_INDEX_SHIFT)
#define PROP_REEL(u)        (((u)->PROP & PROP_REEL_MASK)  >> PROP_REEL_SHIFT)
#define PROP_UNIT(u)        (((u)->PROP & PROP_UNIT_MASK)  >> PROP_UNIT_SHIFT)
#define PROP_MODEL(u)       (((u)->PROP & PROP_MODEL_MASK) >> PROP_MODEL_SHIFT)

#define PROP_REEL_UNLIM     (Reel_Unlimited << PROP_REEL_SHIFT)
#define PROP_REEL_600       (Reel_600_Foot  << PROP_REEL_SHIFT)
#define PROP_REEL_1200      (Reel_1200_Foot << PROP_REEL_SHIFT)
#define PROP_REEL_2400      (Reel_2400_Foot << PROP_REEL_SHIFT)


/* Magnetic tape reel sizes */

typedef enum {
    Reel_Unlimited = 0,
    Reel_600_Foot,
    Reel_1200_Foot,
    Reel_2400_Foot
    } REEL_SIZE;


/* Unit command phases.

   Command phases indicate the state of the unit.  If an event is currently
   scheduled, the phase indicates the unit state as it will be on entry to the
   event service routine.  For example, upon event expiration, a scheduled unit
   in the start phase will have started, will be up to speed, and will have
   passed through the interrecord gap and be ready to transfer data.  The start
   phase activation delay represents the time required for these actions to
   occur.
*/

typedef enum {
    Idle_Phase = 0,                             /* waiting for the next command to be issued */
    Wait_Phase,                                 /* waiting for the channel data transfer */
    Start_Phase,                                /* waiting for the drive to come up to speed after starting */
    Traverse_Phase,                             /* waiting for the drive to traverse an erase gap */
    Data_Phase,                                 /* waiting for the drive to traverse a data record */
    Stop_Phase,                                 /* waiting for the drive to slow to a stop */
    Error_Phase                                 /* waiting to interrupt for a command abort */
    } CNTLR_PHASE;


/* Drive properties table.

   The drive properties table is used to validate drive type and density changes
   within the subset of drives supported by a given controller.  It also
   contains the erase gap size, which is controller-specific.
*/

typedef struct {
    CNTLR_TYPE  controller;                     /* the controller model */
    DRIVE_TYPE  drive;                          /* a supported tape drive model */
    uint32      density;                        /* a supported tape drive density code */
    uint32      bpi;                            /* the recording density in bits per inch */
    uint32      gap_size;                       /* the erase gap size in tenths of an inch */
    } DRIVE_PROPS;

static const DRIVE_PROPS drive_props [] = {
/*                                               gap  */
/*     cntrlr      drive    density code   bpi   size */
/*    ---------  ---------  ------------  -----  ---- */
    { HP_13181,  HP_7970B,  MT_DENS_800,   800,   48  },
    { HP_13183,  HP_7970E,  MT_DENS_1600, 1600,   30  },
    { HP_30215,  HP_7970B,  MT_DENS_800,   800,   38  },
    { HP_30215,  HP_7970E,  MT_DENS_1600, 1600,   38  },
    { HP_IB,     HP_7974,   MT_DENS_800,   800,    0  },
    { HP_IB,     HP_7974,   MT_DENS_1600, 1600,    0  }
    };

#define PROPS_COUNT         (sizeof drive_props / sizeof drive_props [0])


/* Delay properties table.

   To support the realistic timing mode, the delay properties table contains
   timing specifications for the supported tape drives.  Table entries
   correspond one-for-one with drive properties table entries.

   The times represent the delays for mechanical and electronic operations.
   Delay values are stored in event tick counts; macros are used to convert from
   times to ticks.

   The rewind start time is the delay from rewind command initiation until the
   controller is released.  The rewind stop time is the time to stop the reverse
   motion plus the time to search forward to the load point.

   The controller overhead values are estimates; they do not appear to be
   documented for any of the controllers.
*/

static const DELAY_PROPS real_times [] = {
/*     rewind    rewind     rewind       BOT         IR      per-byte     cntlr   */
/*     start    time/inch    stop       time        time     data xfer   overhead */
/*    --------  ---------  --------  -----------  ---------  ----------  -------- */
    { uS (556), mS (6.25), S (2.25), mS (102.22), mS (7.88), uS (27.78), uS (  5) },   /* HP_13181,  HP_7970B,  MT_DENS_800, */
    { uS (556), mS (6.25), S (2.25), mS (160.00), mS (8.67), uS (13.89), uS (  5) },   /* HP_13183,  HP_7970E,  MT_DENS_1600 */
    { mS (2.2), mS (6.25), S (2.25), mS (  9.73), mS (9.73), uS (27.78), uS ( 20) },   /* HP_30215,  HP_7970B,  MT_DENS_800  */
    { mS (2.2), mS (6.25), S (2.25), mS ( 12.24), mS (9.73), uS (13.89), uS ( 20) },   /* HP_30215,  HP_7970E,  MT_DENS_1600 */
    { mS (0.0), mS (0.00), S (0.00), mS ( 00.00), mS (0.00), uS (00.00), uS ( 00) },   /* HP_IB,     HP_7974,   MT_DENS_800  */
    { mS (0.0), mS (0.00), S (0.00), mS ( 00.00), mS (0.00), uS (00.00), uS ( 00) }    /* HP_IB,     HP_7974,   MT_DENS_1600 */
    };


/* Command properties table.

   The validity of each command for the controller type is checked against the
   command properties table when it is prepared for execution.  The table also
   includes the class of the command, whether the drive must be ready before
   execution is permitted, and whether the command requires a data transfer
   phase.
 */

typedef struct {
    CNTLR_CLASS  class;                         /* command class */
    t_bool       valid [CNTLR_COUNT];           /* command validity, indexed by CNTLR_TYPE */
    t_bool       ready;                         /* command requires a ready drive */
    t_bool       transfer;                      /* command requires a transfer of data */
    } COMMAND_PROPERTIES;

#define T               TRUE
#define F               FALSE

static const COMMAND_PROPERTIES cmd_props [] = {        /* command properties, in CNTLR_OPCODE order */
/*       opcode            valid for      drive  data  */
/*        class         181 183 215 HPIB  ready  xfer  */
/*    -------------     --- --- --- ----  -----  ----- */
    { Class_Control,   { T,  T,  T,  T },   F,     F   },   /* 00 = Select_Unit_0 */
    { Class_Control,   { T,  T,  T,  T },   F,     F   },   /* 01 = Select_Unit_1 */
    { Class_Control,   { T,  T,  T,  T },   F,     F   },   /* 02 = Select_Unit_2 */
    { Class_Control,   { T,  T,  T,  T },   F,     F   },   /* 03 = Select_Unit_3 */
    { Class_Control,   { T,  T,  F,  F },   F,     F   },   /* 04 = Clear_Controller */
    { Class_Read,      { T,  T,  T,  T },   T,     T   },   /* 05 = Read_Record */
    { Class_Read,      { F,  F,  T,  F },   T,     T   },   /* 06 = Read_Record_with_CRCC */
    { Class_Read,      { T,  T,  F,  F },   T,     T   },   /* 07 = Read_Record_Backward */
    { Class_Read,      { T,  T,  F,  F },   T,     T   },   /* 08 = Read_File_Forward */
    { Class_Write,     { T,  T,  T,  T },   T,     T   },   /* 09 = Write_Record */
    { Class_Write,     { F,  F,  T,  F },   T,     T   },   /* 10 = Write_Record_without_Parity */
    { Class_Write,     { T,  T,  T,  T },   T,     F   },   /* 11 = Write_File_Mark */
    { Class_Write,     { T,  T,  T,  T },   T,     F   },   /* 12 = Write_Gap */
    { Class_Write,     { T,  T,  F,  F },   T,     F   },   /* 13 = Write_Gap_and_File_Mark */
    { Class_Control,   { T,  T,  T,  T },   T,     F   },   /* 14 = Forward_Space_Record */
    { Class_Control,   { T,  T,  T,  T },   T,     F   },   /* 15 = Forward_Space_File */
    { Class_Control,   { T,  T,  T,  T },   T,     F   },   /* 16 = Backspace_Record */
    { Class_Control,   { T,  T,  T,  T },   T,     F   },   /* 17 = Backspace_File */
    { Class_Rewind,    { T,  T,  T,  T },   T,     F   },   /* 18 = Rewind */
    { Class_Rewind,    { T,  T,  T,  T },   T,     F   },   /* 19 = Rewind_Offline */
    { Class_Invalid,   { F,  F,  F,  F },   F,     F   }    /* 20 = Invalid_Opcode */
    };


/* Status mapping table.

   The various HP tape controllers report tape status conditions in different
   locations in the status word.  To accommodate this, a lookup table is used to
   map specific conditions to the controller-specific bit(s).  An enumeration is
   used to provide symbolic table index constants.

   A given controller may not report all status conditions.  For conditions not
   reported, the table entries are zero, so that ORing the values into the
   status word has no effect.


   Implementation notes:

    1. Unit Selected status for units 0-3 must have values 0-3, so that the
       selected unit field of the controller state structure may be used
       directly as an index into the table.

    2. Single Track Error cannot occur under simulation.  In hardware, this
       error occurs on phase-encoded drives when a data error is detected and
       corrected.

    3. Multiple Track Error status for PE drives is the same as Parity Error
       status for NRZI drives.  Both are reported as Data_Error status.

    4. The status bits for the 30215 controller's Data_Error, Timing_Error,
       Tape_Runaway, and Command_Rejected errors are complemented from their
       actual values.  This allows an all-zeros value to represent No Error,
       which is consistent with the values used by the other controllers.  The
       30215 simulator complements these encoded error bits before reporting the
       controller status.

    5. Of the 30215 controller's encoded error status values, only Data_Error
       (value 4) and Timing_Error (value 6) can occur in the same transfer.  The
       controller hardware uses a priority encoder to give Timing_Error reporting
       priority.  Because of the values employed, we can simply OR the two
       errors as they occur, and Timing_Error will result.
*/

typedef enum {
    Unit_0_Selected = 0,
    Unit_1_Selected = 1,
    Unit_2_Selected = 2,
    Unit_3_Selected = 3,
    Command_Rejected,
    Data_Error,
    Density_1600,
    End_of_File,
    End_of_Tape,
    Interface_Busy,
    Load_Point,
    Odd_Length,
    Protected,
    Rewinding,
    Tape_Runaway,
    Timing_Error,
    Unit_Busy,
    Unit_Offline,
    Unit_Ready,
    Write_Status
    } STATUS_CONDITION;

static const uint32 status_bits [] [CNTLR_COUNT] = {    /* indexed by STATUS_CONDITION and CNTLR_TYPE */

/*    HP_13181   HP_13183   HP_30215   */
/*    --------   --------   --------   */
    { 0000000,   0000000,   0000000    },       /* Unit_0_Selected  */
    { 0000000,   0020000,   0004000    },       /* Unit_1_Selected  */
    { 0000000,   0040000,   0010000    },       /* Unit_2_Selected  */
    { 0000000,   0060000,   0014000    },       /* Unit_3_Selected  */
    { 0000010,   0000010,   0000012    },       /* Command_Rejected */
    { 0000002,   0000002,   0000004    },       /* Data_Error       */
    { 0000000,   0100000,   0000100    },       /* Density_1600     */
    { 0000200,   0000200,   0000020    },       /* End_of_File      */
    { 0000040,   0000040,   0002000    },       /* End_of_Tape      */
    { 0000400,   0000400,   0000000    },       /* Interface_Busy   */
    { 0000100,   0000100,   0000200    },       /* Load_Point       */
    { 0004000,   0004000,   0040000    },       /* Odd_Length       */
    { 0000004,   0000004,   0001000    },       /* Protected        */
    { 0002000,   0002000,   0000000    },       /* Rewinding        */
    { 0000000,   0000000,   0000010    },       /* Tape_Runaway     */
    { 0000020,   0000020,   0000006    },       /* Timing_Error     */
    { 0001000,   0001000,   0000000    },       /* Unit_Busy        */
    { 0000001,   0000001,   0000000    },       /* Unit_Offline     */
    { 0000000,   0000000,   0000400    },       /* Unit_Ready       */
    { 0000000,   0000000,   0000040    }        /* Write_Status     */
    };

/* Controller status flags */

#define CST_UNITSEL     (status_bits [cvptr->unit_selected] [cvptr->type])
#define CST_REJECT      (status_bits [Command_Rejected]     [cvptr->type])
#define CST_RUNAWAY     (status_bits [Tape_Runaway]         [cvptr->type])
#define CST_DATAERR     (status_bits [Data_Error]           [cvptr->type])
#define CST_EOF         (status_bits [End_of_File]          [cvptr->type])
#define CST_TIMERR      (status_bits [Timing_Error]         [cvptr->type])
#define CST_IFBUSY      (status_bits [Interface_Busy]       [cvptr->type])
#define CST_ODDLEN      (status_bits [Odd_Length]           [cvptr->type])

/* Unit status flags */

#define UST_WRPROT      (status_bits [Protected]            [cvptr->type])
#define UST_WRSTAT      (status_bits [Write_Status]         [cvptr->type])
#define UST_DEN1600     (status_bits [Density_1600]         [cvptr->type])

/* Dynamic status flags */

#define DST_EOT         (status_bits [End_of_Tape]          [cvptr->type])
#define DST_UNITBUSY    (status_bits [Unit_Busy]            [cvptr->type])
#define DST_UNITRDY     (status_bits [Unit_Ready]           [cvptr->type])
#define DST_LOADPT      (status_bits [Load_Point]           [cvptr->type])
#define DST_REWIND      (status_bits [Rewinding]            [cvptr->type])
#define DST_UNITLOCL    (status_bits [Unit_Offline]         [cvptr->type])


/* Controller operation names */

static const BITSET_NAME flag_names [] = {      /* controller flag names, in CNTLR_FLAG order */
    "CMRDY",                                    /*   000001 */
    "CMXEQ",                                    /*   000002 */
    "DTRDY",                                    /*   000004 */
    "EOD",                                      /*   000010 */
    "INTOK",                                    /*   000020 */
    "OVRUN",                                    /*   000040 */
    "XFRNG"                                     /*   000100 */
    };

static const BITSET_FORMAT flag_format =        /* names, offset, direction, alternates, bar */
    { FMT_INIT (flag_names, 0, lsb_first, no_alt, no_bar) };


static const BITSET_NAME function_names [] = {  /* interface function names, in CNTLR_IFN order */
    "IFIN",                                     /*   000000200000 */
    "IFOUT",                                    /*   000000400000 */
    "IFGTC",                                    /*   000001000000 */
    "SCPE",                                     /*   000002000000 */
    "RQSRV",                                    /*   000004000000 */
    "DVEND",                                    /*   000010000000 */
    "STCFL",                                    /*   000020000000 */
    "STDFL",                                    /*   000040000000 */
    "STINT",                                    /*   000100000000 */
    "DATTN"                                     /*   000200000000 */
    };

static const BITSET_FORMAT function_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (function_names, 16, lsb_first, no_alt, no_bar) };


static const char *opcode_names [] = {           /* command opcode names, in CNTLR_OPCODE order */
    "Select Unit 0",
    "Select Unit 1",
    "Select Unit 2",
    "Select Unit 3",
    "Clear Controller",
    "Read Record",
    "Read Record with CRCC",
    "Read Record Backward",
    "Read File Forward",
    "Write Record",
    "Write Record without Parity",
    "Write File Mark",
    "Write Gap",
    "Write Gap and File Mark",
    "Forward Space Record",
    "Forward Space File",
    "Backspace Record",
    "Backspace File",
    "Rewind",
    "Rewind Offline",
    "Invalid Command"
    };


static const char *phase_names [] = {            /* unit state names, in CNTLR_PHASE order */
    "idle",
    "wait",
    "start",
    "traverse",
    "data",
    "stop",
    "error"
    };


static const char *state_names [] = {           /* controller state names, in CNTLR_STATE order */
    "idle",
    "busy",
    "end",
    "error"
    };


static const char *const unit_names [] = {      /* unit names, in unit number order */
    "Unit 0",
    "Unit 1",
    "Unit 2",
    "Unit 3",
    "Controller unit"
    };


/* Simulator tape support library call properties table.

   The support library (sim_tape) call properties table is used to determine
   whether the call may have read or written an erase gap in the tape image
   file and whether the call returned a valid data length value.  These are used
   to interpret the tape positional change as a result of the call to separate
   the gap length from the data record length.  The table also contains strings
   describing the call actions that are used when tracing library calls.
*/

typedef enum {
    lib_space_fwd,                              /* call to sim_tape_sprecf */
    lib_space_rev,                              /* call to sim_tape_sprecr */
    lib_read_fwd,                               /* call to sim_tape_rdrecf */
    lib_read_rev,                               /* call to sim_tape_rdrecr */
    lib_write,                                  /* call to sim_tape_wrrecf */
    lib_write_gap,                              /* call to sim_tape_wrgap  */
    lib_write_tmk,                              /* call to sim_tape_wrtmk  */
    lib_rewind                                  /* call to sim_tape_rewind */
    } TAPELIB_CALL;


typedef struct {
    t_bool      gap_is_valid;                   /* call may involve an erase gap */
    t_bool      data_is_valid;                  /* call may involve a data record */
    const char  *action;                        /* string describing the call action */
    } TAPELIB_PROPERTIES;


static const TAPELIB_PROPERTIES lib_props [] = {    /* indexed by TAPELIB_CALL */
/*     gap   data                     */
/*    valid  valid        action      */
/*    -----  -----  ----------------- */
    {   T,     T,   "forward space"   },        /* lib_space_fwd */
    {   T,     T,   "backspace"       },        /* lib_space_rev */
    {   T,     T,   "read"            },        /* lib_read_fwd  */
    {   T,     T,   "reverse read"    },        /* lib_read_rev  */
    {   F,     T,   "write"           },        /* lib_write     */
    {   T,     F,   "write gap"       },        /* lib_write_gap */
    {   F,     F,   "write tape mark" },        /* lib_write_tmk */
    {   T,     F,   "rewind"          },        /* lib_rewind    */
    };


/* Simulator tape support library status values */

static const char *status_name [] = {           /* indexed by MTSE value */
    "succeeded",                                /*   MTSE_OK      */
    "terminated with tape mark seen",           /*   MTSE_TMK     */
    "failed with unit not attached",            /*   MTSE_UNATT   */
    "failed with I/O error",                    /*   MTSE_IOERR   */
    "failed with invalid record length",        /*   MTSE_INVRL   */
    "failed with invalid tape format",          /*   MTSE_FMT     */
    "terminated with beginning of tape seen",   /*   MTSE_BOT     */
    "terminated with end of medium seen",       /*   MTSE_EOM     */
    "succeeded with data error",                /*   MTSE_RECE    */
    "failed with no write ring",                /*   MTSE_WRP     */
    "failed with tape runaway"                  /*   MTSE_RUNAWAY */
    };


/* Tape library local controller routines */

static CNTLR_IFN_IBUS start_command    (CVPTR cvptr, CNTLR_FLAG_SET flags, CNTLR_OPCODE opcode);
static CNTLR_IFN_IBUS continue_command (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data);
static CNTLR_IFN_IBUS end_command      (CVPTR cvptr, UNIT *uptr);
static CNTLR_IFN_IBUS poll_drives      (CVPTR cvptr);
static CNTLR_IFN_IBUS call_tapelib     (CVPTR cvptr, UNIT *uptr, TAPELIB_CALL lib_call, t_mtrlnt parameter);
static CNTLR_IFN_IBUS abort_command    (CVPTR cvptr, UNIT *uptr, t_stat status);
static void           reject_command   (CVPTR cvptr, UNIT *uptr);
static void           add_crcc_lrcc    (CVPTR cvptr, CNTLR_OPCODE opcode);

/* Tape library local utility routines */

static void   activate_unit  (CVPTR cvptr, UNIT *uptr);
static t_stat validate_drive (CVPTR cvptr, UNIT *uptr, DRIVE_TYPE new_drive, uint32 new_bpi);



/* Tape library global controller routines */


/* Tape controller interface.

   This routine simulates the hardware interconnection between the abstract tape
   controller and the CPU interface.  This routine is called whenever the flag
   state changes.  This would be when a new command is to be started, when a
   channel begins a read or write operation, when a channel program terminates,
   or when a channel program error occurs.  It must also be called when the unit
   service routine is entered.  The caller passes in the set of interface flags
   and the contents of the data buffer.  The routine returns a set of functions;
   this is accompanied by a data value in these cases:

     Function  Data Value
     --------  --------------------------------
      IFGTC    command classification code
      SCPE     SCPE status code
      IFIN     data record word
      IFOUT    data record CRCC and LRCC word
      DATTN    unit number requesting attention

   On entry, the flags and controller state are examined to determine if a new
   controller command should be initiated or the current command should be
   continued.  If this routine is being called as a result of an event service
   or by channel initialization, "uptr" will point at the unit being serviced.
   Otherwise, "uptr" will be NULL (for example, when starting a new controller
   command).

   On entry, if a 3000 channel error has occurred, then return with no event
   scheduled; the CPU interface will recover by clearing the controller.
   Otherwise, if this is an event service entry or channel initialization, then
   process the next step of the command.  Otherwise, if the CMRDY or CMXEQ flag
   is asserted, then validate or start a new command.  Finally, if the
   controller is idle, then poll the drives for attention.

   In all cases, return a combined function set and outbound data word to the
   caller.


   Implementation notes:

    1. To accommodate the HP 1000's 1318x controllers that separate command
       validation from command execution, the "start_command" routine is entered
       if either the CMRDY or CMXEQ flag is set, but the command will be started
       only if the latter is set.
*/

CNTLR_IFN_IBUS tl_controller (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data)
{
CNTLR_IFN_IBUS outbound;

dpprintf (cvptr->device, TL_DEB_IOB, "Controller (%s) received data %06o with flags %s\n",
          state_names [cvptr->state], data, fmt_bitset (flags, flag_format));

if (flags & XFRNG)                                      /* if a channel error has occurred */
    outbound = NO_ACTION;                               /*   then the controller hangs until it's cleared */

else if (uptr)                                              /* otherwise if an event is being serviced */
    outbound = continue_command (cvptr, uptr, flags, data); /*   then continue with command processing */

else if (flags & (CMRDY | CMXEQ))                                   /* otherwise if a new command is ready */
    outbound = start_command (cvptr, flags, (CNTLR_OPCODE) data);   /*   then validate or execute it */

else                                                    /* otherwise there's nothing to do */
    outbound = NO_ACTION;                               /*   except possibly poll for attention */

if (cvptr->state == Idle_State                          /* if the controller is idle */
  && cvptr->type == HP_30215                            /*   and it's the 3000 controller */
  && flags & INTOK)                                     /*     and interrupts are allowed */
    outbound = poll_drives (cvptr);                     /*       then poll the drives for attention */

dpprintf (cvptr->device, TL_DEB_IOB, "Controller (%s) returned data %06o with functions %s\n",
          state_names [cvptr->state], TLIBUS (outbound),
          fmt_bitset (TLIFN (outbound), function_format));

return outbound;
}


/* Set a unit online or offline.

   If the unit indicated by the "uptr" parameter is currently attached, it will
   be set online if the "online" parameter is true or offline if the parameter
   is false.  If the drive is not rewinding, and it was offline and is being set
   online, then the unit attention flag is set, and, if the controller is idle,
   the routine returns SCPE_INCOMP to indicate that the caller must then call
   the controller to poll for unit attention to complete the command.
   Otherwise, it returns SCPE_OK, and the drives will be polled automatically
   when the current command completes and the controller is idled.

   If the drive is rewinding when it is set online, attention is deferred until
   the rewind completes (because the unit is not ready until then).
*/

t_stat tl_onoffline (CVPTR cvptr, UNIT *uptr, t_bool online)
{
const int32 unit = (int32) (uptr - cvptr->device->units);   /* the unit number */
t_stat status = SCPE_OK;

if (uptr->flags & UNIT_ATT) {                           /* if the unit is attached */
    if (!(uptr->flags & UNIT_REWINDING)                 /*   then if the tape is not rewinding */
      && uptr->flags & UNIT_OFFLINE                     /*     and it is currently offline */
      && online) {                                      /*       and is going online */
        cvptr->unit_attention |= 1 << unit;             /*         then the unit needs attention */

        if (cvptr->state == Idle_State)                 /* if the controller is idle */
            status = SCPE_INCOMP;                       /*   then it must be called to poll the drives */
        }

    dpprintf (cvptr->device, TL_DEB_CMD, "Unit %d set %s\n",
              unit, (online ? "online" : "offline"));
    }

else                                                    /* otherwise the unit is detached and offline */
    status = SCPE_UNATT;                                /*   so it cannot be set online */

return status;
}


/* Return the current controller and unit status.

   This routine returns the combined formatted controller and unit status for
   the currently selected drive unit.

   In hardware, the selected tape unit presents its status information
   continuously to the controller.  The 7970B and 7970E drives provide these
   common status signals:

     - SL  = status online (selected * online)
     - SLP = status load point (selected * online * at-load-point)
     - SRW = status rewind (selected * online * rewinding)
     - SET = status end-of-tape (selected * online * EOT-during-motion-forward)
     - SR  = status ready (selected * online * ~loading * ~rewinding)
     - SFP = status file protect (selected * online * ~write-ring)
     - SW  = status write (selected * online * cmd-set-write * motion-forward)

   Unique status signals for the 7970B are:

     - S7  = status 7-track drive

   For the 7970E, they are:

     - SD2  = status density 200 bpi
     - SD5  = status density 556 bpi
     - SD8  = status density 800 bpi
     - SD16 = status density 1600 bpi
     - MTE  = multiple-track-error
     - STE  = single-track-error
     - TM   = tape-mark
     - IDB  = ID burst
     - EOB  = end-of-block

   In simulation, returned status is a combination of static controller status,
   static unit status, and dynamic unit status.  Static status is set during
   command execution and persists until the next command begins.  An example is
   a data error or write protected status.  Dynamic status is determined at the
   point of the status call.  An example is end-of-tape status.

   The bits representing the returned status are controller-specific, so macros
   are used to index into the status mapping table.


   Implementation notes:

    1. The HP 3000 controller presents the SR signal as "unit ready" status.
       The 1000 controllers present the complement of the SR signal as "unit
       busy" status and the complement of the SL signal as "unit local" status.
*/

HP_WORD tl_status (CVPTR cvptr)
{
UNIT *const uptr = cvptr->device->units + cvptr->unit_selected; /* a pointer to the selected unit */
uint32 status;

status = cvptr->status | CST_UNITSEL;                   /* merge the controller status and the selected unit number */

if (uptr->flags & UNIT_OFFLINE)                         /* if the unit is offline */
    status |= DST_UNITBUSY | DST_UNITLOCL;              /*   then set not-ready and not-online status */

else {                                                  /* otherwise the unit is online */
    status |= uptr->STATUS;                             /*   so add the unit status */

    if (uptr->flags & UNIT_REWINDING)                   /* if the tape is rewinding */
        status |= DST_REWIND | DST_UNITBUSY;            /*   then set rewind and busy status */

    else {                                              /* otherwise */
        status |= DST_UNITRDY;                          /*   the unit is ready */

        if (sim_tape_bot (uptr))                        /* if the tape is positioned at the beginning */
            status |= DST_LOADPT;                       /*   then set load-point status */

        else if (sim_tape_eot (uptr))                   /* otherwise if the tape positioned after EOT */
            status |= DST_EOT;                          /*   then set end-of-tape status */
        }
    }

return LOWER_WORD (status);                             /* return the 16-bit combined controller and unit status */
}


/* Reset the controller.

   This routine is called to perform a hard clear on the tape controller.  It is
   a harder clear than the clear performed by the "tl_clear" routine in
   response to a programmed master reset or clear.  This clear aborts any I/O in
   progress, including rewinds, and resets the controller and all units to the
   idle state.  It is typically called by the interface in response to a RESET
   command.

   In addition, if this is a power-on reset, it sets up the property entry index
   value in each unit.
*/

t_stat tl_reset (CVPTR cvptr)
{
uint32     unit;
UNIT       *uptr;
DRIVE_TYPE drive;

tl_clear (cvptr);                                           /* clear any in-progress writes */

if (sim_switches & SWMASK ('P'))                                /* if this is a power-on reset */
    for (unit = 0; unit < cvptr->device->numunits; unit++) {    /*   then set up the unit property indices */
        uptr = cvptr->device->units + unit;                     /* get a pointer to the unit */
        drive = GET_MODEL (uptr->flags);                        /*   and the current drive model ID */

        if (validate_drive (cvptr, uptr, drive, 0) != SCPE_OK)  /* if the drive's property index cannot be set */
            return SCPE_IERR;                                   /*   then return internal error status */
        }

for (unit = 0; unit < cvptr->device->numunits; unit++) {    /* reset all of the drives */
    uptr = cvptr->device->units + unit;                     /* get a pointer to the unit */

    sim_tape_reset (uptr);                                  /* reset the tape support library status */
    sim_cancel (uptr);                                      /*   and cancel any in-process operation */
    uptr->wait = NO_EVENT;                                  /*     and any scheduled operation */

    uptr->PHASE = Idle_Phase;                               /* idle the unit */
    uptr->OPCODE = Invalid_Opcode;                          /*   and clear the opcode */

    uptr->STATUS &= ~UST_WRSTAT;                            /* clear any write status */
    uptr->flags &= ~UNIT_REWINDING;                         /* the unit is no longer rewinding */
    }

return SCPE_OK;
}


/* Clear the controller.

   This routine performs a hardware clear on the controller.  It is equivalent
   to asserting the CLEAR signal to the 3000 controller, which restarts the
   controller microprogram, or executing the CLR command on the 1000
   controllers.  It clears any controller operation in progress and stops all
   tape motion, except that drives with rewinds in progress are allowed to
   complete the rewind.  For the 3000 controller only, unit 0 is selected.

   In simulation, if a clear is done before a write starts, the write is
   abandoned.  If it's done while a write is in progress, the record is
   truncated at the current point and marked as bad.

   In REALTIME mode only, if an in-progress read or spacing operation involves
   an erase gap (indicated by the position change being greater than the record
   length traversed), the simulated position is calculated.  If it is within the
   gap, the tape is repositioned and then aligned to a gap marker.  Otherwise,
   the position is unchanged, as repositioning within a record is not supported.

   In FASTTIME mode, calculating tape position from the remaining traversal time
   is unreliable, so it is not attempted.


   Implementation notes:

    1. The traverse phase is entered either when traversing an erase gap or when
       rewinding.  Once rewinding is excluded, a unit in the traverse phase must
       be moving over a gap.

    2. The REALTIME gap traversal position at the time of the controller clear
       is calculated by determining how much of the gap had been traversed and
       then adding or subtracting that from the initial position, depending on
       the direction of travel.  The gap traversal amount in bytes is the
       original gap length minus the amount not yet traversed, which is
       determined by dividing the remaining event delay time by the delay time
       per byte.  If the current (final) position is less than the initial
       position, then the tape was moving backward, so the traversal amount is
       subtracted from the initial position.  Otherwise, the tape is moving
       forward, and the amount is added.

    3. In hardware, a clear asynchronously resets the state machine (1000
       controllers) or restarts the microprogram (3000 controller).  Therefore,
       an aborted write will leave a bad record on the tape, as the normal write
       sequence will not complete -- an NRZI drive will not have the trailing
       CRCC and LRCC bytes, and a PE drive will not have the postamble and IRG.
       In simulation, the record is written with the "bad data" indicator.

    4. The only unit that may be in a non-idle phase without being active is the
       controller unit.
*/

void tl_clear (CVPTR cvptr)
{
uint32   unit;
int32    remaining_time;
UNIT     *uptr;
t_addr   reset_position, relative_position;
t_mtrlnt marker;

for (unit = 0; unit < cvptr->device->numunits; unit++) {    /* look for a write or gap traverse in progress */
    uptr = cvptr->device->units + unit;                     /* get a pointer to the unit */

    remaining_time = sim_activate_time (uptr);              /* get the remaining unit delay time, if any */

    if (remaining_time) {                                   /* if the unit is currently active */
        if (uptr->flags & UNIT_REWINDING)                   /*   then a clear does not affect a rewind in progress */
            dpprintf (cvptr->device, TL_DEB_INCO,
                      "Unit %u controller clear allowed %s to continue\n",
                      unit, opcode_names [uptr->OPCODE]);

        else {                                              /* but all other commands are aborted */
            sim_cancel (uptr);                              /*   so cancel any scheduled event */

            if ((cvptr->device->flags & DEV_REALTIME)       /* if REALTIME mode is selected */
              && uptr->PHASE == Traverse_Phase) {           /*   and a gap is being traversed */
                relative_position = cvptr->gaplen           /*     then calculate the relative progress */
                                      - remaining_time / cvptr->dlyptr->data_xfer;

                if (uptr->pos < cvptr->initial_position)                            /* if the motion is backward */
                    reset_position = cvptr->initial_position - relative_position;   /*   then move toward the BOT */
                else                                                                /* otherwise */
                    reset_position = cvptr->initial_position + relative_position;   /*   move toward the EOT */

                cvptr->gaplen -= (t_mtrlnt) relative_position;  /* reduce the gap length by the amount not traversed */

                while (cvptr->gaplen > sizeof (t_mtrlnt)) {     /* align the reset position to a gap marker */
                    if (sim_fseek (uptr->fileref,               /* seek to the reset position */
                                   reset_position, SEEK_SET)    /*   and if the seek succeeds */
                      || sim_fread (&marker, sizeof (t_mtrlnt), /*     then read the marker */
                                    1, uptr->fileref) == 0) {                   /* if either call fails */
                        cprintf ("%s simulator tape library I/O error: %s\n",   /*   then report the error */
                                 sim_name, strerror (errno));                   /*     to the console */

                        clearerr (uptr->fileref);                               /* clear the error and leave the file */
                        break;                                                  /*   at the original position */
                        }

                    else if (marker == MTR_GAP) {           /* otherwise if a gap marker was read */
                        uptr->pos = reset_position;         /*   then set the new position */
                        break;                              /*     and repositioning is complete */
                        }

                    else {                                  /* otherwise */
                        reset_position--;                   /*   back up a byte and try again */
                        cvptr->gaplen--;                    /*     until the gap is exhausted */
                        }
                    };

                dpprintf (cvptr->device, TL_DEB_INCO,
                          "Unit %u controller clear stopped tape motion at position %" T_ADDR_FMT "u\n",
                          unit, uptr->pos);
                }

            else                                            /* otherwise FASTTIME mode is selected */
                dpprintf (cvptr->device, TL_DEB_INCO,
                          "Unit %u controller clear aborted %s after partial completion\n",
                          unit, opcode_names [uptr->OPCODE]);

            if (cmd_props [uptr->OPCODE].class == Class_Write   /* if the last command was a write */
              && cmd_props [uptr->OPCODE].transfer == TRUE      /*   that involves a data transfer */
              && uptr->PHASE == Data_Phase)                     /*   that was not completed */
                cvptr->call_status = MTSE_RECE;                 /*     then the record will be bad */

            uptr->PHASE = Stop_Phase;                           /* execute the stop phase of the command */
            continue_command (cvptr, uptr, NO_FLAGS, NO_DATA);  /*   to ensure a partial record is written */

            sim_tape_reset (uptr);                              /* reset the tape support library status */
            }
        }

    else if (uptr->PHASE != Idle_Phase) {               /* otherwise if the controller unit is executing */
        uptr->PHASE = Idle_Phase;                       /*   then idle it */

        if (uptr->OPCODE != Clear_Controller)           /* report the abort only if this isn't a clear command */
            dpprintf (cvptr->device, TL_DEB_INCO,
                      "Unit %u controller clear aborted %s after partial completion\n",
                      unit, opcode_names [uptr->OPCODE]);
        }
    }

cvptr->status = 0;                                      /* clear the controller status */
cvptr->state = Idle_State;                              /*   and idle it */
cvptr->unit_attention = 0;                              /* clear any pending unit attention */

if (cvptr->type == HP_30215)                            /* if this is the 3000 controller */
    cvptr->unit_selected = 0;                           /*   then a clear selects unit 0 */

dpprintf (cvptr->device, TL_DEB_CMD, "Controller cleared\n");

return;
}



/* Tape library global utility routines */


/* Return the name of an opcode.

   A string representing the supplied controller opcode is returned to the
   caller.  If the opcode is invalid, an error string is returned.
*/

const char *tl_opcode_name (CNTLR_OPCODE opcode)
{
if (opcode < Invalid_Opcode)                            /* if the opcode is legal */
    return opcode_names [opcode];                       /*   then return the opcode name */
else                                                    /* otherwise */
    return opcode_names [Invalid_Opcode];               /*   return an error indication */
}


/* Return the name of a unit.

   A string representing the supplied unit is returned to the caller.  If the
   unit is invalid, an error string is returned.
*/

const char *tl_unit_name (int32 unit)
{
if (unit <= TL_CNTLR_UNIT)                              /* if the unit number is valid */
    return unit_names [unit];                           /*   then return the unit designator */
else                                                    /* otherwise */
    return "Unit invalid";                              /*   return an error indication */
}


/* Tape library global SCP support routines */


/* Attach a tape image file to a unit.

   The file specified by the supplied filename is attached to the indicated
   unit.  If the attach was successful, the drive is set online, and unit
   attention is set.

   If the controller is idle, the routine returns SCPE_INCOMP to indicate that
   the caller must then call the controller to poll for drive attention to
   complete the command.  Otherwise, it returns SCPE_OK, and the drives will be
   polled automatically when the current command completes and the controller is
   idled.


   Implementation notes:

    1. The support library LOCKED and WRITEENABLED modifiers are not used to
       specify a tape as read-only.  All HP drives use a write ring on the back
       of the tape reel, which must be dismounted to remove or add the ring.
       Therefore, changing read-only status results in a remount, leaving the
       tape at the load point.  LOCKED and WRITEENABLED do not provide these
       semantics.  Instead, we require the -R option to the ATTACH command to
       specify that a tape does not have a write ring.

    2. If we are called during a RESTORE command, the unit's flags are not
       changed to avoid upsetting the state that was SAVEd.

    3. The pointer to the appropriate event delay times is set in case we are
       being called during a RESTORE command (the assignment is redundant
       otherwise).
*/

t_stat tl_attach (CVPTR cvptr, UNIT *uptr, CONST char *cptr)
{
const int32 unit = (int32) (uptr - cvptr->device->units);   /* the unit number */
t_stat result;

result = sim_tape_attach (uptr, cptr);                  /* attach the tape image file to the unit */

if (result == SCPE_OK                                   /* if the attach was successful */
  && (sim_switches & SIM_SW_REST) == 0) {               /*   and we are not being called during a RESTORE command */
    uptr->flags = uptr->flags & ~UNIT_OFFLINE;          /*     then set the unit online */

    if (uptr->flags & UNIT_RO)                          /* if the attached file is read-only */
        uptr->STATUS |= UST_WRPROT;                     /*   then set write-protected status */
    else                                                /* otherwise */
        uptr->STATUS &= ~UST_WRPROT;                    /*   clear the status */

    cvptr->unit_attention |= 1 << unit;                 /* drive attention sets on tape load */

    dpprintf (cvptr->device, TL_DEB_CMD, "Unit %d tape loaded and set online\n",
              unit);

    if (cvptr->state == Idle_State)                     /* if the controller is idle */
        result = SCPE_INCOMP;                           /*   then it must be called to poll the drives */
    }

if (cvptr->device->flags & DEV_REALTIME)                /* if realistic timing is selected */
    cvptr->dlyptr = &real_times [PROP_INDEX (uptr)];    /*   then get the real times pointer for this drive */
else                                                    /* otherwise optimized timing is selected */
    cvptr->dlyptr = cvptr->fastptr;                     /*   so use the fast times pointer */

return result;                                          /* return the result of the attach */
}


/* Detach a tape image file from a unit.

   The tape is unloaded from the drive, and the attached file, if any, is
   detached.  Unloading a tape leaves the drive offline.  A command in progress
   is allowed to continue to completion, unless it attempts to access the file.
   If it does, the command will abort and simulation will stop with a "Unit not
   attached" error message.
*/

t_stat tl_detach (UNIT *uptr)
{
uptr->flags &= ~UNIT_OFFLINE;                           /* set the unit offline */

return sim_tape_detach (uptr);                          /* detach the tape image file from the unit */
}


/* Set the controller timing mode.

   This validation routine is called to set the timing mode for the tape
   controller.  The "value" parameter is set to 1 to enable realistic timing and
   0 to enable optimized timing.  The "desc" parameter is a pointer to the
   controller.

   When realistic timing is specified, unit activation delays are a function of
   both the controller type and the tape drive model.  As some controllers,
   e.g., the HP 3000 controller, support several drive models, the delays must
   be determined dynamically when a command is started on a given unit.
   Therefore, this routine simply sets or clears the "real time" flag, which is
   tested in the "start_command" routine.
*/

t_stat tl_set_timing (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
DEVICE *const dptr = ((CVPTR) desc)->device;            /* a pointer to the controlling device */

if (value)                                              /* if realistic timing is requested */
    dptr->flags |= DEV_REALTIME;                        /*   then set the real time flag */
else                                                    /* otherwise */
    dptr->flags &= ~DEV_REALTIME;                       /*   clear it for fast timing */

return SCPE_OK;
}


/* Set the tape drive model.

   This validation routine is called to set the model of the tape drive
   associated with the specified unit.  The "value" parameter indicates the
   model ID.  Support for the drive model with the specified controller is
   verified before permitting the change.
*/

t_stat tl_set_model (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const CVPTR cvptr = (CVPTR) desc;                       /* the controller state structure pointer */
const DRIVE_TYPE new_drive = GET_MODEL (value);         /* the new model ID */

return validate_drive (cvptr, uptr, new_drive, 0);      /* verify the model change and set the property index */
}


/* Set the tape drive density.

   This validation routine is called to set the density of the tape drive
   associated with the specified unit via a "SET <unit> DENSITY=<bpi>" command.
   The "cptr" parameter points to the <bpi> string, which must be a value
   supported by the controller and tape drive model.  Support for the new
   density setting is verified before permitting the change.
*/

t_stat tl_set_density (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const CVPTR cvptr = (CVPTR) desc;                       /* the controller state structure pointer */
const DRIVE_TYPE model = GET_MODEL (uptr->flags);       /* the current drive model ID */
uint32 new_bpi;
t_stat status;

if (cptr == NULL || *cptr == '\0')                      /* if no density value is present */
    return SCPE_MISVAL;                                 /*   then report a missing value error */

new_bpi = (uint32) get_uint (cptr, 10, UINT_MAX, &status);  /* parse the supplied value */

if (status != SCPE_OK)                                      /* if a parsing failure occurred */
    return status;                                          /*   then report the problem */

else if (new_bpi == 0)                                      /* otherwise if the new density is zero */
    return SCPE_ARG;                                        /*   then disallow it */

else                                                        /* otherwise a numeric value is present */
    return validate_drive (cvptr, uptr, model, new_bpi);    /*   so validate the density change */
}


/* Set the tape drive reel capacity.

   This validation routine is called to set the tape reel capacity of the tape
   drive associated with the specified unit.  The "value" parameter indicates
   whether the capacity in megabytes (0) or the tape length in feet (1) was
   specified.

   If the capacity was specified with a "SET <unit> CAPACITY=<n>" command, the
   simulator tape support library routine "sim_tape_set_capac" is called to
   parse the size in megabytes and set the unit capacity value.  If the routine
   succeeded, any existing reel size is cleared.

   If the capacity was specified with a "SET <unit> REEL=<length>" command, the
   "cptr" parameter points to the <length> string, which must be one of the
   standard reel sizes (600, 1200, or 2400-foot).  If the reel size is 0, the
   capacity is reset to the unlimited size.  Otherwise, the reel size ID is set
   in the unit property value, and the unit capacity in bytes is calculated and
   set from the reel size and tape drive density.  If the reel size is not a
   valid value, the routine returns SCPE_ARG to indicate an invalid argument was
   supplied.
*/

t_stat tl_set_reelsize (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const uint32 tape_bpi = drive_props [PROP_INDEX (uptr)].bpi;    /* the tape unit density */
int32  reel;
t_stat status;

if (value == 0) {                                           /* if the capacity is being specified directly */
    status = sim_tape_set_capac (uptr, value, cptr, desc);  /*   then set it using the supplied size in megabytes */

    if (status == SCPE_OK)                                  /* if the supplied capacity is OK */
        uptr->PROP = uptr->PROP & ~PROP_REEL_MASK           /*   then clear any prior reel size setting */
                       | PROP_REEL_UNLIM;
    return status;
    }

else if (cptr == NULL)                                  /* otherwise if no reel size is present */
    return SCPE_ARG;                                    /*   then return an invalid argument error */

else {                                                  /* otherwise a size is specified */
    reel = (int32) get_uint (cptr, 10, 2400, &status);  /*   so parse the tape length in feet */

    if (status != SCPE_OK)                              /* if the parse failed */
        return status;                                  /*   then return the failure status */

    else {                                              /* otherwise */
        switch (reel) {                                 /*   validate the reel size */

            case 0:                                         /* an unlimited-length reel */
                uptr->capac = 0;                            /*   so set the capacity  */
                uptr->PROP = uptr->PROP & ~PROP_REEL_MASK   /*     and reel property to "unlimited" */
                               | PROP_REEL_UNLIM;
                break;

            case 600:                                       /* a 600 foot reel */
                uptr->capac = 600 * 12 * tape_bpi;          /*   so set the capacity in bytes */
                uptr->PROP = uptr->PROP & ~PROP_REEL_MASK   /*     and set the reel property ID */
                               | PROP_REEL_600;
                break;

            case 1200:                                      /* a 1200 foot reel */
                uptr->capac = 1200 * 12 * tape_bpi;
                uptr->PROP = uptr->PROP & ~PROP_REEL_MASK
                               | PROP_REEL_1200;
                break;

            case 2400:                                      /* a 2400 foot reel */
                uptr->capac = 2400 * 12 * tape_bpi;
                uptr->PROP = uptr->PROP & ~PROP_REEL_MASK
                               | PROP_REEL_2400;
                break;

            default:                                        /* other length values */
                return SCPE_ARG;                            /*   are invalid */
            }

        return SCPE_OK;                                 /* a valid reel size is accepted */
        }
    }
}


/* Show the controller timing mode.

   This display routine is called to show the timing mode for the tape
   controller.  The "desc" parameter is a pointer to the controller.


   Implementation notes:

    1. The explicit use of "const CNTLR_VARS *" is necessary to declare a
       pointer to a constant structure.  Using "const CVPTR" declares a constant
       pointer instead.
*/

t_stat tl_show_timing (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
DEVICE *const dptr = ((const CNTLR_VARS *) desc)->device;   /* a pointer to the controlling device */

if (dptr->flags & DEV_REALTIME)                         /* if the real time flag is set */
    fputs ("realistic timing", st);                     /*   then we're using realistic timing */
else                                                    /* otherwise */
    fputs ("fast timing", st);                          /*   we're using optimized timing */

return SCPE_OK;
}


/* Show the tape drive density.

   This display routine is called to show the density in bits per inch of the
   tape drive associated with the specified unit.  The "uptr" parameter points
   to the unit to be queried.
*/

t_stat tl_show_density (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
fprintf (st, "%u bpi", drive_props [PROP_INDEX (uptr)].bpi);

return SCPE_OK;
}


/* Show the tape drive reel capacity.

   This display routine is called to show the tape reel capacity of the tape
   drive associated with the unit pointed to by the "uptr" parameter.  The
   "value" parameter indicates whether the reel size was requested explicitly
   via a "SHOW <unit> REEL" command (1), or whether the capacity was requested
   explicitly via a "SHOW <unit> CAPACITY" command or implicitly via "SHOW
   <dev>" or "SHOW <unit>" commands (0).  The "desc" parameter is a pointer to
   the controller.

   If the reel capacity is unlimited or is a specified size in megabytes, the
   simulator tape support library routine "sim_tape_show_capac" is called to
   report the unlimited size or the size in megabytes.  Otherwise, the specified
   length in feet is reported.

   The reel size format modifier is a "named SHOW only" entry, so the caller
   does not add a trailing newline after the routine returns.  Therefore, we
   have to add it here.


   Implementation notes:

    1. Reel size IDs for 600, 1200, and 2400 foot reels must be 1, 2, and 3,
       respectively, to provide multiplication by 2 ** <reel ID>.
*/

t_stat tl_show_reelsize (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
t_stat status = SCPE_OK;

if (PROP_REEL (uptr) == Reel_Unlimited)                     /* if the unit has unlimited or custom size */
    status = sim_tape_show_capac (st, uptr, value, desc);   /*   then display the capacity in bytes */

else                                                        /* otherwise */
    fprintf (st, "%4d foot reel", 300 << PROP_REEL (uptr)); /*   display the reel size in feet */

if (value == 1)                                             /* if we're called to display the reel size */
    fputc ('\n', st);                                       /*   then add a newline, as the caller will omit it */

return status;
}



/* Tape library local controller routines */


/* Start a new command.

   This routine is called to validate and optionally begin execution of a new
   command.  It is called when the controller is waiting for a command and the
   interface asserts CMRDY and/or CMXEQ to indicate that a new command is
   available.  It returns a set of action functions and a data word to the
   caller.  For a good command with CMXEQ asserted, it also sets up the next
   phase of operation on the controller and/or drive unit and schedules the
   unit(s) as appropriate.

   On entry, the command opcode is supplied in the "inbound_data" parameter and
   applies to the currently selected unit.  The current delay properties pointer
   is set up, depending on the timing mode, and the opcode is checked for
   validity.  If it fails the check, the command is rejected.  If it's OK and is
   a Unit Select command, it is executed immediately.  Otherwise, if the CMXEQ
   flag is set, command execution is scheduled.

   The routine asserts the IFGTC function to inform the interface that the
   command was executed.  If the command transfers data, the class of the
   command (read or write) is returned, so that the interface may ensure that
   the correct type of channel transfer is set up.  All other commands,
   including write commands that do not transfer data (e.g., Write File Mark),
   are classified as control commands to indicate that no channel transfer is
   required.

   A command rejection occurs on the 1000 controllers for these reasons:

     - the unit is not ready for any command requiring tape motion
     - the tape has no write ring and a write command is issued
     - a Select Unit command is issued while the controller is busy
     - a Backspace Record or Backspace File command is issued with the tape
       positioned at the load point

   A command rejection occurs on the 3000 controller for these reasons:

     - the unit is not ready for any command requiring tape motion
     - the tape has no write ring and a write command is issued
     - an illegal command opcode is issued
     - illegal bits are set in the control word
     - a command is issued while the controller is busy
     - a TOGGLEOUTXFER signal asserts without a write data command in process
     - a TOGGLEINXFER signal asserts without a read data command in process
     - a PCONTSTB signal asserts with the input or output transfer flip-flops set

   Examples of the last three rejection reasons are:

     - a Write File Mark control order is followed by a write channel order
     - a Write Record control order is followed by a read channel order
     - a write channel order is followed by a Write Record control order

   An interface may force a command reject by passing the Invalid_Opcode value
   as the command to validate.


   Implementation notes:

    1. The 1000 controllers separate validity checking from execution.  The
       former occurs when an OTx instruction is issued; the latter occurs when
       an STC instruction is issued.  The flags that accompany instruction
       execution are CMRDY for OTx and CMXEQ for STC.

       The 3000 controller combines these two functions, and the flags
       accompanying the single call are CMRDY and CMXEQ.

    2. The Select_Unit_n command opcode values are contiguous, so that the
       unit selected may be set directly from the opcode value.

    3. The 3000 controller hardware executes a Select Unit command in about 10
       microcode instructions from PCONTSTB to drive select output and service
       request assertion, which is about 4.3 microseconds (the microinstruction
       time is 434 nanoseconds).  The 1000 controllers clock the Select Unit
       command directly into a register with the IOO signal; this effectively
       selects the unit "immediately."  In simulation, the Select Unit command
       completes without scheduling an event delay.

    4. For the 3000 controller, command rejection requires a delay between
       detection and interrupt assertion, which must be scheduled on the
       controller unit (all drive units may be busy, e.g., rewinding, so we
       can't schedule on them).

    5. All tape drives ignore a rewind command if the tape is positioned at the
       load point.  The 3000 controller will complete the command immediately,
       whereas the 1000 controllers still will delay for the rewind start time
       before completing.

    6. The "drive ready" condition exists if the unit is online and not
       rewinding.
*/

static CNTLR_IFN_IBUS start_command (CVPTR cvptr, CNTLR_FLAG_SET flags, CNTLR_OPCODE opcode)
{
UNIT *const uptr = cvptr->device->units + cvptr->unit_selected;   /* a pointer to the currently selected unit */
CNTLR_IFN_IBUS outbound;

if (cvptr->device->flags & DEV_REALTIME)                /* if realistic timing is selected */
    cvptr->dlyptr = &real_times [PROP_INDEX (uptr)];    /*   then get the real times pointer for this drive */
else                                                    /* otherwise optimized timing is selected */
    cvptr->dlyptr = cvptr->fastptr;                     /*   so use the fast times pointer */

if ((flags & CMRDY)                                     /* if command validity is to be checked */
  && (opcode >= Invalid_Opcode                          /*   and the opcode is invalid */
  || cvptr->type > LAST_CNTLR                           /*   or the controller type is undefined */
  || cmd_props [opcode].valid [cvptr->type] == FALSE    /*   or the opcode is not valid for this controller */
  || cmd_props [opcode].ready                           /*   or it requires a ready drive */
    && uptr->flags & (UNIT_OFFLINE | UNIT_REWINDING)    /*     but it's offline or rewinding */
  || cmd_props [opcode].class == Class_Write            /*   or this is a write command */
    && sim_tape_wrp (uptr)                              /*     but the drive is write-protected */
  || opcode != Clear_Controller                         /*   or it's not a controller clear */
    && cvptr->state != Idle_State)) {                   /*     and the controller is busy */
    CNTLR_UPTR->OPCODE = opcode;                        /*   then save the rejected code */
    reject_command (cvptr, NULL);                       /*     and reject the command */
    outbound = NO_ACTION;                               /*       with no further action */
    }

else {                                                  /* otherwise the command is (assumed to be) OK */
    if (cmd_props [opcode].class == Class_Write)        /* if this is a write command */
        uptr->STATUS |= UST_WRSTAT;                     /*   then set write status */
    else                                                /* otherwise */
        uptr->STATUS &= ~UST_WRSTAT;                    /*   clear it */

    cvptr->status = 0;                                  /* clear the controller status for the new command */

    cvptr->call_status = MTSE_OK;                       /* clear the last library call status code */
    cvptr->index  = 0;                                  /* set up the */
    cvptr->length = 0;                                  /*   buffer access */
    cvptr->gaplen = 0;                                  /*     and clear the gap length */

    if (opcode >= Select_Unit_0 && opcode <= Select_Unit_3) {       /* if the opcode is a Select Unit command */
        cvptr->unit_selected = (uint32) (opcode - Select_Unit_0);   /*   then select the indicated unit */

        dpprintf (cvptr->device, TL_DEB_INCO, "%s completed\n",
                  opcode_names [opcode]);

        dpprintf (cvptr->device, TL_DEB_CMD, "%s succeeded\n",
                  opcode_names [opcode]);

        outbound = IFGTC | RQSRV | Class_Control;       /* indicate that a control command was executed */
        }

    else if (flags & CMXEQ) {                           /* otherwise if the command is to be executed */
        cvptr->state = Busy_State;                      /*   then set the controller */
        cvptr->status = CST_IFBUSY;                     /*     and the interface to the busy state */

        uptr->OPCODE = opcode;                          /* save the opcode in the selected unit */

        if (cmd_props [opcode].transfer) {              /* if this command transfers data */
            CNTLR_UPTR->PHASE = Wait_Phase;             /*   then set up the wait phase */
            CNTLR_UPTR->OPCODE = opcode;                /*     and command opcode on the controller unit */

            outbound = IFGTC | RQSRV | cmd_props [opcode].class;    /* return the transfer class (read or write) */
            }

        else {                                          /* otherwise it's a control command */
            uptr->PHASE = Start_Phase;                  /*   so set up the start phase */
            uptr->wait = cvptr->dlyptr->overhead;       /*     and the initial controller delay */

            if (cmd_props [opcode].ready)                       /* if the command accesses the drive */
                if (cmd_props [opcode].class != Class_Rewind)   /*   then if this is not a rewind command */
                    if (sim_tape_bot (uptr))                    /*      then if the tape is positioned at the BOT */
                        uptr->wait += cvptr->dlyptr->bot_start; /*        then the start delay is longer */
                    else                                        /*          than it would be if the position */
                        uptr->wait += cvptr->dlyptr->ir_start;  /*            is between two records */

                else if (! sim_tape_bot (uptr)                  /* otherwise if the rewind is not from the load point */
                  || cvptr->type != HP_30215)                   /*   or this is not the 3000 controller */
                    uptr->wait += cvptr->dlyptr->rewind_start;  /*     then add the rewind start delay */

            activate_unit (cvptr, uptr);                /* schedule the start phase */

            outbound = IFGTC | Class_Control;           /* indicate a control command is executing */
            }
        }

    else                                                /* otherwise */
        outbound = NO_ACTION;                           /*   command execution was not requested */
    }

return outbound;                                        /* return the functions and data word */
}


/* Continue the current command.

   This routine simulates continuing execution of the controller microcode or
   state machine for the current command.  It is called whenever the controller
   has had to wait for action from the CPU interface or the drive unit, and that
   action has now occurred.  Typically, this would be whenever the interface
   flag status changes, or a unit's event service has been entered.  It returns
   a set of action functions and a data word to the caller.  It also sets up the
   next phase of operation on the controller and/or drive unit and schedules the
   unit(s) as appropriate.

   On entry, the "uptr" parameter points to the unit whose event is to be
   serviced; this may be either the controller unit or a drive unit.  The
   "inbound_flags" and "inbound_data" parameters contain the CPU interface flags
   and data buffer values.

   While a unit is activated, the current phase indicates the reason for the
   activation, i.e., what the simulated drive is "doing" at the moment, as
   follows:

      Idle_Phase     -- Waiting for the next command to be issued (note that
                        this phase, which indicates that the unit is not
                        scheduled, is distinct from the controller Idle_State,
                        which indicates that the controller itself is idle).

      Wait_Phase     -- Waiting for the channel data transfer program to start.

      Start_Phase    -- Waiting for the drive to come up to speed after
                        starting.

      Traverse_Phase -- Waiting for the drive to traverse an erase gap preceding
                        a data record.

      Data_Phase     -- Waiting for the drive to traverse a data record or
                        accept or return the next data bytes to be transferred
                        to or from the tape.

      Stop_Phase     -- Waiting for the drive to slow to a stop.

      Error_Phase    -- Waiting to interrupt for a command abort.

   Depending on the current command opcode and phase, a number of actions may be
   taken:

      Idle_Phase     -- Attempting to continue execution of an idle unit causes
                        a command rejection error.  For the 3000, this implies a
                        TOGGLEINXFER or TOGGLEOUTXFER or READNEXTWD signal was
                        asserted with no command in progress.

      Wait_Phase     -- If the EOD flag is set, then reject the command.
                        Otherwise, the correct type of channel transfer has
                        started, so start the tape motion and set up the start
                        phase.

      Start_Phase    -- If a write data command is executing, request the first
                        data word from the interface.  A Write File Mark command
                        sets up the traverse phase to write an erase gap if the
                        tape is at BOT and the mode is REALTIME; otherwise, the
                        data phase is set up.  A read data command reads the
                        record from the tape image file and sets up the traverse
                        or data phases, depending on whether or not an erase gap
                        was present.  A file or record positioning command
                        spaces the file in the indicated direction and sets up
                        the traverse, start, or stop phases, depending on
                        whether a gap was present and the command continues or
                        is complete.  A Write Gap command sets up the traverse
                        phase to write the gap to the file.  A rewind command
                        completes immediately or sets up the stop phase if the
                        tape is positioned at BOT, or sets up the traverse phase
                        to rewind the file.

      Traverse_Phase -- A Write Gap or rewind command sets up the stop phase.  A
                        write, read, or spacing command sets up the data phase.
                        A gap traversal that ended with an error, e.g., runaway
                        or a tape mark, sets up the stop phase.

      Data_Phase     -- For read transfers, return the next word from the record
                        buffer, or for write transfers, store the next word into
                        the record buffer, and schedule the next data phase
                        transfer if the CPU has not indicated an end-of-data
                        condition.  If it has, or if the last word of the buffer
                        has been transmitted, schedule the stop phase.  For a
                        Write File Mark command, write the tape mark to the
                        file and set up the stop phase.

      Stop_Phase     -- A write data command writes the completed record to the
                        file.  A Rewind command sets unit attention and then
                        rewinds the image file.  In all cases, idle the unit and
                        controller and mark the command as complete.

      Error_Phase    -- A command abort has delayed until the tape motion has
                        stopped, so idle the unit and assert the STINT function
                        to request an interrupt.

   Phase assignments are validated on entry, and SCPE_IERR (Internal Error) is
   returned if entry is made with a phase that is not valid for a given
   operation.

   At the completion of the current phase, the next phase is scheduled, if
   required, before returning the appropriate function set and data word to the
   caller.  If the current phase is continuing, the service activation time will
   be set appropriately.

   The commands employ the various phases as follows:

       Command                       Wait    Start   Trav    Data    Stop
     +-----------------------------+-------+-------+-------+-------+-------+
     | Select_Unit_0               |   -   |   -   |   -   |   -   |   -   |
     | Select_Unit_1               |   -   |   -   |   -   |   -   |   -   |
     | Select_Unit_2               |   -   |   -   |   -   |   -   |   -   |
     | Select_Unit_3               |   -   |   -   |   -   |   -   |   -   |
     | Clear_Controller            |   -   |   C   |   -   |   -   |   N   |
     | Read_Record                 |   X   |   R   |   t   |   D   |   N   |
     | Read_Record_with_CRCC       |   X   |   R   |   t   |   D   |   N   |
     | Read_Record_Backward        |   X   |   R   |   t   |   D   |   N   |
     | Read_File_Forward           |   X   |   R   |   t   |   D   |   N   |
     | Write_Record                |   X   |   g   |   t   |   D   |   W   |
     | Write_Record_without_Parity |   X   |   g   |   t   |   D   |   W   |
     | Write_File_Mark             |   -   |   g   |   t   |   F   |   N   |
     | Write_Gap                   |   -   |   G   |   T   |   -   |   N   |
     | Write_Gap_and_File_Mark     |   -   |   G   |   T   |   F   |   N   |
     | Forward_Space_Record        |   -   |   S   |   t   |   d   |   N   |
     | Forward_Space_File          |   -   |   S   |   t   |   d   |   N   |
     | Backspace_Record            |   -   |   S   |   t   |   d   |   N   |
     | Backspace_File              |   -   |   S   |   t   |   d   |   N   |
     | Rewind                      |   -   |   N   |   B   |   -   |   E   |
     | Rewind_Offline              |   -   |   N   |   B   |   -   |   E   |
     +-----------------------------+-------+-------+-------+-------+-------+

     Key:

       B = traverse the tape while rewinding to BOT
       C = clear the controller
       D = traverse the record while transferring data via the record buffer
       d = traverse the record without transferring data
       E = rewind the tape file
       F = write a file mark to the tape file
       G = write a gap to the tape file
       g = write a gap to the tape file only if REALTIME and BOT
       N = no action other than setting up the next phase
       R = read a record from the tape file
       S = space over a record in the tape file
       T = traverse an erase gap
       t = traverse an erase gap only if present
       W = write a record to the tape file
       X = wait for a channel transfer to start
       - = skip the phase

   For the 3000 controller, a unit preparing to read or write data must wait for
   the channel transfer to start.  The wait phase is set up on the controller
   unit, and the operation pauses until a read or write channel order is issued.
   The EOD flag is used to ensure that the correct type of order is issued.  The
   interface sets the EOD flag as part of IFGTC processing and clears it if the
   type of channel order matches the type of tape command, e.g., a read order is
   issued for a Read Record command.  If the wrong type of order is issued,
   e.g., a write order for a Read Record command, EOD will be set on entry in
   the wait state; this will cause a command reject.

   Within the data phase of a write command, the interface sets EOD to indicate
   the end of the data record.  For a read command, the interface generally
   doesn't know the length of the next record.  If the programmed transfer
   length is shorter than the record length, the interface will set EOD to
   indicate that the remainder of the data record is not wanted.  If the record
   length is shorter than the transfer length, the controller will assert the
   DVEND function to indicate that the device has ended the transfer
   prematurely.


   Implementation notes:

    1. The Forward Space Record, Forward Space File, Backspace Record, and
       Backspace File commands schedule data phases to traverse the data record,
       but not data is transferred to the interface.

    2. The 7970 ignores a rewind command if the tape is positioned at the load
       point.  The 3000 controller completes such a command immediately; as the
       Unit Ready signal from the drive never denies, drive attention is never
       set, i.e., the rewind completes without the usual unit interrupt.  The
       1000 controllers set Interface Busy status for 0.56 millisecond,
       regardless of the tape position, and complete with the Command Flag set.

    3. The 30215 controller does not detect tape runaway when spacing in the
       reverse direction.  If the support library "space record reverse" routine
       returns MTSE_RUNAWAY status, the error status is cleared, but the buffer
       length is set to 0 to indicate that the spacing did not encompass a data
       record.  After traversing the gap, the spacing will be retried.

    4. Support library errors that occur after traversing an erase gap, e.g.,
       encountering a tape mark, schedule the traversal phase for the gap but
       set the controller into the error state to terminate the command after
       traversal.

    5. If the EOD flag is set by the interface before the entire data record is
       transferred, the stop phase delay for the interrecord gap is lengthened
       by an amount corresponding to the length of the data record remaining.

    6. Data record read error status (MTSE_RECE) is initially treated as
       successful to allow the transfer of the record data to the interface.
       After the stop phase has been processed, the error status is restored to
       abort the tape command.

    7. The Clear Controller command must be scheduled on the controller unit, as
       all four tape units may be executing rewinds in progress, which must be
       allowed to complete normally.

    8. A write of a null record is reported as a tape error.  This may occur if
       a Write_Record_without_Parity (WRZ) writes an all-zeros buffer (a byte
       with zero parity won't be seen, as it has no clocking information).

    9. The 13181 controller places the CRCC and LRCC into the interface output
       data register after completion of a read or write record command.  This
       behavior is undocumented but is used by the HP 1000 tape diagnostic.  To
       provide this functionality in simulation, the controller calculates the
       CRCC and LRCC from the current record buffer if it is configured as a
       13181 and is in REALTIME mode and returns the CRCC/LRCC in an outbound
       data word at the end of a read or write transfer.  The 13181 interface
       simulator must store this word in its output data register if IFOUT is
       asserted with EOD set; the IFIN handler stores the outbound value in the
       data register normally, so no special handling is needed for this case.
       This "extra" word is not accompanied by a channel service request, so it
       does not form part of the data record transferred.

   10. The 13181 and 13183 controllers check and report odd-length records while
       spacing.  The 30215 controller checks and reports only for reads.

   11. The 13181 and 13183 controllers terminate the Forward Space File command
       at the end of the first record past the end of tape marker.

   12. The 30215 controller always indicates an even number of bytes for a partial
       transfer, even if the record itself is an odd length.  Odd-length status
       for the other controllers always reflects the last record read or spaced
       over.

   13. Erase gaps must be written to the image file during the start phase so
       that a controller clear during the traverse phase can reposition into the
       gap appropriately.

   14. Data phase trace statements number data bytes or words transferred from 1
       to the record length, rather than from 0 to the length - 1.

   15. The data record buffer size is limited.  If an attempt is made to write a
       record that is larger than the buffer, the record is truncated at the
       buffer size and marked as "bad", and a Transfer Error interrupt occurs.

       Tape diagnostics usually attempt to write a single record that
       encompasses the entire tape reel by supplying data until "end of tape"
       status is seen.  Continuing to accept and discard data after the buffer
       size is exceeded isn't practicable, as the unit position isn't updated
       until the record is written, and therefore the caller would never see the
       EOT status that is used to terminate the write.

   16. The tape drive is a synchronous device, so overrun or underrun can occur
       if the interface is not ready when the controller must transfer data.  An
       overrun occurs when the controller is ready with a tape read word (IFIN),
       but the interface buffer is full (DTRDY).  An underrun occurs when the
       controller needs a tape write word (IFOUT), but the interface buffer is
       empty (~DTRDY).  These conditions are detected by the interface and
       communicated to the controller by the OVRUN flag.

   17. Unit attention is set at the completion of a Rewind or Rewind_Offline if
       the unit is online.  The status cannot be inferred from the command, as
       the user may have set the unit offline or online explicitly before the
       rewind completed.

   18. The "%.0u" print specification in the trace call absorbs the zero
       "length" value parameter without printing when the controller unit is
       specified.
*/

static CNTLR_IFN_IBUS continue_command (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET inbound_flags, CNTLR_IBUS inbound_data)
{
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OPCODE;    /* the current command opcode */
const CNTLR_PHASE  phase  = (CNTLR_PHASE)  uptr->PHASE;     /* the current command phase */
const t_bool       service_entry = (phase > Wait_Phase);    /* TRUE if entered via unit service */
int32              unit;
TL_BUFFER          data_byte;
t_mtrlnt           error_flag;
BYTE_SELECTOR      selector;
DRIVE_PROPS const  *pptr;
CNTLR_IFN_IBUS     outbound = NO_ACTION;
t_bool             complete = FALSE;

unit = (int32) (uptr - cvptr->device->units);           /* get the unit number */

dpprintf (cvptr->device, TL_DEB_STATE, "%s %s %s phase entered from %s\n",
          unit_names [unit], opcode_names [opcode], phase_names [phase],
          (service_entry ? "service" : "interface"));


switch (phase) {                                        /* dispatch the phase */

    case Idle_Phase:
        reject_command (cvptr, uptr);                   /* reject if no command is in progress */
        break;


    case Wait_Phase:
        if (inbound_flags & EOD)                        /* if the EOD flag is set */
            reject_command (cvptr, uptr);               /*   then the wrong channel order was issued */

        else {                                          /* otherwise the channel is ready to transfer */
            uptr->OPCODE = Invalid_Opcode;              /*   so clear the controller command */
            uptr->PHASE = Idle_Phase;                   /*     and idle the unit */

            unit = (int32) cvptr->unit_selected;        /* get the selected unit number */
            uptr = cvptr->device->units + unit;         /*   and unit pointer */

            uptr->PHASE = Start_Phase;                  /* set up the start phase */
            uptr->wait = cvptr->dlyptr->overhead;       /*   and the initial controller delay */

            if (sim_tape_bot (uptr))                    /* if the tape is positioned at the BOT */
                uptr->wait += cvptr->dlyptr->bot_start; /*   then the start delay is longer */
            else                                        /*     than it would if the position */
                uptr->wait += cvptr->dlyptr->ir_start;  /*       is between records */
            }
        break;


    case Start_Phase:
        dpprintf (cvptr->device, TL_DEB_INCO, "Unit %d %s started at position %" T_ADDR_FMT "u\n",
                  unit, opcode_names [opcode], uptr->pos);

        pptr = &drive_props [PROP_INDEX (uptr)];        /* get the drive property pointer */

        cvptr->initial_position = uptr->pos;            /* save the initial tape position */

        switch (opcode) {                               /* dispatch the current operation */

            case Clear_Controller:
                tl_clear (cvptr);                       /* clear the controller */

                uptr->PHASE = Stop_Phase;               /* schedule the completion */
                uptr->wait = cvptr->dlyptr->ir_start;   /*   after motion ceases */
                break;


            case Read_Record:
            case Read_Record_with_CRCC:
                outbound =                                      /* read the next record from the file */
                   call_tapelib (cvptr, uptr, lib_read_fwd, TL_MAXREC);

                if ((outbound & SCPE) == NO_FLAGS) {            /* if the read succeeded */
                    if (cvptr->length & 1)                      /*   then if the record length was odd */
                        cvptr->status |= CST_ODDLEN;            /*     then set the corresponding status */

                    if (cvptr->gaplen) {                        /* if a gap was present */
                        uptr->PHASE = Traverse_Phase;           /*   then schedule the gap traversal phase */
                        uptr->wait =                            /*     and the traversal time */
                           cvptr->gaplen * cvptr->dlyptr->data_xfer;
                        }

                    else {                                      /* otherwise there was no gap */
                        uptr->PHASE = Data_Phase;               /*   so proceed to the data phase */
                        uptr->wait =                            /*     with the data delay time */
                           2 * cvptr->dlyptr->data_xfer;
                        }

                    if (pptr->bpi <= 800                        /* if this is an NRZI drive */
                      && opcode == Read_Record_with_CRCC        /*   and the CRCC was requested */
                      || cvptr->device->flags & DEV_REALTIME    /* or the mode is REALTIME */
                      && cvptr->type == HP_13181)               /*   for the 13181 controller */
                        add_crcc_lrcc (cvptr, opcode);          /*     then add the CRCC/LRCC to the buffer */
                    }
                break;


            case Write_Record:
            case Write_Record_without_Parity:
                outbound = RQSRV;                               /* request the first data word from the channel */

            /* fall into the Write_File_Mark case */

            case Write_File_Mark:
                if ((cvptr->device->flags & DEV_REALTIME) == 0  /* if fast timing is enabled */
                  || sim_tape_bot (uptr) == FALSE) {            /*   or the tape is not positioned at the BOT */
                    uptr->PHASE = Data_Phase;                   /*     then proceed to the data phase */
                    uptr->wait = 2 * cvptr->dlyptr->data_xfer;  /*       with the data delay time */
                    break;
                    }                                           /* otherwise an initial gap is needed */

            /* fall into the Write_Gap case */

            case Write_Gap:
                outbound |=                                     /* erase the gap */
                   call_tapelib (cvptr, uptr, lib_write_gap, pptr->gap_size);

                if ((outbound & SCPE) == NO_FLAGS) {            /* if the erase succeeded */
                    uptr->PHASE = Traverse_Phase;               /*   then set up to traverse the it */

                    cvptr->gaplen = (pptr->gap_size * pptr->bpi) / 10;      /* set the length of the gap */
                    uptr->wait = cvptr->gaplen * cvptr->dlyptr->data_xfer;  /*   and the traversal time */
                    }
                break;


            case Forward_Space_Record:
            case Forward_Space_File:
                outbound =                                  /* space forward over the next record in the file */
                   call_tapelib (cvptr, uptr, lib_space_fwd, 0);

                if ((outbound & SCPE) == NO_FLAGS)          /* if the spacing succeeded */
                    if (cvptr->gaplen > 0) {                /*   then if a gap is present */
                        uptr->PHASE = Traverse_Phase;       /*     then schedule the traversal phase */
                        uptr->wait =                        /*       for the gap */
                           cvptr->gaplen * cvptr->dlyptr->data_xfer;
                        }

                    else {                                  /* otherwise no gap intervenes */
                        uptr->PHASE = Data_Phase;           /*   so schedule the data phase */
                        uptr->wait =                        /*     to space over the record */
                           cvptr->length * cvptr->dlyptr->data_xfer;
                        }
                break;


            case Backspace_Record:
            case Backspace_File:
                outbound =                                  /* space in reverse over the previous record in the file */
                   call_tapelib (cvptr, uptr, lib_space_rev, 0);

                if ((outbound & SCPE) == NO_FLAGS)          /* if the spacing succeeded */
                    if (cvptr->gaplen > 0) {                /*   then if a gap is present */
                        uptr->PHASE = Traverse_Phase;       /*     then schedule the traversal phase */
                        uptr->wait =                        /*       for the gap */
                           cvptr->gaplen * cvptr->dlyptr->data_xfer;
                        }

                    else {                                  /* otherwise no gap intervenes */
                        uptr->PHASE = Data_Phase;           /*   so schedule the data phase */
                        uptr->wait =                        /*     to space over the record */
                           cvptr->length * cvptr->dlyptr->data_xfer;
                        }
                break;


            case Rewind_Offline:
                uptr->flags |= UNIT_OFFLINE;            /* set the unit offline immediately */

            /* fall into the Rewind case */

            case Rewind:
                outbound = end_command (cvptr, uptr);   /* release the controller */

                if (sim_tape_bot (uptr))                /* if the tape is positioned at the load point */
                    complete = TRUE;                    /*   then the command is complete */

                else {                                  /* otherwise the tape must be rewound */
                    uptr->flags |= UNIT_REWINDING;      /*   so set rewinding status */

                    uptr->OPCODE = opcode;              /* restore the opcode cleared by the end routine */
                    uptr->PHASE = Traverse_Phase;       /*   and proceed to the rewinding phase */

                    uptr->wait =                        /* base the traversal time on the current tape position */
                       (int32) ((uptr->pos * cvptr->dlyptr->rewind_rate) / pptr->bpi);
                    }
                break;


            case Select_Unit_0:                         /* the command is invalid for this state */
            case Select_Unit_1:                         /* the command is invalid for this state */
            case Select_Unit_2:                         /* the command is invalid for this state */
            case Select_Unit_3:                         /* the command is invalid for this state */
            case Read_Record_Backward:                  /* the command is invalid for this state */
            case Read_File_Forward:                     /* the command is invalid for this state */
            case Write_Gap_and_File_Mark:               /* the command is invalid for this state */
            case Invalid_Opcode:                        /* the command is invalid for this state */
                outbound =                              /* abort the command and stop the simulation */
                   abort_command (cvptr, uptr, SCPE_IERR);
                break;
            }                                           /* end of the start phase operation dispatch */

        break;                                          /* end of the start phase handlers */


    case Traverse_Phase:
        switch (opcode) {                               /* dispatch the current operation */

            case Read_Record:
            case Read_Record_with_CRCC:
            case Write_Record:
            case Write_Record_without_Parity:
            case Write_File_Mark:
            case Forward_Space_Record:
            case Forward_Space_File:
                uptr->PHASE = Data_Phase;                   /* proceed to the data phase */
                uptr->wait = 2 * cvptr->dlyptr->data_xfer;  /*   for a data transfer as needed */
                break;


            case Write_Gap:
               uptr->PHASE = Stop_Phase;                /* proceed to the completion phase */
               uptr->wait = cvptr->dlyptr->ir_start;    /*   after the interrecord stop delay */
               break;


            case Backspace_Record:
            case Backspace_File:
                if (cvptr->length == 0)                 /* if a tape runaway occurred but was ignored */
                    uptr->PHASE = Start_Phase;          /*   then try the spacing operation again */
                else                                    /* otherwise */
                    uptr->PHASE = Data_Phase;           /*   proceed to the data phase to skip the record */

                uptr->wait = 2 * cvptr->dlyptr->data_xfer;
                break;


            case Rewind:
            case Rewind_Offline:
                uptr->PHASE = Stop_Phase;                   /* motion is complete, so stop the drive */
                uptr->wait = cvptr->dlyptr->rewind_stop;    /*   after the load point search finishes */
                break;


            case Select_Unit_0:                         /* the command is invalid for this state */
            case Select_Unit_1:                         /* the command is invalid for this state */
            case Select_Unit_2:                         /* the command is invalid for this state */
            case Select_Unit_3:                         /* the command is invalid for this state */
            case Clear_Controller:                      /* the command is invalid for this state */
            case Read_Record_Backward:                  /* the command is invalid for this state */
            case Read_File_Forward:                     /* the command is invalid for this state */
            case Write_Gap_and_File_Mark:               /* the command is invalid for this state */
            case Invalid_Opcode:                        /* the command is invalid for this state */
                outbound =                              /* abort the command and stop the simulation */
                   abort_command (cvptr, uptr, SCPE_IERR);
                break;
            }                                           /* end of the traverse phase operation dispatch */


            if (cvptr->state > Busy_State) {            /* if an error condition exists */
                uptr->PHASE = Stop_Phase;               /*   then terminate the command */
                uptr->wait = cvptr->dlyptr->ir_start;   /*     after tape motion stops */
                }

        break;                                          /* end of the traverse phase handlers */


    case Data_Phase:
        switch (opcode) {                               /* dispatch the current operation */

            case Read_Record:
            case Read_Record_with_CRCC:
                if (cvptr->index == cvptr->length           /* if record data is exhausted */
                  || inbound_flags & EOD) {                 /*   or the channel transfer is complete */
                    uptr->PHASE = Stop_Phase;               /*   then set up the stop phase processing */
                    uptr->wait = (cvptr->length - cvptr->index)
                                   * cvptr->dlyptr->data_xfer
                                   + cvptr->dlyptr->ir_start;

                    if ((inbound_flags & EOD) == NO_FLAGS)  /* if the record ended before than the channel finished */
                        cvptr->state = End_State;           /*   then note that the device ended the transfer */

                    if (cvptr->device->flags & DEV_REALTIME /* if the mode is REALTIME */
                      && cvptr->type == HP_13181)           /*   for the 13181 controller */
                        outbound =                          /*     then return the CRCC and LRCC */
                           IFIN | TO_WORD (cvptr->buffer [cvptr->length + 0],
                                           cvptr->buffer [cvptr->length + 1]);
                    }

                else {                                          /* otherwise there is more data to transfer */
                    if (cvptr->type == HP_IB) {                 /* if the controller uses HP-IB */
                        outbound =                              /*   then transfer one byte at a time */
                           cvptr->buffer [cvptr->index++];      /*     to the data register */

                        dpprintf (cvptr->device, TL_DEB_XFER, "Unit %d %s byte %u is %03o\n",
                                  unit, opcode_names [opcode],
                                  cvptr->index, outbound);

                        uptr->wait = cvptr->dlyptr->data_xfer;  /* schedule the next byte transfer */
                        }

                    else {                                      /* otherwise we transfer full words */
                        outbound =                              /* move data to the high byte */
                           TO_WORD (cvptr->buffer [cvptr->index++], 0);

                        if (cvptr->index < cvptr->length)       /* if there is more to transfer */
                            outbound |=                         /*   then merge in the low byte */
                              cvptr->buffer [cvptr->index++];

                        dpprintf (cvptr->device, TL_DEB_XFER, "Unit %d %s word %u is %06o\n",
                                  unit, opcode_names [opcode],
                                  (cvptr->index + 1) / 2, outbound);

                        uptr->wait = 2 * cvptr->dlyptr->data_xfer;  /* schedule the next word transfer */
                        }

                    outbound |= IFIN | RQSRV;               /* tell the interface that more data is available */
                    }
                break;


            case Write_Record:
            case Write_Record_without_Parity:
                if (cvptr->index == TL_MAXREC) {            /* if the buffer is full */
                    uptr->PHASE = Stop_Phase;               /*   then set up the stop phase processing */
                    uptr->wait = cvptr->dlyptr->ir_start;   /*     after the motion stops */

                    cvptr->call_status = MTSE_RECE;         /* the record will be truncated, so mark it as bad */

                    outbound = IFOUT;                       /* tell the interface that we have the data */
                    }

                else {                                          /* otherwise the buffer still has space */
                    if (cvptr->type == HP_IB) {                 /* if the controller uses HP-IB */
                        cvptr->buffer [cvptr->index++] =        /*   then transfer the lower byte */
                           LOWER_BYTE (inbound_data);           /*     to the buffer */

                        cvptr->length = cvptr->length + 1;      /* update the buffer length */

                        uptr->wait = cvptr->dlyptr->data_xfer;  /* schedule the next byte transfer */

                        dpprintf (cvptr->device, TL_DEB_XFER, "Unit %d %s byte %u is %06o\n",
                                  unit, opcode_names [opcode],
                                  cvptr->index, inbound_data);
                        }

                    else {                                                      /* otherwise we transfer full words */
                        for (selector = upper; selector <= lower; selector++) { /*   so unpack the data bytes */
                            if (selector == upper)                              /* if the upper byte is selected */
                                data_byte = UPPER_BYTE (inbound_data);          /*   then get it */
                            else                                                /* otherwise */
                                data_byte = LOWER_BYTE (inbound_data);          /*   get the lower byte */

                            if (opcode == Write_Record_without_Parity           /* if writing without parity */
                              && drive_props [PROP_INDEX (uptr)].bpi <= 800) {  /*   and this is an NRZI drive */
                                if (odd_parity [data_byte])                     /*     then if the parity is wrong */
                                    cvptr->call_status = MTSE_RECE;             /*       then the CRCC will be wrong */

                                if (data_byte == 0)                     /* a zero byte without parity */
                                    continue;                           /*   will be read as a dropout, not data */
                                }                                       /*     which may generate an odd record length */

                            cvptr->buffer [cvptr->index++] = data_byte; /* transfer the byte */
                            cvptr->length = cvptr->length + 1;          /*  and update the buffer length */
                            }

                        uptr->wait = 2 * cvptr->dlyptr->data_xfer;      /* schedule the next word transfer */

                        dpprintf (cvptr->device, TL_DEB_XFER, "Unit %d %s word %u is %06o\n",
                                  unit, opcode_names [opcode],
                                  (cvptr->index + 1) / 2, inbound_data);
                        }

                    if (inbound_flags & EOD) {                  /* if the transfer is now complete */
                        uptr->PHASE = Stop_Phase;               /*   then set up the stop phase processing */
                        uptr->wait = cvptr->dlyptr->ir_start;   /*     after the motion stops */

                        if (cvptr->device->flags & DEV_REALTIME /* if the mode is REALTIME */
                          && cvptr->type == HP_13181) {         /*   for the 13181 controller */
                            add_crcc_lrcc (cvptr, opcode);      /*     then add the CRCC/LRCC to the buffer */

                            outbound =                          /* return the CRCC and LRCC */
                               TO_WORD (cvptr->buffer [cvptr->length + 0],
                                        cvptr->buffer [cvptr->length + 1]);
                            }

                        outbound |= IFOUT;                      /* tell the interface that we have the data */
                        }

                    else                                        /* otherwise tell the interface that */
                        outbound = IFOUT | RQSRV;               /*   we have the data and need more */
                    }
                break;


            case Write_File_Mark:
            case Write_Gap_and_File_Mark:
                outbound =                                  /* write the file (tape) mark */
                   call_tapelib (cvptr, uptr, lib_write_tmk, 0);

                if ((outbound & SCPE) == NO_FLAGS) {        /* if the write succeeded */
                    cvptr->status |= CST_EOF;               /*   then set EOF status */
                    uptr->PHASE = Stop_Phase;               /*     and set up the stop phase processing */
                    uptr->wait = cvptr->dlyptr->ir_start;   /*       after the motion stops */
                    }
                break;


            case Forward_Space_Record:
            case Backspace_Record:
                uptr->PHASE = Stop_Phase;               /* set up the stop phase processing */
                uptr->wait = cvptr->dlyptr->ir_start;   /*   after the motion stops */
                break;


            case Forward_Space_File:
                if (sim_tape_eot (uptr)                     /* if the EOT was seen */
                  && (cvptr->type == HP_13181               /*   and this is */
                  || cvptr->type == HP_13183)) {            /*     an HP 1000 controller */
                    uptr->PHASE = Stop_Phase;               /*       then the command ends */
                    uptr->wait = cvptr->dlyptr->ir_start;   /*         at this record */
                    break;                                  /*           after the motion stops */
                    }

            /* otherwise fall into the Backspace_File case */

            case Backspace_File:
                uptr->PHASE = Start_Phase;                  /* set up to space over the next record */
                uptr->wait = 2 * cvptr->dlyptr->ir_start;   /*   after spacing over the interrecord gap */
                break;


            case Select_Unit_0:                         /* the command is invalid for this state */
            case Select_Unit_1:                         /* the command is invalid for this state */
            case Select_Unit_2:                         /* the command is invalid for this state */
            case Select_Unit_3:                         /* the command is invalid for this state */
            case Clear_Controller:                      /* the command is invalid for this state */
            case Read_Record_Backward:                  /* the command is invalid for this state */
            case Read_File_Forward:                     /* the command is invalid for this state */
            case Write_Gap:                             /* the command is invalid for this state */
            case Rewind:                                /* the command is invalid for this state */
            case Rewind_Offline:                        /* the command is invalid for this state */
            case Invalid_Opcode:                        /* the command is invalid for this state */
                outbound =                              /* abort the command and stop the simulation */
                   abort_command (cvptr, uptr, SCPE_IERR);
                break;
            }                                           /* end of the data phase operation dispatch */

        break;                                          /* end of the data phase handlers */


    case Stop_Phase:
        switch (opcode) {                               /* dispatch the operation command */

            case Clear_Controller:                      /* no additional completion action is required */
                break;


            case Read_Record:
            case Read_Record_with_CRCC:
                if (inbound_flags & OVRUN) {            /* if a data overrun occurred */
                    cvptr->status |= CST_TIMERR;        /*   then indicate a timing error */
                    cvptr->state = Error_State;
                    }

                if (cvptr->type == HP_30215             /* if this is the 3000 controller */
                  && cvptr->index < cvptr->length)      /*   and a partial record was transferred */
                    cvptr->status &= ~CST_ODDLEN;       /*     then the byte count is always even */

                cvptr->length = cvptr->index;           /* set the length to report the amount actually transferred */
                break;


            case Write_Record:
            case Write_Record_without_Parity:
                if (cvptr->length > 0) {                    /* if there is data in the buffer */
                    if (cvptr->call_status == MTSE_RECE)    /*   then if the data is in error */
                        error_flag = MTR_ERF;               /*     then mark the record as bad */
                    else                                    /*   otherwise */
                        error_flag = 0;                     /*     mark the record as good */

                    outbound =                              /* write the record */
                       call_tapelib (cvptr, uptr, lib_write, error_flag);

                    if (inbound_flags & OVRUN) {            /* if a data underrun occurred */
                        cvptr->status |= CST_TIMERR;        /*   then indicate a timing error */
                        cvptr->state = Error_State;
                        }
                    }

                else {                                  /* otherwise the buffer is empty */
                    cvptr->status |= CST_DATAERR;       /*   which reports as a data error */
                    cvptr->state = Error_State;         /*     as it cannot be written */
                    }
                break;


            case Forward_Space_Record:
            case Forward_Space_File:
            case Backspace_Record:
            case Backspace_File:
                if (cvptr->type != HP_30215             /* if this is not the 3000 controller */
                  && cvptr->length & 1)                 /*   and the record length is odd */
                    cvptr->status |= CST_ODDLEN;        /*     then set the corresponding status */

            /* fall into the Write_File_Mark case */

            case Write_File_Mark:
            case Write_Gap:
                if (cvptr->state == End_State)          /* a BOT or EOF indication */
                    cvptr->state = Busy_State;          /*   is the normal completion for these commands */
                break;                                  /* no additional completion action required */


            case Rewind:
            case Rewind_Offline:
                if ((uptr->flags & UNIT_OFFLINE) == 0)      /* if the unit is online */
                    cvptr->unit_attention |= 1u << unit;    /*   then attention sets on rewind completion */

                uptr->flags &= ~UNIT_REWINDING;         /* clear rewinding status */

                outbound =                              /* rewind the tape (which always succeeds) */
                   call_tapelib (cvptr, uptr, lib_rewind, 0);

                complete = TRUE;                        /* mark the command as complete */
                uptr->PHASE = Idle_Phase;               /*   but skip the normal completion action */
                break;


            case Select_Unit_0:                         /* the command is invalid for this state */
            case Select_Unit_1:                         /* the command is invalid for this state */
            case Select_Unit_2:                         /* the command is invalid for this state */
            case Select_Unit_3:                         /* the command is invalid for this state */
            case Read_Record_Backward:                  /* the command is invalid for this state */
            case Read_File_Forward:                     /* the command is invalid for this state */
            case Write_Gap_and_File_Mark:               /* the command is invalid for this state */
            case Invalid_Opcode:                        /* the command is invalid for this state */
                outbound =                              /* abort the command and stop the simulation */
                   abort_command (cvptr, uptr, SCPE_IERR);
                break;
            }                                           /* end of the stop phase operation dispatch */


        if (cvptr->call_status == MTSE_RECE)            /* a bad data record */
            cvptr->state = Error_State;                 /*   is now treated as an error */

        if (uptr->PHASE == Stop_Phase) {                /* if the stop completed normally */
            outbound |= end_command (cvptr, uptr);      /*   then terminate the command */
            complete = TRUE;                            /*     and mark it as complete */
            }

        break;                                          /* end of the stop phase handlers */


    case Error_Phase:
        outbound = end_command (cvptr, uptr);           /* end the command with an interrupt */
        break;

    }                                                   /* end of phase dispatching */


if (uptr->wait != NO_EVENT)                             /* if the unit has been scheduled */
    activate_unit (cvptr, uptr);                        /*   then activate it */

if (complete) {                                         /* if the command is complete */
    dpprintf (cvptr->device, TL_DEB_INCO,               /*   then report the final tape position */
              "Unit %d %s completed at position %" T_ADDR_FMT "u\n",
              unit, opcode_names [opcode], uptr->pos);

    dpprintf (cvptr->device, TL_DEB_CMD, (cvptr->length > 0
                                            ? "Unit %d %s of %u-byte record %s\n"
                                            : "Unit %d %s %.0u%s\n"),
              unit, opcode_names [opcode], cvptr->length,
              status_name [cvptr->call_status]);
    }

return outbound;                                        /* return the data word and function set */
}


/* End the current command.

   The command executing on the specified unit is ended, and the unit and
   controller are idled.  The set of functions appropriate to the controller
   state is returned to the caller.
*/

static CNTLR_IFN_IBUS end_command (CVPTR cvptr, UNIT *uptr)
{
static const CNTLR_IFN_SET end_functions [] = {         /* indexed by CNTLR_STATE */
    NO_FUNCTIONS,                                       /*   Idle_State  */
    RQSRV | STCFL,                                      /*   Busy_State  */
    DVEND | RQSRV | STCFL,                              /*   End_State   */
    STINT | STCFL                                       /*   Error_State */
    };
CNTLR_IFN_SET outbound;

outbound = end_functions [cvptr->state];                /* get the state-specific end functions */

uptr->OPCODE = Invalid_Opcode;                          /* clear the command */
uptr->PHASE = Idle_Phase;                               /*   and idle the unit */

cvptr->state = Idle_State;                              /* idle the controller */
cvptr->status &= ~CST_IFBUSY;                           /*   and the interface */

return (CNTLR_IFN_IBUS) outbound;                       /* return the function set */
}


/* Poll the tape drives for drive attention status.

   The controller's drive attention bitmap is checked to determine if any tape
   drive unit is requesting attention.  If so, then the units are scanned in
   ascending order to determine the first unit with a request pending.  That
   unit's flag is cleared, and a Set Drive Attention interface function is
   returned, along with the unit number on the interface data bus.  If no unit
   is requesting attention, the routine returns an empty function set.

   The HP 3000 controller sets drive attention when a Rewind command completes
   or a unit is set online from an offline condition.  The controller must poll
   the drives for attention requests when it is idle and interrupts are allowed
   by the CPU interface.

   HP 1000 controllers do not call this routine.  Rewind completion and
   offline-to-online transitions must be determined by requesting controller
   status.
*/

static CNTLR_IFN_IBUS poll_drives (CVPTR cvptr)
{
int32 unit;

dpprintf (cvptr->device, TL_DEB_INCO, "Controller polled drives for attention\n");

unit = 0;                                               /* scan the units in numerical order */

while (cvptr->unit_attention)                           /* loop through the attention bits */
    if (cvptr->unit_attention & 1 << unit) {            /*   looking for the first one set */
        cvptr->unit_attention &= ~(1 << unit);          /*     and then clear it */

        dpprintf (cvptr->device, TL_DEB_INCO, "Unit %u requested attention\n",
                  unit);

        cvptr->unit_selected = unit;                    /* select the unit requesting attention */

        return (CNTLR_IFN_IBUS) (DATTN | unit);         /* return drive attention and the unit number */
        }

    else                                                /* if this unit isn't requesting attention */
        unit = unit + 1;                                /*   then look at the next one */

return NO_ACTION;                                       /* no drives are requesting attention */
}


/* Call a simulator tape support library routine.

   The simulator tape support library routine specified by the "lib_call"
   parameter is called for the controller specified by "cvptr" and the unit
   specified by "uptr".  For the lib_read_fwd and lib_read_rev calls, the
   "parameter" value is the available buffer size in bytes.  For the lib_write
   call, the parameter is 0 to mark the record as good or MTR_ERF to mark it as
   bad.  For the lib_write_gap call, the parameter is the gap length in tenths
   of an inch.  The parameter value is ignored for the other calls.

   After calling the specified routine, the returned status is examined and
   translated into the appropriate controller status values.  Any status other
   than "call succeeded" or "call succeeded but with bad record data" asserts
   the SCPE function and returns the corresponding SCPE status code.
   Recoverable errors, e.g., encountering a tape mark, return SCPE_OK to the
   event service routine to allow simulation to continue.  Fatal errors, e.g., a
   corrupt tape image file, cause simulation stops.  If simulation is resumed by
   the user, the current tape command fails and sets tape error status.

   The caller must test the returned bus value for the SCPE function before
   continuing with the current phase processing.  If it is present, an abort has
   occurred, and the rest of the phase processing should be bypassed, as the
   phase and timing have been set appropriately by this routine.


   Implementation notes:

    1. If the parameter accompanying a lib_write call is MTR_ERF, and the write
       succeeded, then report a read-after-write failure by setting the call
       status explicitly to indicate a bad record.

    2. The simulator tape support library routines skip over erase gaps when
       reading or spacing.  To recover the gap size for use in calculating the
       motion delay, the difference between the initial and final tape
       positions, less the difference corresponding to the returned data record,
       determines the gap size.

    3. The position delta cannot be calculated by taking the absolute value of
       the difference because both position values are of type t_addr, which is
       unsigned and therefore yields an unsigned result.  Moreover, the
       positions cannot be cast to signed values because they may be either 32-
       or 64-bit values, depending on USE_ADDR64.

    4. The calculated "gap length" is actually the rewind distance for the
       Rewind and Rewind_Offline commands.

    5. When a command is started, the current record length is initialized to 0.
       Support library routines that return MTSE_OK or MTSE_RECE may (e.g.,
       sim_tape_rdrecf) or may not (e.g., sim_tape_wrtmk) set the record length.
       Therefore, when processing these two status returns, cvptr->length > 0
       indicates that the command has associated data, whereas cvptr->length = 0
       indicates that no data are present.

    6. The record length value returned by support library routines is not valid
       unless the call succeeds.  Therefore, we set the length to zero
       explicitly for all status returns other than MTSE_OK and MTSE_RECE.

    7. The CNTLR_IFN_IBUS returned value depends on the SCPE error codes fitting
       into the lower 16 bits.

    8. The 30215 controller does not detect tape runaway when spacing in the
       reverse direction.  If the support library "space record reverse" routine
       returns MTSE_RUNAWAY status, the error status is cleared, but the buffer
       length is set to 0 to indicate that the spacing did not encompass a data
       record.  After traversing the gap, the spacing will be retried.

    9. The MTSE_WRP status return shouldn't occur, as write-protection status is
       checked before initiating a write command.  However, a user might detach
       and then reattach a file with read-only status in the middle of the
       command, so we detect and handle this condition.

   10. In hardware, if a tape unit is taken offline while a transfer is in
       progress, the 1000 and 3000 controllers will hang.  They check for Unit
       Ready status at the start of the transfer but then wait in a loop for
       Read Clock or End of Block.  Neither signal will occur if the drive has
       been taken offline.

       In simulation, taking a unit offline with the SET <unit> OFFLINE command
       allows the current command to complete normally before Unit Ready is
       denied.  Taking a unit offline with the DETACH <unit> command causes a
       "Unit not attached" simulator stop.
*/

static CNTLR_IFN_IBUS call_tapelib (CVPTR cvptr, UNIT *uptr, TAPELIB_CALL lib_call, t_mtrlnt parameter)
{
t_bool         do_gap, do_data;
int32          unit;
uint32         gap_inches, gap_tenths;
CNTLR_IFN_IBUS result = (CNTLR_IFN_IBUS) NO_FUNCTIONS;  /* the expected case */

switch (lib_call) {                                     /* dispatch to the selected routine */

    case lib_space_fwd:                                 /* space record forward */
        cvptr->call_status = sim_tape_sprecf (uptr, &cvptr->length);
        break;

    case lib_space_rev:                                 /* space record reverse */
        cvptr->call_status = sim_tape_sprecr (uptr, &cvptr->length);
        break;

    case lib_read_fwd:                                  /* read record forward */
        cvptr->call_status = sim_tape_rdrecf (uptr, cvptr->buffer,
                                              &cvptr->length, parameter);
        break;

    case lib_read_rev:                                  /* read record reverse */
        cvptr->call_status = sim_tape_rdrecr (uptr, cvptr->buffer,
                                              &cvptr->length, parameter);
        break;

    case lib_write:                                     /* write record forward */
        cvptr->call_status = sim_tape_wrrecf (uptr, cvptr->buffer,
                                              parameter | cvptr->length);

        if (parameter && cvptr->call_status == MTSE_OK) /* if the record is bad and the write succeeded */
            cvptr->call_status = MTSE_RECE;             /*   then report a read-after-write failure */
        break;

    case lib_write_gap:                                 /* write erase gap */
        cvptr->call_status = sim_tape_wrgap (uptr, (uint32) parameter);
        break;

    case lib_write_tmk:                                 /* write tape mark */
        cvptr->call_status = sim_tape_wrtmk (uptr);
        break;

    case lib_rewind:                                    /* rewind tape */
        cvptr->call_status = sim_tape_rewind (uptr);
        break;
    }


if (cvptr->initial_position < uptr->pos)                                /* calculate the preliminary gap size */
    cvptr->gaplen = (t_mtrlnt) (uptr->pos - cvptr->initial_position);   /*   for either forward motion */
else                                                                    /*     or */
    cvptr->gaplen = (t_mtrlnt) (cvptr->initial_position - uptr->pos);   /*       for reverse motion */

switch (cvptr->call_status) {                           /* dispatch on the call status */

    case MTSE_RECE:                                     /* record data in error */
        cvptr->status |= CST_DATAERR;                   /* report as a data error */

    /* fall into the MTSE_OK case */

    case MTSE_OK:                                       /* operation succeeded */
        if (cvptr->length > 0)                          /* if data is present */
            cvptr->gaplen -= (cvptr->length + 1 & ~1)   /*   then reduce the calculated gap length */
                             + 2 * sizeof (t_mtrlnt);   /*     by the rounded record length and marker sizes */
        break;


    case MTSE_TMK:                                      /* tape mark encountered */
        cvptr->gaplen -= sizeof (t_mtrlnt);             /* reduce the gap length by the metadata marker size */

    /* fall into the MTSE_EOM case */

    case MTSE_EOM:                                      /* end of medium encountered */
        cvptr->status |= CST_EOF;                       /* set the EOF status */

        if (cvptr->type == HP_13181)                    /* the HP 1000 NRZI controller */
            cvptr->status |= CST_ODDLEN;                /*   also sets odd length status for a tape mark */

    /* fall into the MTSE_BOT case */

    case MTSE_BOT:                                      /* beginning of tape encountered */
        cvptr->state = End_State;                       /* indicate a device end condition */

        if (cvptr->gaplen > 0) {                        /* if a gap is present */
            uptr->PHASE = Traverse_Phase;               /*   then traverse it first */
            uptr->wait =                                /*     before stopping */
               cvptr->gaplen * cvptr->dlyptr->data_xfer;
            }

        else {                                          /* otherwise */
            uptr->PHASE = Stop_Phase;                   /*   set up the stop phase processing */
            uptr->wait = cvptr->dlyptr->ir_start;       /*     after motion stops */
            }

        cvptr->length = 0;                              /* clear the record length */
        result = SCP_STATUS (SCPE_OK);                  /*   and indicate a recoverable error */
        break;


    case MTSE_RUNAWAY:                                  /* tape runaway */
        if (lib_call == lib_space_rev                   /* the HP 3000 controller does not recognize */
          && cvptr->type == HP_30215)                   /*   tape runaway during reverse motion */
            cvptr->call_status = MTSE_OK;               /*     so ignore it if it's encountered */

        else {                                          /* otherwise */
            cvptr->state = Error_State;                 /*   indicate a terminal error */
            cvptr->status |= CST_RUNAWAY;               /*     and report the cause */

            uptr->PHASE = Traverse_Phase;               /* traverse the gap */
            uptr->wait =                                /*   before stopping */
               cvptr->gaplen * cvptr->dlyptr->data_xfer;

            result = SCP_STATUS (SCPE_OK);              /* indicate a recoverable error */
            }

        cvptr->length = 0;                              /* clear the record length */
        break;


    case MTSE_FMT:                                          /* illegal tape image format */
        result = abort_command (cvptr, uptr, SCPE_FMT);     /* abort the command and stop the simulation */
        break;


    case MTSE_UNATT:                                        /* unit unattached */
        tl_detach (uptr);                                   /* resyncronize the attachment status */
        result = abort_command (cvptr, uptr, SCPE_UNATT);   /* abort the command and stop the simulation */
        break;


    case MTSE_INVRL:                                        /* invalid record length */
        result = abort_command (cvptr, uptr, SCPE_MTRLNT);  /* abort the command and stop the simulation */
        break;


    case MTSE_IOERR:                                        /* host system I/O error */
        result = abort_command (cvptr, uptr, SCPE_IOERR);   /* abort the command and stop the simulation */
        break;


    case MTSE_WRP:                                          /* write protected */
        uptr->STATUS |= UST_WRPROT;                         /* resynchronize the write protection status */
        result = abort_command (cvptr, uptr, SCPE_NORO);    /* abort the command and stop the simulation */
        break;


    default:                                                /* unanticipated error */
        result = abort_command (cvptr, uptr, SCPE_IERR);    /* abort the command and stop the simulation */
        break;
    }                                                   /* end of the call status dispatching */


if (DPPRINTING (cvptr->device, TL_DEB_INCO)) {          /* if tracing is enabled */
    unit = (int32) (uptr - cvptr->device->units);       /*   then get the unit number */

    do_data =                                           /* TRUE if the data record length is valid and present */
       lib_props [lib_call].data_is_valid && cvptr->length > 0;

    do_gap =                                            /* TRUE if the erase gap length is valid and present */
       lib_props [lib_call].gap_is_valid && cvptr->gaplen > 0;

    if (cvptr->gaplen > 0) {                            /* if a gap or rewind spacing exists */
        gap_inches =                                    /*   then calculate the movement in inches */
           cvptr->gaplen / drive_props [PROP_INDEX (uptr)].bpi;
        gap_tenths =                                    /*     and tenths of an inch */
           ((10 * cvptr->gaplen) / drive_props [PROP_INDEX (uptr)].bpi) % 10;
        }

    if (do_gap && do_data)                              /* if both gap and data are present */
        hp_debug (cvptr->device, TL_DEB_INCO,           /*   then report both objects */
                  "Unit %d %s call of %u.%u-inch erase gap and %u-word record %s\n",
                  unit, lib_props [lib_call].action,
                  gap_inches, gap_tenths,               /*     using the movement calculated above */
                  (cvptr->length + 1) / 2,
                  status_name [cvptr->call_status]);

    else if (do_data)                                   /* otherwise if data only are present */
        hp_debug (cvptr->device, TL_DEB_INCO,           /*   then report the record length */
                  "Unit %d %s call of %u-word record %s\n",
                  unit, lib_props [lib_call].action,
                  (cvptr->length + 1) / 2,
                  status_name [cvptr->call_status]);

    else if (do_gap)                                    /* otherwise if motion only is present */
        hp_debug (cvptr->device, TL_DEB_INCO,           /*   then report it */
                  "Unit %d %s call of %u.%u%s %s\n",
                  unit, lib_props [lib_call].action,
                  gap_inches, gap_tenths,
                  (lib_call == lib_rewind ? " inches" : "-inch erase gap"),
                  status_name [cvptr->call_status]);

    else                                                /* otherwise no data was involved */
        hp_debug (cvptr->device, TL_DEB_INCO,           /*   so just report the event */
                  "Unit %d %s call %s\n",
                  unit, lib_props [lib_call].action,
                  status_name [cvptr->call_status]);
    }

if ((cvptr->device->flags & DEV_REALTIME) == 0)         /* if optimized timing is selected */
    cvptr->gaplen = 0;                                  /*   then the gap traversal phase is omitted */

return result;                                          /* return the function set and data */
}


/* Abort the command.

   The current command is aborted due to a fatal error return from the simulator
   tape support library, e.g., if the tape image file format is corrupt.  As
   this routine is invoked when there is no sensible way of recovering, it sets
   the controller to the error state, sets the controller status to reflect an
   uncorrectable data error, schedules the error phase after a nominal delay,
   and returns the associated SCPE error code to the calling service event
   routine to stop the simulation.  If the user resumes, the error phase will be
   entered, and the program executing under simulation (typically, an operating
   system driver) will receive the data error.  The operation may be retried,
   leading to the same failure, but eventually the simulated program will give
   up and handle the fatal error in an appropriate fashion.
*/

static CNTLR_IFN_IBUS abort_command (CVPTR cvptr, UNIT *uptr, t_stat status)
{
cvptr->state = Error_State;                             /* indicate a fatal error */
cvptr->status |= CST_DATAERR;                           /*   and report the cause as a data error */

uptr->PHASE = Error_Phase;                              /* terminate the command */
uptr->wait = cvptr->dlyptr->overhead;                   /*   after a nominal delay */

cvptr->length = 0;                                      /* clear the record length and stop the simulation */
return SCP_STATUS (status);                             /*   with the appropriate console message */
}


/* Reject the command.

   The command attempting to start (if "uptr" is NULL) is rejected, or the
   command currently executing (if "uptr" is not NULL) is aborted.  A busy unit
   is idled, Command Reject status is set, and, if the controller is the HP 3000
   controller, an interrupt request is scheduled after the tape stop delay
   expires.
*/

static void reject_command (CVPTR cvptr, UNIT *uptr)
{
if (uptr)                                               /* if a command is currently executing */
    uptr->PHASE = Idle_Phase;                           /*   then idle the tape drive */
else                                                    /* otherwise a command is attempting to start */
    uptr = CNTLR_UPTR;                                  /*   so set up action on the controller unit */

dpprintf (cvptr->device, TL_DEB_CMD, "%s %s command rejected\n",
          unit_names [uptr - cvptr->device->units], opcode_names [uptr->OPCODE]);

cvptr->status = CST_REJECT;                             /* set the Command Reject status */
cvptr->state = Error_State;

if (cvptr->type == HP_30215) {                          /* if this is the 3000 controller */
    uptr = CNTLR_UPTR;                                  /*   then schedule the delay on the controller unit */

    uptr->PHASE = Error_Phase;                          /* set up the error phase */
    uptr->wait = cvptr->dlyptr->ir_start;               /*   and schedule the tape stop delay */

    activate_unit (cvptr, uptr);                        /* start the controller delay */
    }

return;
}


/* Add the calculated CRC and LRC characters to the tape record buffer.

   The cyclic redundancy check and longitudinal redundancy check characters
   specified by ANSI X3.22 and ECMA-12 are calculated for the data record
   currently in the buffer and then appended to the end of the buffer after
   padding to an even record length if necessary.

   The CRCC is generated by performing half-adds (XORs) of each record byte plus
   odd parity with an initially-zero accumulator, followed by a 9-bit circular
   right-shift.  For each add, if the LSB of the accumulator is 1, bits 2 to 5
   include a forced-carry (an XOR with the binary constant 000 011 100).  After
   all data bytes have been added, all bits except bits 3 and 5 are inverted by
   a half-add with the binary constant 111 010 111.

   The LRCC is generated by performing half-adds (XORs) of each record byte plus
   odd parity with an initially-zero accumulator, followed by a half-add of the
   calculated CRCC byte.

   If the routine is called on behalf of the Read_Record_with_CRCC command, the
   9-bit CRCC is split and appended to the buffer, and the record length is
   increased appropriately.  Otherwise, the CRCC and LRCC, without their MSBs,
   are stored in the buffer, and the record length is not altered.


   Implementation notes:

    1. The 9-bit circular right-shift is implemented by testing the LSB.  If
       it is zero, then a 16-bit logical right-shift is performed; this is
       equivalent, as the rotated-in MSB would be zero.  If the LSB is one, the
       same 16-bit logical right-shift is performed, but the rotated-in MSB is
       restored by ORing in a preshifted one-bit (this latter action is folded
       into the XOR required to complement the bits 5-2 and relies on the MSB
       being zero after shifting).
*/

static void add_crcc_lrcc (CVPTR cvptr, CNTLR_OPCODE opcode)
{
uint32  index;
HP_WORD byte, crc, lrc;

crc = 0;                                                /* initialize the CRC  */
lrc = 0;                                                /*   and LRC accumulators */

for (index = 0; index < cvptr->length; index++) {       /* for each byte in the data record */
    byte = odd_parity [cvptr->buffer [index]]           /*   merge the calculated odd parity bit */
             | cvptr->buffer [index];                   /*     to reconstruct the 9-bit tape data */

    crc = crc ^ byte;                                   /* add the byte to the accumulators */
    lrc = lrc ^ byte;                                   /*   without carries */

    if (crc & 1)                                        /* perform a 9-bit circular right shift */
        crc = crc >> 1 ^ 0474;                          /*   on the CRC accumulator */
    else                                                /*     while inverting bits 2 through 5 */
        crc = crc >> 1;                                 /*       if the resulting LSB is a one */

    dpprintf (cvptr->device, TL_DEB_XFER,
              "CRCC/LRCC index = %2d, buffer = %03o, byte = %06o, crc = %06o, lrc = %06o\n",
              index, cvptr->buffer [index], byte, crc, lrc);
    }

crc = crc ^ 0727;                                       /* invert all bits except bits 3 and 5 */
lrc = lrc ^ crc;                                        /* include the CRCC in the LRCC calculation */

index = cvptr->length;                                  /* get the current record length */

if (index & 1)                                          /* if the record length was odd */
    cvptr->buffer [index++] = 0;                        /*   then pad the buffer with a zero byte */

if (opcode == Read_Record_with_CRCC) {                  /* if the CRCC was requested with the record */
    cvptr->buffer [index++] = LOWER_BYTE (crc);         /*   then store the CRCC in the upper byte */
    cvptr->buffer [index++] = crc >> 1 & D8_SIGN;       /*     and the parity bit in the MSB of the lower byte */

    cvptr->length = index;                              /* count the CRCC and LRCC as part of the record */
    }

else {
    cvptr->buffer [index++] = LOWER_BYTE (crc);         /* store the CRCC and LRCC into the buffer */
    cvptr->buffer [index++] = LOWER_BYTE (lrc);         /*   without altering the valid length */
    }

return;
}



/* Tape library local utility routines */


/* Activate the unit.

   The specified unit is activated using the unit's "wait" time.  If tracing
   is enabled, the activation is logged to the debug file.
*/

static void activate_unit (CVPTR cvptr, UNIT *uptr)
{
dpprintf (cvptr->device, TL_DEB_STATE, "%s %s %s phase delay %d service scheduled\n",
          unit_names [uptr - cvptr->device->units],
          opcode_names [uptr->OPCODE], phase_names [uptr->PHASE],
          uptr->wait);

sim_activate (uptr, uptr->wait);                        /* activate the unit */
uptr->wait = NO_EVENT;                                  /*   and clear the activation time */

return;
}


/* Validate a drive model or density change.

   The tape drive model "new_drive" and density setting "new_bpi" of the unit
   specified by "uptr" are validated against the drives and densities supported
   by the controller type indicated by "cvptr".  If the controller supports the
   model at the density specified, the unit's property index field is set to
   reference the corresponding entry in the drive properties table, the
   simulator tape support library is notified of the density change, and the
   routine returns SCPE_OK to indicate success.  If the controller does not
   support the model, SCPE_ARG is returned to indicate failure.

   If "new_bpi" is zero, then validation does not consider the drive density,
   and the first matching property entry, which should specify the preferred
   default for selectable-density drives, is used.
*/

static t_stat validate_drive (CVPTR cvptr, UNIT *uptr, DRIVE_TYPE new_drive, uint32 new_bpi)
{
const CNTLR_TYPE ctype = cvptr->type;                   /* the controller type */
uint32 entry;

for (entry = 0; entry < PROPS_COUNT; entry++)           /* check each property table entry for a match */
    if (drive_props [entry].controller == ctype         /* if this is our controller */
      && drive_props [entry].drive == new_drive         /*   and the model is supported */
      && (new_bpi == 0                                  /*   and the density is not specified */
      || drive_props [entry].bpi == new_bpi)) {         /*     or the density is supported */
        uptr->PROP = uptr->PROP & ~PROP_INDEX_MASK      /*   then set the unit's property table index */
                       | entry << PROP_INDEX_SHIFT;

        sim_tape_set_dens (uptr,                        /* tell the tape library the density in use */
                           drive_props [entry].density,
                           NULL, NULL);

        if (drive_props [entry].bpi == 1600)            /* if the drive is a 1600-bpi unit */
            uptr->STATUS |= UST_DEN1600;                /*   then set the density bit in the status */
        else                                            /* otherwise */
            uptr->STATUS &= ~UST_DEN1600;               /*   clear it */

        return SCPE_OK;                                 /* allow the change */
        }

return SCPE_ARG;                                        /* if validation fails then reject the change */
}
