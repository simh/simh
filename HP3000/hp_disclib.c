/* hp_disclib.c: HP MAC/ICD disc controller simulator library

   Copyright (c) 2011-2016, J. David Bryan
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

   03-Aug-16    JDB     "fmt_bitset" now allows multiple concurrent calls
   09-Jun-16    JDB     Added casts for ptrdiff_t to int32 values
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     DRIVE_PROPS.name is now a pointer-to-constant
   13-May-16    JDB     Modified for revised SCP API function parameter types
   03-May-16    JDB     Added a trace to identify the unit requesting attention
   24-Mar-16    JDB     Changed the buffer element type from uint16 to DL_BUFFER
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   27-Jul-15    JDB     First revised release version
   21-Feb-15    JDB     Revised for new controller interface model
   24-Dec-14    JDB     Added casts for explicit downward conversions
   27-Oct-14    JDB     Corrected the relative movement calculation in start_seek
   20-Dec-12    JDB     sim_is_active() now returns t_bool
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   07-May-12    JDB     Corrected end-of-track delay time logic
   02-May-12    JDB     First release
   09-Nov-11    JDB     Created disc controller common library from DS simulator

   References:
     - 13037 Disc Controller Technical Information Package
         (13037-90902, August 1980)
     - HP 13365 Integrated Controller Programming Guide
         (13365-90901, February 1980)
     - HP 1000 ICD/MAC Disc Diagnostic Reference Manual
         (5955-4355, June 1984)
     - RTE-IVB System Manager's Manual
         (92068-90006, January 1983)
     - DVR32 RTE Moving Head Driver Source
         (92084-18711, Revision 5000)


   The 13037 multiple-access disc controller (MAC) connects from one to eight HP
   7905 (15 MB), 7906 (20 MB), 7920 (50 MB), or 7925 (120 MB) disc drives to
   interfaces installed in from one to eight HP 1000, 2000, or 3000 CPUs.  The
   drives use a common command set and present data to the controller
   synchronously at a data rate of 468.75 kilowords per second (2.133
   microseconds per word).

   The controller hardware consists of three PCAs: a 16-bit microprogrammed
   processor constructed from 74S181 bit slices operating at 5 MHz, a device
   controller that provides the interconnections to the drives and CPU
   interfaces, and an error correction controller that enables the correction of
   up to 32-bit error bursts.  1024 words of 24-bit firmware are stored in ROM
   on the error correction PCA, and the execution time is 200 nanoseconds per
   instruction.

   The Integrated Controller Drive (ICD) models include the HP 7906H, 7920H, and
   7925H.  These drives are identical to the corresponding MAC drives, except
   that they integrate a single-CPU version of the MAC controller on two PCAs
   housed within the drive: an 8-bit microprocessor constructed from two 4-bit
   slices operating at 3.75 MHz, and an 8-bit DMA that handles the data path
   between the drive and CPU.  Connection to the CPU is via the Hewlett-Packard
   Interface Bus (HP-IB) -- HP's implementation of the IEEE-488 standard.

   The ICD command set essentially is the MAC command set modified for
   single-unit operation.  The unit number and CPU hold bit fields in the opcode
   words are unused in the ICD implementation.  The Load TIO Register, Wakeup,
   and Request Syndrome commands are removed, as Load TIO is used with the HP
   3000, Wakeup is used in a multi-CPU environment, and the simpler ICD
   controller does not support ECC.  Controller status values 02B (Unit
   Available) and 27B (Unit Unavailable) are dropped as the controller supports
   only single units, 12B (I/O Program Error) is reused to indicate HP-IB
   protocol errors, 13B (Sync Not Received) is added, and 17B (Possibly
   Correctable Data Error) is removed as error correction is not supported.

   Some minor redefinitions also occur.  For example, status 14B (End of
   Cylinder) is expanded to include an auto-seek beyond the drive limits, and
   37B (Drive Attention) is restricted to just head unloads (from head loads and
   unloads).

   The MAC controller offers an HP-IB option: the HP 12745A Disc Controller to
   HP-IB Adapter Kit.  This card plugs into the 13037's chassis containing the
   other three controller cards and connects to the CPU interface port of the
   device controller PCA in place of the multi-CPU-interface cable.  It allows
   HP-IB 3000s and the HP 64000 Logic Development Station to connect to MAC disc
   drives; the ICD drives are not supported on these systems.

   This library provides the common functions required by HP disc controllers.
   It implements the 13037 MAC and 13365 ICD controller command sets used with
   the 7905/06/20/25 and 7906H/20H/25H disc drives.

   The library is an adaptation of the code originally written by Bob Supnik
   for the HP2100 DS simulator.  DS simulates a 13037 controller connected via a
   13175 disc interface to an HP 1000 computer.  To create the library, the
   functions of the controller were separated from the functions of the
   interface.  This allows the library to work with other CPU interfaces, such
   as the 12821A HP-IB disc interface, that use substantially different
   communication protocols.  The library functions implement the controller
   command set for the drive units.  The interface functions handle the transfer
   of commands and data to and from the CPU.

   The original release of this library did not handle the data transfer between
   the controller and the interface directly.  Instead, data was moved between
   the interface and a sector buffer by the interface simulator, and then the
   buffer was passed to the disc library for reading or writing.  This buffer
   was also used to pass disc commands and parameters to the controller, and to
   receive status information from the controller.

   While this approach served to allow the library to be shared between
   dissimilar interfaces, each interface had to have intimate knowledge of the
   internal controller state in order to schedule parameter and data transfers.
   In particular, the unit service routine had to base its actions on specific
   controller phase and opcode pairs and manipulate the internal state variables
   of the controller.  As such, the library could not be viewed opaquely.

   In addition, the HP 3000 interface required channel program support that was
   not provided by the simulation library although it was present in hardware.
   Adapting the existing library model would have placed an even larger and more
   intimate burden on the interface simulation.

   As a result, the library API was rewritten to model the hardware more
   closely and provide a more strict separation of controller and interface
   functions.  Instead of providing separate routines to prepare, start, and end
   commands, service units, and poll drives for Attention status, the new model
   provides a single routine that represents the hardware data, flag, and
   function buses between the interface and controller.  The interface calls
   this routine whenever the state of the flag bus changes, and the controller
   responds by potentially changing the data and function buses.  The interface
   merely responds to those changes without requiring any other knowledge of the
   internal state of the controller.


   A device interface simulator interacts with the disc controller simulator via
   the dl_controller routine, which simulates the command, status, and data
   interconnection between the interface and controller.  Utility routines are
   also provided to attach and detach disc image files from drive units, load or
   unload the drive's heads, set drive model and protection status, select the
   interface timing mode (real or fast), and enable overriding of disc command
   status returns for diagnostics.

   In hardware, the interface and controller are interconnected via a 16-bit
   bidirectional data bus (IBUS), a 6-bit flag bus and one signal (CLEAR) to
   the controller, and a 4-bit function bus (IFNBUS) and four signals (ENID,
   ENIR, IFVLD, and IFCLK) to the interface.  The interface initiates controller
   action by changing the state of the flag bus, and the controller responds by
   asserting a function on the function bus.  For example, the interface starts
   a disc command by asserting CMRDY on the flag bus.  The controller responds
   by placing the IFGTC (Interface Get Command) function on the function bus and
   asserting the ENID (Enable Interface Drivers) and IFVLD (Interface Function
   Valid) signals.  The interface then replies by placing the command on the
   data bus, where it is read by the controller.  The controller then decodes
   the command and initiates processing.

   The controller microprogram runs continuously.  However, command execution
   pauses in various wait loops whenever the controller must suspend until an
   external event occurs.  For example, an Address Record command waits first
   for the CPU to send the cylinder address and then waits again for the CPU to
   send the head/sector address.  The controller then saves these values in
   registers before completing the command and then waiting for the CPU to send
   a new command.

   In simulation, the dl_controller routine is called with a set of flags and
   the content of the data bus whenever the flag state changes or a service
   event occurs.  The routine returns a set of functions and the new content of
   the data bus.  To use the above example, dl_controller would be called with
   CMRDY and the command word and would return IFGTC; the data bus return value
   would not be used in this case.  In hardware, the controller might send a
   series of individual functions to the interface in response to a single
   invocation.  In simulation, this series would be collected into a single
   function set for return.

   Hardware wait loops are simulated by the dl_controller routine returning to
   the caller until the expected external event occurs.  In the Address Record
   example, dl_controller would be called first when the command is issued by
   the CPU.  The routine would initiate command processing and then return to
   wait for the cylinder address.  When the CPU provided the address, the
   interface simulator would call dl_controller again with the cylinder value.
   The routine would then return to wait for the head/sector address.  When it
   was available, dl_controller would be called with the value, and the routine
   would complete the command and return to the caller to wait for a new
   command.  So, in simulation, the controller only "runs" when it has work to
   do.

   A controller instance is represented by a CNTLR_VARS structure, which
   maintains the controller's internal state.  A MAC interface will have a
   single controller instance that controls up to eight drive units, whereas an
   ICD interface will have one controller instance per drive unit.  The minor
   differences in controller action between the two are handled internally.

   The interface simulator must declare one unit for each disc drive to be
   controlled by the library.  For a MAC controller, eight units are required,
   plus one additional unit for the controller itself.  For an ICD controller,
   only one unit is required.

   The controller maintains five values in each drive's unit structure:

     u3 (CYL)    -- the current drive cylinder
     u4 (STATUS) -- the drive status (Status-2)
     u5 (OPCODE) -- the drive current operation in process
     u6 (PHASE)  -- the drive current operation phase
     pos         -- the current byte offset into the disc image file

   Drives maintain their cylinder (head positioner) locations separate from the
   cylinder location stored in the controller.  This allows the controller to
   implement sparing by positioning to one location while storing a different
   location in the sector headers.  It also allows seek retries by issuing a
   Recalibrate (which moves the positioner to cylinder 0) followed by the
   original read or write (which repositioned to the cylinder stored in the
   controller).

   The drive status field contains only a subset of the status maintained by
   drives in hardware.  Specifically, the Attention, Read-Only, First Status,
   and Seek Check bits are stored in the status field.  The other bits (Format
   Enabled, Not Ready, and Drive Busy) are set dynamically whenever status is
   requested (the Drive Fault bit is not simulated).

   Per-drive opcode and phase values allow seeks to be overlapped.  For example,
   a Seek issued to unit 0 may be followed by a Read issued to unit 1.  When the
   seek completes on unit 0, its opcode and phase values will let the controller
   set the appropriate seek completion status without disturbing the values
   currently in-use by unit 1.

   The simulation defines these command phases:

     Idle        -- the unit is not currently executing a command
     Parameter   -- the unit is obtaining or returning parameter values
     Seek        -- the unit is seeking to a new head position
     Rotate      -- the unit is rotating into position to access a sector
     Data        -- the unit is obtaining or returning sector data values
     Intersector -- the unit is rotating between sectors
     End         -- the unit is completing a command

   A value represents the current state of the unit.  If a unit is active, the
   phase will end when the unit is serviced.

   In addition to the controller structure(s), an interface declares a data
   buffer to be used for sector transfers.  The buffer is an array containing
   DL_BUFSIZE 16-bit elements; the address of the buffer is stored in the
   controller state structure.  The controller maintains the current index into
   the buffer, as well as the length of valid data stored there.  Only one
   buffer is needed per interface, regardless of the number of controllers or
   units handled, as a single interface cannot perform data transfers on one
   drive concurrently with a command directed to another drive.

   An interface is also responsible for declaring a structure of type
   DELAY_PROPS that contains the timing values for the controller when in
   FASTTIME mode.  The values are event counts for these actions:

     - track-to-track seek time
     - full-stroke seek time
     - full sector rotation time
     - per-word data transfer time
     - intersector gap time
     - controller execution overhead time

   These values are typically exposed via the interface's register set and so
   may be altered by the user.  A macro, DELAY_INIT, is provided to initialize
   the structure.

   An interface may optionally declare an array of DIAG_ENTRY structures if it
   wishes to use the diagnostic override capability.  Diagnostic overrides are
   used to return controller status values that otherwise are not simulated to a
   diagnostic program.  An example would be a Correctable Data Error or a
   Head-Sector Miscompare.  If this facility is to be used, a pointer to the
   array is placed in the CNTLR_VARS structure when it is initialized, and the
   array itself is initialized with the DL_OVEND value that indicates that no
   overrides are currently defined.

   If the pointer is set, then when each command is started, the cylinder, head,
   sector, and opcode values from the current override entry are checked against
   the corresponding current controller values.  If a match occurs, then the
   controller status and Spare/Protected/Defective values are set from the entry
   rather than being cleared, and the pointer is moved to the next entry.  These
   values will then be returned as the completion status of the current command.

   If the command performs address verification, then any SPD value(s) will be
   used as the result of the verification.  In particular, setting the P bit for
   a Write will cause a Protected Track error if the FORMAT switch is not on,
   and any status value other than Normal Completion, Correctable Data Error, or
   Uncorrectable Data Error will cause a verification abort.

   In hardware, the errors that may occur during verification are Cylinder
   Miscompare, Head-Sector Miscompare, Sync Timeout, Illegal Spare Access, and
   Defective Track; any of these errors may be simulated.  In addition, an
   Uncorrectable Data Error may occur in hardware if the controller is unable to
   verify any of the 16 sectors starting at the sector preceding the target
   sector, but this error cannot be simulated.

   Specifying either a Correctable Data Error or an Uncorrectable Data Error
   will cause an abort at the end of the first sector of a read, write, or
   verify command.

   Correctable Data Error and Uncorrectable Data Error may also be specified for
   the Request Syndrome command.  A table entry with the former status value is
   always followed by an additional entry that contains the values to be
   returned for the three syndrome words and the displacement.

   The last defined table entry always contains a special end-of-table value.

   The controller library provides a macro, DL_MODS, that initializes MTAB
   entries, and two utility routines, dl_set_diag and dl_show_diag, that provide
   a user interface for setting up a table of diagnostic overrides.  See the
   comments for these routines below for the command syntax.

   A macro, CNTLR_INIT, is provided to initialize the controller structure from
   the following parameters:

     - the type of the controller (MAC or ICD)
     - the simulation DEVICE structure on which the controller operates
     - the data buffer array
     - the diagnostic override array or NULL if not used
     - the structure containing the FASTTIME values

   A macro, DL_REGS, is also provided that initializes a set of REG structures
   to provide a user interface to the controller structure.

   In hardware, disc drives respond to commands issued by the controller.  The
   only unsolicited status from drives occurs when the heads are loaded or
   unloaded by the operator.  In simulation, SET <dev> UNLOAD and SET <dev> LOAD
   commands represent these actions.  The controller must be notified by calling
   the dl_load_unload routine in response to changes in a disc drive's RUN/STOP
   switch.

   Finally, the controller library provides extensive tracing of its internal
   operations via debug logging.  Six debug flags are declared for use by the
   interface simulator.  When enabled, these report controller actions at
   various levels of detail.


   Implementation notes:

    1. The library does not simulate sector headers and trailers.  Initialize
       and Write Full Sector commands ignore the SPD bits and the supplied
       header and trailer words.  Read Full Sector fills in the header with the
       current CHS address and sets the SPD bits to zero.  The CRC and ECC words
       in the trailer are returned as zeros.  Programs that depend on drives
       retaining the set values will fail.

    2. The library does not simulate drive hold bits or support multiple CPU
       interfaces connected to the same controller.  CPU access to a valid drive
       always succeeds.

    3. The sector buffer is an array of 16-bit elements.  Byte-oriented
       interface simulators, such as the 12821A HP-IB Disc Interface, must do
       their own byte packing and unpacking.

    4. In hardware, a command pending on an interface (CMRDY asserted) while a
       previous command is executing will be started as soon as the current
       command completes.  In simulation, dl_controller will exit when the
       current command completes and must be called again if CMRDY is asserted.
       This is necessary to allow the completion status of the prior command to
       be returned to the interface before the new command is started.
*/



#include <math.h>

#include "hp_disclib.h"



/* Program constants */

#define CNTLR_UNIT          (DL_MAXDRIVE + 1)           /* controller unit number */
#define MAX_UNIT            10                          /* last legal unit number */

#define WORDS_PER_SECTOR    128                         /* data words per sector */

#define UNTALK_DELAY        160                         /* ICD untalk delay (constant instruction count) */
#define CNTLR_TIMEOUT       S (1.74)                    /* command and parameter wait timeout (1.74 seconds) */

#define NO_EVENT            -1                          /* do not schedule an event */

#define NO_ACTION           (CNTLR_IFN_IBUS) (NO_FUNCTIONS | NO_DATA)


/* Controller unit pointer */

#define CNTLR_UPTR          (cvptr->device->units + cvptr->device->numunits - 1)


/* Unit flags accessor */

#define GET_MODEL(f)        (DRIVE_TYPE) ((f) >> UNIT_MODEL_SHIFT & UNIT_MODEL_MASK)


/* Controller clear types */

typedef enum {
    Hard_Clear,                                         /* power-on/preset hard clear */
    Timeout_Clear,                                      /* command or parameter timeout clear */
    Soft_Clear                                          /* programmed soft clear */
    } CNTLR_CLEAR;


/* Command accessors.

   Disc commands are passed across the data bus to the controller with the CMRDY
   flag asserted.  The commands have several forms, depending on the particular
   opcode:

       15| 14  13  12| 11  10  9 | 8   7   6 | 5   4   3 | 2   1   0    HP 1000 numbering
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   - |  command opcode   | -   -   -   -   -   -   -   - |  form 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   - |  command opcode   | -   -   -   - |  unit number  |  form 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   - |  command opcode   | H | -   -   - |  unit number  |  form 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | P | D |  command opcode   | H | -   -   - |  unit number  |  form 4
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   - |  command opcode   |    retries    | D | S | C | A |  form 5
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   - |  command opcode   | head  |        sector         |  form 6
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15    HP 3000 numbering

   Form 1 is used by the Address Record, Clear, End, Load TIO Register, Request
   Disc Address, and Request Syndrome commands.

   Form 2 is used by the Request Disc Sector and Request Status commands.

   Form 3 is used by the Read, Read Full Sector, Read With Offset, Read Without
   Verify, Recalibrate, Seek, Verify, Wakeup, Write, and Write Full Sector
   commands, where:

     H = hold the drive

   Form 4 is used by the Initialize Command, where:

     S = initialize the track to spare status
     P = initialize the track to protected status
     D = initialize the track to defective status
     H = hold the drive

   Form 5 is used by the Set File Mask command, where:

     D = decremental seek
     S = sparing enabled
     C = cylinder mode
     A = auto-seek enabled

   Form 6 is used by the Cold Load Read command.
*/

#define CM_OPCODE_MASK      0017400u            /* operation code mask */
#define CM_UNIT_MASK        0000017u            /* unit number mask */

#define CM_SPARE            0100000u            /* spare track */
#define CM_PROTECTED        0040000u            /* protected track */
#define CM_DEFECTIVE        0020000u            /* defective track */
#define CM_SPD_MASK         (CM_SPARE | CM_PROTECTED | CM_DEFECTIVE)

#define CM_RETRY_MASK       0000360u            /* retry count mask */
#define CM_FILE_MASK_MASK   0000017u            /* file mask mask */

#define CM_DECR_SEEK        0000010u            /* 0/1 = incremental/decremental seek */
#define CM_SPARE_EN         0000004u            /* sparing enabled */
#define CM_CYL_MODE         0000002u            /* 0/1 = surface/cylinder mode */
#define CM_AUTO_SEEK_EN     0000001u            /* auto-seek enabled */

#define CM_HEAD_MASK        0000300u            /* cold load read head mask */
#define CM_SECTOR_MASK      0000077u            /* cold load read sector mask */


#define CM_OPCODE_SHIFT     8
#define CM_UNIT_SHIFT       0

#define CM_RETRY_SHIFT      4
#define CM_FILE_MASK_SHIFT  0

#define CM_HEAD_SHIFT       6
#define CM_SECTOR_SHIFT     0


#define CM_SPD(c)           ((c) & CM_SPD_MASK)

#define CM_OPCODE(c)        (CNTLR_OPCODE) (((c) & CM_OPCODE_MASK) >> CM_OPCODE_SHIFT)

#define CM_UNIT(c)          (((c) & CM_UNIT_MASK)      >> CM_UNIT_SHIFT)

#define CM_RETRY(c)         (((c) & CM_RETRY_MASK)     >> CM_RETRY_SHIFT)
#define CM_FILE_MASK(c)     (((c) & CM_FILE_MASK_MASK) >> CM_FILE_MASK_SHIFT)

#define CM_HEAD(c)          (((c) & CM_HEAD_MASK)      >> CM_HEAD_SHIFT)
#define CM_SECTOR(c)        (((c) & CM_SECTOR_MASK)    >> CM_SECTOR_SHIFT)


static const BITSET_NAME file_mask_names [] = {     /* File mask word */
    "\1decremental seek\0incremental seek",         /* bit  3/12 */
    "sparing",                                      /* bit  2/13 */
    "\1cylinder mode\0surface mode",                /* bit  1/14 */
    "autoseek"                                      /* bit  0/15 */
    };

static const BITSET_FORMAT file_mask_format =       /* names, offset, direction, alternates, bar */
    { FMT_INIT (file_mask_names, 0, msb_first, has_alt, append_bar) };


/* Parameter accessors.

   Parameters are passed across the data bus to the controller with the DTRDY
   flag asserted.  The parameters have several forms, depending on the commands
   requesting them:

       15| 14  13  12| 11  10  9 | 8   7   6 | 5   4   3 | 2   1   0    HP 1000 numbering
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                           cylinder                            |  form 1  (in/out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     0     |       head        |            sector             |  form 2  (in/out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | P | D |    status code    | -   -   -   - |  unit number  |  form 3  (out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | E | -   - |   drive type  | - | A | R | F | L | S | K | N | B |  form 4  (out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |               0               |            sector             |  form 5  (out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         sector count                          |  form 6  (in)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         displacement                          |  form 7  (out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                            pattern                            |  form 8  (out)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                           data word                           |  form 9  (in)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       | A | D | S | - | cyl offset magnitude  |  form 10 (in)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15    HP 3000 numbering

   Forms 1 and 2 are used by the Address Record, Request Disc Address, Request
   Syndrome, and Seek commands.

   Form 3 is used by the Request Status and Request Syndrome commands, where:

     S = last track was a spare
     P = last track was protected
     D = last track was defective

   Form 4 is used by the Request Status command, where:

     E = error present
     A = attention
     R = read-only
     F = format enabled
     L = drive fault
     S = first status
     K = seek check
     N = not ready
     B = drive busy

   Form 5 is used by the Request Sector Address command.

   Form 6 is used by the Verify command.

   Forms 7 and 8 are used by the Request Syndrome command.

   Form 9 is used by the Load TIO Register command.

   Form 10 is used by the Read With Offset command, where:

     A = advance the clock (valid for 13037A only)
     D = delay the clock (valid for 13037A only)
     S = sign of cylinder offset
*/

#define S1_SPARE            0100000u            /* spare track */
#define S1_PROTECTED        0040000u            /* protected track */
#define S1_DEFECTIVE        0020000u            /* defective track */
#define S1_STATUS_MASK      0017400u            /* encoded termination status mask */
#define S1_UNIT_MASK        0000017u            /* unit number mask */

#define S1_STATUS_SHIFT     8
#define S1_UNIT_SHIFT       0

#define S1_STATUS(n)        ((n) << S1_STATUS_SHIFT & S1_STATUS_MASK)
#define S1_UNIT(n)          ((n) << S1_UNIT_SHIFT   & S1_UNIT_MASK)


#define S2_ERROR            0100000u            /* any error */
#define S2_DRIVE_TYPE_MASK  0017000u            /* drive type mask */
#define S2_ATTENTION        0000200u            /* attention */
#define S2_READ_ONLY        0000100u            /* read-only */
#define S2_FORMAT_EN        0000040u            /* format enabled */
#define S2_FAULT            0000020u            /* drive fault */
#define S2_FIRST_STATUS     0000010u            /* first status */
#define S2_SEEK_CHECK       0000004u            /* seek check */
#define S2_NOT_READY        0000002u            /* not ready */
#define S2_BUSY             0000001u            /* drive busy */

#define S2_STOPS            (S2_FAULT \
                               | S2_SEEK_CHECK \
                               | S2_NOT_READY)  /* bits that stop drive access */

#define S2_ERRORS           (S2_FAULT \
                               | S2_SEEK_CHECK \
                               | S2_NOT_READY \
                               | S2_BUSY)       /* bits that set S2_ERROR */

#define S2_CPS              (S2_ATTENTION \
                               | S2_FAULT \
                               | S2_FIRST_STATUS \
                               | S2_SEEK_CHECK) /* bits that are cleared by Controller Preset */

#define S2_DRIVE_TYPE_SHIFT 9

#define S2_DRIVE_TYPE(n)    ((n) << S2_DRIVE_TYPE_SHIFT & S2_DRIVE_TYPE_MASK)
#define S2_TO_DRIVE_TYPE(n) (((n) & S2_DRIVE_TYPE_MASK) >> S2_DRIVE_TYPE_SHIFT)


#define PIO_HEAD_MASK       0017400u            /* head mask */
#define PIO_SECTOR_MASK     0000377u            /* sector mask */

#define PI_ADV_CLOCK        0001000u            /* advance clock */
#define PI_DEL_CLOCK        0000400u            /* delay clock */
#define PI_NEG_OFFSET       0000200u            /* 0/1 = positive/negative cylinder offset sign */
#define PI_OFFSET_MASK      0000077u            /* cylinder offset mask */


#define PIO_HEAD_SHIFT      8
#define PIO_SECTOR_SHIFT    0

#define PI_OFFSET_SHIFT     0


#define PI_HEAD(p)          (((p) & PIO_HEAD_MASK)   >> PIO_HEAD_SHIFT)
#define PI_SECTOR(p)        (((p) & PIO_SECTOR_MASK) >> PIO_SECTOR_SHIFT)
#define PI_OFFSET(p)        (((p) & PI_OFFSET_MASK)  >> PI_OFFSET_SHIFT)

#define PO_HEAD(n)          (((n) << PIO_HEAD_SHIFT   & PIO_HEAD_MASK))
#define PO_SECTOR(n)        (((n) << PIO_SECTOR_SHIFT & PIO_SECTOR_MASK))


static const BITSET_NAME status_1_names [] = {  /* Status-1 word */
    "spare",                                    /* bit 15/0 */
    "protected",                                /* bit 14/1 */
    "defective"                                 /* bit 13/2 */
    };

static const BITSET_FORMAT status_1_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_1_names, 13, msb_first, no_alt, append_bar) };

static const BITSET_FORMAT initialize_format =  /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_1_names, 13, msb_first, no_alt, no_bar) };


static const BITSET_NAME status_2_names [] = {  /* Status-2 word */
    "attention",                                /* bit  7/ 8 */
    "read only",                                /* bit  6/ 9 */
    "format enabled",                           /* bit  5/10 */
    "fault",                                    /* bit  4/11 */
    "first status",                             /* bit  3/12 */
    "seek check",                               /* bit  2/13 */
    "not ready",                                /* bit  1/14 */
    "busy"                                      /* bit  0/15 */
    };

static const BITSET_FORMAT status_2_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_2_names, 0, msb_first, no_alt, no_bar) };


static const BITSET_NAME offset_names [] = {    /* Read With Offset parameter */
    "advanced clock",                           /* bit  9/ 6 */
    "delayed clock"                             /* bit  8/ 7 */
    };

static const BITSET_FORMAT offset_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (offset_names, 8, msb_first, no_alt, append_bar) };


/* Drive properties table.

   In hardware, drives report their drive type numbers to the controller upon
   receipt of a Request Status tag bus command.  The drive type is used to
   determine the legal range of head and sector addresses (the drive itself will
   validate the cylinder address during a Seek command and the head/sector
   address during an Address Record drive command).

   In simulation, the model ID number from the unit flags is used as an index
   into the drive properties table.  The table is used to validate seek
   parameters and to provide the mapping between CHS addresses and the linear
   byte addresses required by the host file access routines.

   The 7905/06(H) drives consist of removable and fixed platters, whereas the
   7920(H)/25(H) drives have only removable multi-platter packs.  As a result,
   7905/06 drives are almost always accessed in platter mode, i.e., a given
   logical disc area is fully contained on either the removable or fixed
   platter, whereas the 7920/25 drives are almost always accessed in cylinder
   mode with logical disc areas spanning some or all of the platters.

   Disc image files are arranged as a linear set of tracks.  To improve
   locality of access, tracks in the 7905/06 images are grouped per-platter,
   whereas tracks on the 7920 and 7925 are sequential by cylinder and head
   number.

   The simulator maps the tracks on the 7905/06 removable platter (heads 0 and
   1) to the first half of the disc image, and the tracks on the fixed platter
   (heads 2 and, for the 7906 only, 3) to the second half of the image.  For the
   7906(H), the cylinder-head order of the tracks is 0-0, 0-1, 1-0, 1-1, ...,
   410-0, 410-1, 0-2, 0-3, 1-2, 1-3, ..., 410-2, 410-3.  The 7905 order is the
   same, except that head 3 tracks are omitted.

   For the 7920(H)/25(H), all tracks appear in cylinder-head order, e.g., 0-0,
   0-1, 0-2, 0-3, 0-4, 1-0, 1-1, ..., 822-2, 822-3, 822-4 for the 7920(H).

   This variable-access geometry is accomplished by defining separate "heads per
   cylinder" values for the fixed and removable sections of each drive that
   indicates the number of heads that should be grouped for locality.  The
   removable values are set to 2 on the 7905 and 7906, indicating that those
   drives typically use cylinders consisting of two heads.  They are set to the
   number of heads per drive for the 7920 and 7925, as those typically use
   cylinders encompassing the entire pack.

   The Drive Type is reported by the controller in the second status word
   (Status-2) returned by the Request Status command.
*/

typedef struct {
    const char  *name;                          /* drive name */
    uint32      sectors;                        /* sectors per head */
    uint32      heads;                          /* heads per cylinder*/
    uint32      cylinders;                      /* cylinders per drive */
    uint32      words;                          /* words per drive */
    uint32      remov_heads;                    /* number of removable-platter heads */
    uint32      fixed_heads;                    /* number of fixed-platter heads */
    } DRIVE_PROPS;

static const DRIVE_PROPS drive_props [] = {     /* indexed by DRIVE_TYPE */
   /*  drive   sectors    heads   cylinders      words     remov  fixed */
   /*  name    per trk   per cyl  per drive    per drive   heads  heads */
   /* -------  -------   -------  ----------  -----------  -----  ----- */
    { "7906",    48,       4,        411,     WORDS_7906,    2,     2   },     /* drive type 0 */
    { "7920",    48,       5,        823,     WORDS_7920,    5,     0   },     /* drive type 1 */
    { "7905",    48,       3,        411,     WORDS_7905,    2,     1   },     /* drive type 2 */
    { "7925",    64,       9,        823,     WORDS_7925,    9,     0   }      /* drive type 3 */
    };


/* Delay properties table.

   To support the realistic timing mode, the delay properties table contains
   timing specifications for the supported disc drives.  The times represent the
   delays for mechanical and electronic operations.  Delay values are in event
   tick counts; macros are used to convert from times to ticks.

   The drive type field differentiates between drive models available on a given
   controller.  The field is not significant for MAC and ICD controllers because
   all of the drives supported have the same specifications.

   The controller overhead values are estimates; they do not appear to be
   documented for MAC and ICD controllers, although they are published for
   various CS/80 drives.
*/

static const DELAY_PROPS real_times [] = {
   /* cntlr   drive     seek     seek       sector        data     intersector   cntlr   */
   /* type     type    trk-trk   full      rotation     per word       gap      overhead */
   /* -----  --------  -------  --------  -----------  ----------  -----------  -------- */
    { MAC,   HP_All,   mS (5),  mS (45),  uS (347.2),  uS (2.13),  uS (27.2),   uS (200) },
    { ICD,   HP_All,   mS (5),  mS (45),  uS (347.2),  uS (2.13),  uS (27.2),   mS (1.5) }
    };

#define DELAY_COUNT         (sizeof real_times / sizeof real_times [0])


/* Estimate the current sector.

   The sector currently passing under the disc heads is estimated from the
   current "simulation time," which is the number of event ticks since the
   simulation run was started, and the simulated disc rotation time.  The
   computation logic is:

     current_sector := (simulator_time / per_sector_time) MOD sectors_per_track;
*/

#define CURRENT_SECTOR(cvptr,uptr) \
            (uint32) fmod (sim_gtime () / cvptr->dlyptr->sector_full, \
                           drive_props [GET_MODEL (uptr->flags)].sectors)


/* Command properties table.

   The validity of each command for a given controller type is checked against
   the command properties table when it is prepared for execution.  The table
   also includes the count of inbound or outbound parameters, the class of the
   command, and flags that indicate certain common actions that are be taken.


   Implementation notes:

    1. The verify field of the Read_Without_Verify property record is set to
       TRUE so that address verification will be done if a track boundary is
       crossed.  Initial verification is suppressed by setting the controller's
       verify field to FALSE during initial Read_Without_Verify processing.
*/

typedef struct {
    uint32       param_count;                   /* count of input or output parameters */
    CNTLR_CLASS  classification;                /* command classification */
    t_bool       valid [CNTLR_COUNT];           /* command validity, indexed by CNTLR_TYPE */
    t_bool       clear_status;                  /* command clears the controller status */
    t_bool       unit_field;                    /* command has a unit field */
    t_bool       unit_check;                    /* command checks the unit number validity */
    t_bool       unit_access;                   /* command accesses the drive unit */
    t_bool       seek_wait;                     /* command waits for seek completion */
    t_bool       verify_address;                /* command does address verification */
    t_bool       idle_at_end;                   /* command idles the controller at completion */
    uint32       preamble_size;                 /* size of preamble in words */
    uint32       transfer_size;                 /* size of data transfer in words */
    uint32       postamble_size;                /* size of postamble in words */
    } COMMAND_PROPERTIES;

typedef const COMMAND_PROPERTIES *PRPTR;

#define T   TRUE
#define F   FALSE

static const COMMAND_PROPERTIES cmd_props [] = {
/*   parm      opcode        valid for    clr  unit unit unit seek addr end  pre  xfer post   */
/*   I/O   classification   MAC ICD CS80  stat fld  chk  acc  wait verf idle size size size   */
/*   ----  --------------   --- --- ----  ---- ---- ---- ---- ---- ---- ---- ---- ---- ----   */
    {  0,  Class_Read,     { T,  T,  F },  T,   F,   T,   T,   F,   T,   F,   15, 128,  7  }, /* 00 = Cold_Load_Read */
    {  0,  Class_Control,  { T,  T,  F },  T,   T,   T,   T,   T,   F,   T,    0,  0,   0  }, /* 01 = Recalibrate */
    {  2,  Class_Control,  { T,  T,  F },  T,   T,   T,   T,   F,   F,   T,    0,  0,   0  }, /* 02 = Seek */
    {  2,  Class_Status,   { T,  T,  F },  F,   T,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 03 = Request_Status */
    {  1,  Class_Status,   { T,  T,  F },  T,   T,   T,   T,   F,   F,   F,    0,  0,   0  }, /* 04 = Request_Sector_Address */
    {  0,  Class_Read,     { T,  T,  F },  T,   T,   T,   T,   T,   T,   F,   15, 128,  7  }, /* 05 = Read */
    {  0,  Class_Read,     { T,  T,  F },  T,   T,   T,   T,   T,   F,   F,   12, 138,  0  }, /* 06 = Read_Full_Sector */
    {  1,  Class_Read,     { T,  T,  F },  T,   T,   T,   T,   T,   T,   F,    0,  0,   0  }, /* 07 = Verify */
    {  0,  Class_Write,    { T,  T,  F },  T,   T,   T,   T,   T,   T,   F,   15, 128,  7  }, /* 10 = Write */
    {  0,  Class_Write,    { T,  T,  F },  T,   T,   T,   T,   T,   F,   F,   12, 138,  0  }, /* 11 = Write_Full_Sector */
    {  0,  Class_Control,  { T,  T,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 12 = Clear */
    {  0,  Class_Write,    { T,  T,  F },  T,   T,   T,   T,   T,   F,   F,   15, 128,  7  }, /* 13 = Initialize */
    {  2,  Class_Control,  { T,  T,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 14 = Address_Record */
    {  7,  Class_Status,   { T,  F,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 15 = Request_Syndrome */
    {  1,  Class_Read,     { T,  T,  F },  T,   T,   T,   T,   T,   T,   F,   15, 128,  7  }, /* 16 = Read_With_Offset */
    {  0,  Class_Control,  { T,  T,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 17 = Set_File_Mask */
    {  0,  Class_Invalid,  { F,  F,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 20 = Invalid_Opcode */
    {  0,  Class_Invalid,  { F,  F,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 21 = Invalid_Opcode */
    {  0,  Class_Read,     { T,  T,  F },  T,   T,   T,   T,   T,   T,   F,   15, 128,  7  }, /* 22 = Read_Without_Verify */
    {  1,  Class_Status,   { T,  F,  F },  T,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 23 = Load_TIO_Register */
    {  2,  Class_Status,   { T,  T,  F },  F,   F,   F,   F,   F,   F,   F,    0,  0,   0  }, /* 24 = Request_Disc_Address */
    {  0,  Class_Control,  { T,  T,  F },  T,   F,   F,   F,   F,   F,   T,    0,  0,   0  }, /* 25 = End */
    {  0,  Class_Control,  { T,  F,  F },  T,   T,   T,   F,   F,   F,   F,    0,  0,   0  }  /* 26 = Wakeup */
    };


/* Command functions table.

   At each phase of command execution, the controller may return zero or more
   functions for the interface to perform.  The functions control the transfer
   of parameters and data to and from the CPU.  Note that commands do not
   necessarily use all available phases.


   Implementation notes:

    1. Commands usually return BUSY and IFGTC functions to begin.  However, the
       Clear and Set File Mask commands delay the IFGTC function, which requests
       channel service, to the End Phase to ensure that the WRTIO function
       completes before the channel program continues.  This ensures that an End
       I/O order will pick up the correct status value from the interface.

    2. Invalid commands have WRTIO in their Idle_Phase entries because the
       diagnostic expects to see Illegal_Opcode status immediately after the SIO
       program ends.
*/

typedef CNTLR_IFN IFN_ARRAY [7];

static const IFN_ARRAY cmd_functions [] = {     /* indexed by CNTLR_OPCODE */
                                                /* 00 = Cold_Load_Read */
    { BUSY | SRTRY | IFGTC,                     /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFIN,                                     /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 01 = Recalibrate */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | FREE },                           /*   End Phase */

                                                /* 02 = Seek */
    { BUSY  | IFGTC | STDFL,                    /*   Idle Phase */
      IFOUT | STDFL,                            /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      IFOUT | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 03 = Request_Status */
    { BUSY | IFGTC,                             /*   Idle Phase */
      IFIN | STDFL,                             /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | FREE | RQSRV },                   /*   End Phase */

                                                /* 04 = Request_Sector_Address */
    { BUSY | IFGTC,                             /*   Idle Phase */
      IFIN | STDFL,                             /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | RQSRV | FREE },                   /*   End Phase */

                                                /* 05 = Read */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFIN,                                     /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 06 = Read_Full_Sector */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFIN,                                     /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 07 = Verify */
    { BUSY | IFGTC | STDFL,                     /*   Idle Phase */
      IFOUT,                                    /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 10 = Write */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFOUT,                                    /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 11 = Write_Full_Sector */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFOUT,                                    /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 12 = Clear */
    { BUSY,                                     /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      IFGTC | WRTIO | STDFL | FREE },           /*   End Phase */

                                                /* 13 = Initialize */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFOUT,                                    /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 14 = Address_Record */
    { BUSY  | IFGTC | STDFL,                    /*   Idle Phase */
      IFOUT | STDFL,                            /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      IFOUT | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 15 = Request_Syndrome */
    { BUSY | IFGTC,                             /*   Idle Phase */
      IFIN | STDFL,                             /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | RQSRV | FREE },                   /*   End Phase */

                                                /* 16 = Read_With_Offset */
    { BUSY  | IFGTC | STDFL,                    /*   Idle Phase */
      IFOUT,                                    /*   Parameter Phase */
      RQSRV | STDFL,                            /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFIN,                                     /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 17 = Set_File_Mask */
    { BUSY | SRTRY,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      IFGTC | WRTIO | STDFL | FREE },           /*   End Phase */

                                                /* 20 = Invalid_Opcode */
    { BUSY | IFGTC | WRTIO,                     /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      FREE },                                   /*   End Phase */

                                                /* 21 = Invalid_Opcode */
    { BUSY | IFGTC | WRTIO,                     /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      FREE },                                   /*   End Phase */

                                                /* 22 = Read_Without_Verify */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      IFIN,                                     /*   Data Phase */
      0,                                        /*   Intersector Phase */
      STDFL | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 23 = Load_TIO_Register */
    { BUSY  | IFGTC | STDFL,                    /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      IFOUT | WRTIO | RQSRV | FREE },           /*   End Phase */

                                                /* 24 = Request_Disc_Address */
    { BUSY | IFGTC,                             /*   Idle Phase */
      IFIN | STDFL,                             /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | RQSRV | FREE },                   /*   End Phase */

                                                /* 25 = End */
    { IFGTC,                                    /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      0 },                                      /*   End Phase */

                                                /* 26 = Wakeup */
    { BUSY | IFGTC,                             /*   Idle Phase */
      0,                                        /*   Parameter Phase */
      0,                                        /*   Seek Phase */
      0,                                        /*   Rotate Phase */
      0,                                        /*   Data Phase */
      0,                                        /*   Intersector Phase */
      WRTIO | STDFL | FREE }                    /*   End Phase */
    };


/* Status functions table.

   The End Phase functions above are proper for commands that complete normally.
   Commands that return an error status return additional functions that depend
   on the particular status code.  An entry in this table is ORed with the End
   Phase function to produce the final function set that is returned to the
   interface.
*/

static const CNTLR_IFN status_functions [] = {  /* indexed by CNTLR_STATUS */
    0,                                          /*   000 = Normal Completion */
    STINT | WRTIO | FREE,                       /*   001 = Illegal Opcode */
    STDFL | WRTIO | FREE,                       /*   002 = Unit Available */
    STINT | WRTIO | FREE,                       /*   003 = Illegal Drive Type */
    0,                                          /*   004 = (undefined) */
    0,                                          /*   005 = (undefined) */
    0,                                          /*   006 = (undefined) */
    STINT | WRTIO | FREE,                       /*   007 = Cylinder Miscompare */
    DVEND | RQSRV | WRTIO | FREE,               /*   010 = Uncorrectable Data Error */
    STINT | WRTIO | FREE,                       /*   011 = Head-Sector Miscompare */
    STINT | WRTIO | FREE,                       /*   012 = I/O Program Error */
    DVEND | RQSRV | WRTIO | FREE,               /*   013 = Sync Timeout */
    STINT | WRTIO | FREE,                       /*   014 = End of Cylinder */
    0,                                          /*   015 = (undefined) */
    DVEND | RQSRV | WRTIO | FREE,               /*   016 = Data Overrun */
    DVEND | RQSRV | WRTIO | FREE,               /*   017 = Correctable Data Error */
    STINT | WRTIO | FREE,                       /*   020 = Illegal Spare Access */
    STINT | WRTIO | FREE,                       /*   021 = Defective Track */
    STINT | WRTIO | FREE,                       /*   022 = Access Not Ready */
    STINT | WRTIO | FREE,                       /*   023 = Status-2 Error */
    0,                                          /*   024 = (undefined) */
    0,                                          /*   025 = (undefined) */
    STINT | WRTIO | FREE,                       /*   026 = Protected Track */
    STINT | WRTIO | FREE,                       /*   027 = Unit Unavailable */
    0,                                          /*   030 = (undefined) */
    0,                                          /*   031 = (undefined) */
    0,                                          /*   032 = (undefined) */
    0,                                          /*   033 = (undefined) */
    0,                                          /*   034 = (undefined) */
    0,                                          /*   035 = (undefined) */
    0,                                          /*   036 = (undefined) */
    STINT | WRTIO | FREE                        /*   037 = Drive Attention */
    };


/* Controller operation names */

static const BITSET_NAME flag_names [] = {      /* controller flag names, in CNTLR_FLAG order */
    "CLEAR",                                    /* 000001 */
    "CMRDY",                                    /* 000002 */
    "DTRDY",                                    /* 000004 */
    "EOD",                                      /* 000010 */
    "INTOK",                                    /* 000020 */
    "OVRUN",                                    /* 000040 */
    "XFRNG"                                     /* 000100 */
    };

static const BITSET_FORMAT flag_format =        /* names, offset, direction, alternates, bar */
    { FMT_INIT (flag_names, 0, lsb_first, no_alt, no_bar) };


static const BITSET_NAME function_names [] = {  /* interface function names, in CNTLR_IFN order */
    "BUSY",                                     /* 000000200000 */
    "DSCIF",                                    /* 000000400000 */
    "SELIF",                                    /* 000001000000 */
    "IFIN",                                     /* 000002000000 */
    "IFOUT",                                    /* 000004000000 */
    "IFGTC",                                    /* 000010000000 */
    "IFPRF",                                    /* 000020000000 */
    "RQSRV",                                    /* 000040000000 */
    "DVEND",                                    /* 000100000000 */
    "SRTRY",                                    /* 000200000000 */
    "STDFL",                                    /* 000400000000 */
    "STINT",                                    /* 001000000000 */
    "WRTIO",                                    /* 002000000000 */
    "FREE"                                      /* 004000000000 */
    };

static const BITSET_FORMAT function_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (function_names, 16, lsb_first, no_alt, no_bar) };


static const char invalid_name [] = "Invalid";

static const char *opcode_name [] = {           /* command opcode names, in CNTLR_OPCODE order */
    "Cold Load Read",                           /*   00 */
    "Recalibrate",                              /*   01 */
    "Seek",                                     /*   02 */
    "Request Status",                           /*   03 */
    "Request Sector Address",                   /*   04 */
    "Read",                                     /*   05 */
    "Read Full Sector",                         /*   06 */
    "Verify",                                   /*   07 */
    "Write",                                    /*   10 */
    "Write Full Sector",                        /*   11 */
    "Clear",                                    /*   12 */
    "Initialize",                               /*   13 */
    "Address Record",                           /*   14 */
    "Request Syndrome",                         /*   15 */
    "Read With Offset",                         /*   16 */
    "Set File Mask",                            /*   17 */
    invalid_name,                               /*   20 = invalid */
    invalid_name,                               /*   21 = invalid */
    "Read Without Verify",                      /*   22 */
    "Load TIO Register",                        /*   23 */
    "Request Disc Address",                     /*   24 */
    "End",                                      /*   25 */
    "Wakeup"                                    /*   26 */
    };

#define OPCODE_LENGTH       22                  /* length of the longest opcode name */


static const char *const status_name [] = {     /* command status names, in CNTLR_STATUS order */
    "Normal Completion",                        /*   000 */
    "Illegal Opcode",                           /*   001 */
    "Unit Available",                           /*   002 */
    "Illegal Drive Type",                       /*   003 */
    NULL,                                       /*   004 */
    NULL,                                       /*   005 */
    NULL,                                       /*   006 */
    "Cylinder Miscompare",                      /*   007 */
    "Uncorrectable Data Error",                 /*   010 */
    "Head-Sector Miscompare",                   /*   011 */
    "I/O Program Error",                        /*   012 */
    "Sync Timeout",                             /*   013 */
    "End of Cylinder",                          /*   014 */
    NULL,                                       /*   015 */
    "Data Overrun",                             /*   016 */
    "Correctable Data Error",                   /*   017 */
    "Illegal Spare Access",                     /*   020 */
    "Defective Track",                          /*   021 */
    "Access Not Ready",                         /*   022 */
    "Status-2 Error",                           /*   023 */
    NULL,                                       /*   024 */
    NULL,                                       /*   025 */
    "Protected Track",                          /*   026 */
    "Unit Unavailable",                         /*   027 */
    NULL,                                       /*   030 */
    NULL,                                       /*   031 */
    NULL,                                       /*   032 */
    NULL,                                       /*   033 */
    NULL,                                       /*   034 */
    NULL,                                       /*   035 */
    NULL,                                       /*   036 */
    "Drive Attention"                           /*   037 */
    };

#define STATUS_LENGTH       24                  /* length of the longest status name */


static const char *state_name [] = {            /* controller state names, in CNTLR_STATE order */
    "idle",
    "wait",
    "busy"
    };


static const char *phase_name [] = {            /* unit state names, in CNTLR_PHASE order */
    "idle",
    "parameter",
    "seek",
    "rotate",
    "data",
    "intersector",
    "end"
    };


/* Disc library local controller routines */

static CNTLR_IFN_IBUS start_command    (CVPTR cvptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data);
static CNTLR_IFN_IBUS continue_command (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data);
static CNTLR_IFN_IBUS poll_drives      (CVPTR cvptr);

static void   end_command      (CVPTR cvptr, UNIT *uptr, CNTLR_STATUS status);
static t_bool start_seek       (CVPTR cvptr, UNIT *uptr);
static t_bool start_read       (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags);
static void   end_read         (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags);
static t_bool start_write      (CVPTR cvptr, UNIT *uptr);
static void   end_write        (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags);
static t_bool position_sector  (CVPTR cvptr, UNIT *uptr);
static void   next_sector      (CVPTR cvptr, UNIT *uptr);
static void   io_error         (CVPTR cvptr, UNIT *uptr);
static void   set_completion   (CVPTR cvptr, UNIT *uptr, CNTLR_STATUS status);
static void   clear_controller (CVPTR cvptr, CNTLR_CLEAR clear_type);
static void   idle_controller  (CVPTR cvptr);

/* Disc library local utility routines */

static void    set_address   (CVPTR cvptr, uint32    index);
static void    wait_timer    (CVPTR cvptr, FLIP_FLOP action);
static HP_WORD drive_status  (UNIT  *uptr);
static t_stat  activate_unit (CVPTR cvptr, UNIT *uptr);
static void    set_rotation  (CVPTR cvptr, UNIT *uptr);
static void    set_file_pos  (CVPTR cvptr, UNIT *uptr, uint32 model);



/* Disc library global controller routines */


/* Disc controller interface.

   This routine simulates the hardware interconnection between the disc
   controller and the CPU interface.  This routine is called whenever the flag
   state changes.  This would be when a new command is to be started, when
   command parameters are supplied or status words are retrieved, and when
   sector data is read or written.  It must also be called when the unit service
   routine is entered.  The caller passes in the set of interface flags and the
   contents of the data buffer.  The routine returns a set of functions and, if
   IFIN is included in set, the new content of the data buffer.

   In hardware, the controller microcode executes in one of three states:

    1. In the Poll Loop, which looks for commands and drive attention requests.

       In each pass of the loop, the next CPU interface in turn is selected and
       checked for a command; if present, it is executed.  If not, the
       interface is disconnected, and then all drives are checked in turn until
       one is found with Attention status; if none are found, the loop
       continues.  If a drive is requesting attention, the associated CPU
       interface is selected to check for a command; if present, it is executed.
       If not, and the interface allows interrupts, an interrupt request is made
       and the Command Wait Loop is entered.  If interrupts are not allowed, the
       interface is disconnected, and the Poll Loop continues.

    2. In the Command Wait Loop, which looks for commands.

       In each pass of the loop, the currently selected CPU interface is checked
       for a command; if present, it is executed.  If not, the Command Wait Loop
       continues.  While in the loop, a 1.74 second timer is running.  If it
       expires before a command is received, the file mask is reset, the current
       interface is disconnected, and the Poll Loop is entered.

    3. In command execution, which processes the current command.

       While a command is executing, any waits for input parameters, seek
       completion, data transfers, or output status words are handled
       internally.  Each wait is governed by the 1.74 second timer; if it
       expires, the command is aborted, the file mask is reset, the current
       interface is disconnected, and the Poll Loop is reentered.

   In simulation, these states are represented by the CNTLR_STATE values
   Idle_State, Wait_State, and Busy_State, respectively.

   A MAC controller operates from one to eight drives, represented by an array
   of one to eight UNITs.  The unit number present in the command is used to
   index to the target unit via the "units" pointer in the DEVICE structure.
   One additional unit that represents the controller is required, separate from
   the individual drive units.  Commands that do not access the drive, such as
   Address Record, are scheduled on the controller unit to allow controller
   commands to execute while drive units are seeking.  The command wait timer is
   also scheduled on the controller unit to limit the amount of time the
   controller will wait for the interface to supply a command or parameter.

   An ICD simulation manages a single unit corresponding to the drive in which
   the controller is integrated.  A device interface declares a UNIT array
   corresponding to the number of drives supported and passes the unit number to
   use during controller initialization.  A controller unit is not used, as all
   commands are scheduled on the drive unit associated with a given controller.

   On entry, the flags and controller state are examined to determine if a new
   controller command should be initiated or the current command should be
   continued.  If this routine is being called as a result of an event service,
   "uptr" will point at the unit being serviced.  Otherwise, "uptr" will be NULL
   (for example, when starting a new controller command).

   If the CLEARF flag is asserted, then perform a hard clear on the controller.
   Otherwise, if a 3000 channel error has occurred, then terminate any command
   in progress and return I/O Program Error status.  Otherwise, if the
   controller is currently busy with a command, or if this is an event service
   entry, then process the next step of the command.  Otherwise, if the CMRDY
   flag is asserted, then start a new command.  If none of these cases pertain,
   or if the controller is now idle, poll the drives for attention.

   In all cases, return a combined function set and outbound data word to the
   caller.


   Implementation notes:

    1. This routine will be entered when the drive unit event service occurs
       for seek completion, and Seek_Phase processing in continue_command will
       set the drive's Attention bit.  The drives must then be polled and the
       return functions and TIO value set to generate a CPU interrupt.

       A seek command started on one drive while a second drive already has its
       Attention bit set would seem to overwrite the first drive's Seek
       completion status with the second drive's Attention status.  However,
       this won't occur because INTOK will not be set until the first drive's
       channel program completes, and so the drive poll is inhibited until then.
*/

CNTLR_IFN_IBUS dl_controller (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data)
{
CNTLR_IFN_IBUS outbound;

dpprintf (cvptr->device, DL_DEB_IOB, "Controller (%s) received data %06o with flags %s\n",
          state_name [cvptr->state], data, fmt_bitset (flags, flag_format));

if (flags & CLEARF) {                                   /* if the CLEAR flag is asserted */
    clear_controller (cvptr, Hard_Clear);               /*   then perform a hard clear on the controller */
    outbound = NO_ACTION;                               /*     and take no other action on return */

    dpprintf (cvptr->device, DL_DEB_CMD, "Hard clear\n");
    }

else if (flags & XFRNG) {                               /* otherwise if a channel error has occurred */
    end_command (cvptr, uptr, IO_Program_Error);        /*   then terminate the command with error status */
    cvptr->spd_unit = 0;                                /*     and clear the SPD and unit parts of the status */

    outbound = status_functions [IO_Program_Error]      /* set the Status-1 value for WRTIO */
                 | S1_STATUS (IO_Program_Error);
    }

else if (uptr || cvptr->state == Busy_State)                /* otherwise if a command is in process */
    outbound = continue_command (cvptr, uptr, flags, data); /*   then continue with command processing */

else if (flags & CMRDY)                                 /* otherwise if a new command is ready */
    outbound = start_command (cvptr, flags, data);      /*   then begin command execution */

else                                                    /* otherwise there's nothing to do */
    outbound = NO_ACTION;                               /*   except possibly poll for attention */

if (cvptr->state == Idle_State                          /* if the controller is idle */
  && cvptr->type == MAC                                 /*   and it's a MAC controller */
  && flags & INTOK)                                     /*     and interrupts are allowed */
    outbound = poll_drives (cvptr);                     /*       then poll the drives for attention */

dpprintf (cvptr->device, DL_DEB_IOB, "Controller (%s) returned data %06o with functions %s\n",
          state_name [cvptr->state], DLIBUS (outbound),
          fmt_bitset (DLIFN (outbound), function_format));

return outbound;
}


/* Start a new command.

   This routine simulates the controller microcode entry into the command
   executor corresponding to the command presented by the CPU interface.  It's
   called when the controller is waiting for a command and the interface asserts
   CMRDY to indicate that a new command is available.  It returns a set of
   action functions and a data word to the caller.  For a good command, it also
   sets up the next phase of operation on the controller and/or drive unit and
   schedules the unit(s) as appropriate.

   On entry, the command word is supplied in the "inbound_data" parameter; this
   simulates the microcode issuing an IFPRF (Interface Prefetch) to obtain the
   command.  The opcode is isolated from the command word and checked for
   validity.  If it's OK, it's used as an index into the command properties
   table.  If the command contains a unit number field, it is extracted, checked
   for validity, and used to derive a pointer to the corresponding UNIT
   structure.  If the command does not access the drive, or if the unit number
   is invalid, the unit pointer is set to NULL.  A pointer to the controller
   unit is also set up; for ICD controllers, the controller and drive unit are
   the same.

   The controller library supports up to eight drives per MAC controller and one
   drive per ICD controller.  Unit numbers 0-7 represent valid drive addresses
   for a MAC controller.  The unit number field is ignored for an ICD
   controller, and unit 0 is always implied.  In simulation, MAC unit numbers
   correspond one-for-one with device units, whereas one ICD controller is
   associated with each of the several device units that are independently
   addressed as unit 0.

   The MAC controller firmware allows access to unit numbers 8-10 without
   causing a Unit Unavailable error.  Instead, the controller reports these
   legal-but-invalid units as permanently offline.

   If a diagnostic override is defined, and the cylinder, head, sector, and
   opcode values from the current override entry match their corresponding
   controller values, then the status and SPD values are set from the entry
   rather than being cleared, and the pointer is moved to the next entry.  The
   controller is then set to the busy state, and the validity of the opcode and
   unit are checked.  If any errors are detected, the appropriate status is set,
   and the controller unit is scheduled for the End_Phase to simulate the
   controller processing overhead.

   If the command and unit are valid, then if the command accesses a drive unit,
   the unit's OPCODE field is set, and any pending Attention status is cleared.
   If the command takes or returns parameters, then the Parameter_Phase is set
   up on the controller unit and the wait timer is started.  Commands that
   return parameters temporarily store their parameter values in the sector
   buffer at this time for return as the CPU interface requests them.  The Cold
   Load Read and Recalibrate commands start their respective seeks at this time,
   and commands that complete immediately, e.g., Set File Mask, Wakeup, etc.,
   schedule the End_Phase on the controller unit and set up the status value for
   return if they end with the WRTIO function.

   Finally, the controller and/or drive units are activated if they were
   scheduled.  If a seek is in progress on a drive when a command that
   waits for seek completion is started, the unit is not rescheduled.  Instead,
   the unit is left in the Seek_Phase, but the unit's OPCODE field is changed to
   reflect the new command so that the command will start automatically when the
   seek completes.


   Implementation notes:

    1. A command is started only if the controller is not busy.  Therefore, the
       target unit can be only in either the Seek_Phase or the Idle_Phase, as
       all other phases will have the controller in the Busy_State.

    2. Commands that access units and take parameters (e.g., Verify) set up the
       parameter access on the controller unit but perform the rest of the
       operation on the drive unit.  The controller must be used so that
       parameters may be read for the next command for a unit that is currently
       seeking (stacked commands wait for seek completion after parameters have
       been read).

    3. Drive Attention may be still set on a drive that has completed a seek but
       has not been able to interrupt the CPU before a new command is started.
       Therefore, Attention is always cleared when a command starts.

    4. In hardware, the Recalibrate command waits for a seek-in-progress to
       complete before the RCL tag is sent to the drive.  In simulation,
       repositioning the heads to cylinder 0 is started immediately, but any
       remaining time from a seek-in-progress is added to the time required for
       repositioning.  The effect is that recalibration completes in the time it
       would have taken for the seek-in-progress to complete followed by
       repositioning from that location.

    5. The Cold Load Read command does not check for Access Not Ready before
       issuing the seek to cylinder 0, so if a seek or recalibrate is in
       progress, the drive will reject it with a Seek Check error.  However, the
       command continues, and when the seek in progress completes and the read
       begins, the Seek Check error will abort the command at this point with a
       Status-2 Error.

    6. ECC is not simulated, so the Request Syndrome command always returns zero
       values for the displacement and patterns and Uncorrectable Data Error for
       the status.  Correctable Data Error status cannot occur unless a
       diagnostic override is in effect.

    7. The Wakeup command references a drive unit but is scheduled on the
       controller unit because it may be issued while the drive is seeking.
*/

static CNTLR_IFN_IBUS start_command (CVPTR cvptr, CNTLR_FLAG_SET inbound_flags, CNTLR_IBUS inbound_data)
{
UNIT *cuptr, *duptr, *rptr;
uint32 unit;
int32 seek_wait_time;
PRPTR props;
CNTLR_IFN_IBUS outbound;
DIAG_ENTRY *dop = NULL;

wait_timer (cvptr, CLEAR);                              /* stop the command wait timer */

cvptr->opcode = CM_OPCODE (inbound_data);               /* get the opcode from the command */

if (cvptr->opcode > LAST_OPCODE                                 /* if the opcode is undefined */
  || cvptr->type > LAST_CNTLR                                   /*   or the controller type is undefined */
  || cmd_props [cvptr->opcode].valid [cvptr->type] == FALSE)    /*     or the opcode is not valid for this controller */
    cvptr->opcode = Invalid_Opcode;                             /*       then replace it with the invalid opcode */

props = &cmd_props [cvptr->opcode];                     /* get the properties associated with the opcode */

if (cvptr->type == MAC) {                               /* if this a MAC controller */
    if (props->unit_field)                              /*   then if the unit field is defined */
        unit = CM_UNIT (inbound_data);                  /*     then get it from the command */
    else                                                /*   otherwise the unit is not specified in the command */
        unit = 0;                                       /*     so the unit is always unit 0 */

    cuptr = CNTLR_UPTR;                                 /* set the controller unit pointer */

    if (unit > DL_MAXDRIVE                              /* if the unit number is invalid */
      || props->unit_access == FALSE)                   /*   or the command accesses the controller only */
        duptr = NULL;                                   /*     then the drive pointer does not correspond to a unit */
    else                                                /* otherwise the command accesses a valid drive unit */
        duptr = cvptr->device->units + unit;            /*   so set the drive pointer to the unit */
    }

else {                                                  /* otherwise this is an ICD or CS/80 controller */
    unit = 0;                                           /*   so the unit value isn't used */
    cuptr = duptr =                                     /*     and the unit number was predefined */
      cvptr->device->units + cvptr->poll_unit;          /*       when the controller structure was initialized */
    }

dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command started\n",
          unit, opcode_name [cvptr->opcode]);

if (cvptr->dop_index >= 0)                              /* if the diagnostic override table is defined */
    dop = cvptr->dop_base + cvptr->dop_index;           /*   then point at the current entry */

if (dop                                                 /* if the table entry exists */
  && dop->cylinder == cvptr->cylinder                   /*   and the cylinder, */
  && dop->head     == cvptr->head                       /*     head, */
  && dop->sector   == cvptr->sector                     /*       sector, */
  && dop->opcode   == cvptr->opcode)  {                 /*         and opcode values match the current values */
    cvptr->spd_unit = dop->spd | unit;                  /*           then override the Spare/Protected/Defective */
    cvptr->status   = dop->status;                      /*             and status values from the override entry */

    cvptr->dop_index++;                                 /* point at the  */
    dop++;                                              /*   next table entry */

    dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u cylinder %u head %u sector %u diagnostic override\n",
              unit, cvptr->cylinder, cvptr->head, cvptr->sector);
    }

else if (props->clear_status) {                         /* otherwise if this command clears prior status */
    cvptr->status = Normal_Completion;                  /*   then do it */
    cvptr->spd_unit = unit;                             /*     and save the unit number for status requests */
    }


cvptr->state = Busy_State;                              /* the controller is now busy */
cvptr->index = 0;                                       /* reset the buffer index */
cvptr->count = 0;                                       /*   and the sector/word count */
cvptr->verify = props->verify_address;                  /* set the address verification flag */

cuptr->OPCODE = cvptr->opcode;                          /* set the controller unit opcode */
cuptr->wait = NO_EVENT;                                 /*   and assume no controller scheduling */

outbound = cmd_functions [cvptr->opcode] [Idle_Phase];  /* set up the initial function set and zero data */


if (cvptr->opcode == Invalid_Opcode)                        /* if the opcode is invalid */
    set_completion (cvptr, cuptr, Illegal_Opcode);          /*   then finish with an illegal opcode error */

else if (props->unit_check && unit > MAX_UNIT)              /* otherwise if the unit number is checked and is illegal */
    set_completion (cvptr, cuptr, Unit_Unavailable);        /*   then finish with a unit unavailable error */

else if (props->unit_check && unit > DL_MAXDRIVE            /* otherwise if the unit number is checked and is invalid */
  || props->seek_wait && (drive_status (duptr) & S2_STOPS)) /*   or if we're waiting for an offline drive */
    set_completion (cvptr, cuptr, Status_2_Error);          /*     then finish with a Status-2 error */

else {                                                  /* otherwise the command and unit are valid */
    if (duptr) {                                        /* if the drive unit is accessed  */
        duptr->OPCODE = cvptr->opcode;                  /*   then set the drive opcode for later reference */
        duptr->wait = NO_EVENT;                         /* assume no drive scheduling */
        duptr->STATUS &= ~S2_ATTENTION;                 /* clear any pending Attention status */
        }

    if (props->param_count != 0) {                      /* if the command takes or returns parameters */
        cvptr->length = props->param_count;             /*   then set the parameter count */
        cuptr->PHASE = Parameter_Phase;                 /* set up the parameter transfer on the controller */
        wait_timer (cvptr, SET);                        /*   and start the timer to wait for the first parameter */
        }

    switch (cvptr->opcode) {                            /* dispatch the command for initiation */

        case Cold_Load_Read:
            cvptr->cylinder = 0;                                /* set the cylinder address to 0 */
            cvptr->head = CM_HEAD (inbound_data);               /* set the head and */
            cvptr->sector = CM_SECTOR (inbound_data);           /*   sector addresses from the command */

            if (start_seek (cvptr, duptr) == FALSE)             /* start the seek; if it failed */
                set_completion (cvptr, cuptr, Status_2_Error);  /*   then set up the completion status */

            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s from cylinder %u head %u sector %u\n",
                      unit, opcode_name [Cold_Load_Read], cvptr->cylinder, cvptr->head, cvptr->sector);
            break;                                              /* wait for seek completion */


        case Recalibrate:
            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s to cylinder 0\n",
                      unit, opcode_name [Recalibrate]);

            if (duptr->PHASE == Seek_Phase) {                   /* if the unit is currently seeking */
                seek_wait_time = sim_activate_time (duptr);     /*   then get the remaining event time */

                sim_cancel (duptr);                             /* cancel the event to allow rescheduling */
                duptr->PHASE = Idle_Phase;                      /*   and idle the drive so that the seek succeeds */

                dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command waiting for seek completion\n",
                          unit, opcode_name [Recalibrate]);
                }

            else                                                /* otherwise the drive is idle */
                seek_wait_time = 0;                             /*   so there's no seek wait time */

            if (start_seek (cvptr, duptr) == FALSE)                 /* start the seek; if it failed */
                set_completion (cvptr, cuptr, Status_2_Error);      /*   then set up the completion status */

            else if (cvptr->type == MAC)                            /* otherwise if this a MAC controller */
                set_completion (cvptr, cuptr, Normal_Completion);   /*   then schedule seek completion */

            duptr->wait = duptr->wait + seek_wait_time;             /* increase the delay by any remaining seek time */
            break;                                                  /*   and wait for the recalibrate to complete */


        case Request_Status:
            cvptr->buffer [0] = (DL_BUFFER) (cvptr->spd_unit    /* set the Status-1 value */
                                  | S1_STATUS (cvptr->status)); /*   into the buffer */

            if (cvptr->type == MAC)                     /* if this a MAC controller */
                if (unit > DL_MAXDRIVE)                 /*   then if the unit number is invalid */
                    rptr = NULL;                        /*     then it does not correspond to a unit */
                else                                    /*   otherwise the unit is valid */
                    rptr = cvptr->device->units + unit; /*     so get the address of the referenced unit */
            else                                        /* otherwise it is not a MAC controller */
                rptr = duptr;                           /*   so the referenced unit is the current unit */

            cvptr->buffer [1] = (DL_BUFFER) drive_status (rptr);    /* set the Status-2 value into the buffer */

            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s returns %sunit %u | %s and %s%s | %s\n",
                      unit, opcode_name [Request_Status],
                      fmt_bitset (cvptr->spd_unit, status_1_format),
                      CM_UNIT (cvptr->spd_unit), dl_status_name (cvptr->status),
                      (cvptr->buffer [1] & S2_ERROR ? "error | " : ""),
                      drive_props [S2_TO_DRIVE_TYPE (cvptr->buffer [1])].name,
                      fmt_bitset (cvptr->buffer [1], status_2_format));

            if (rptr)                                   /* if the referenced unit is valid */
                rptr->STATUS &= ~S2_FIRST_STATUS;       /*   then clear the First Status bit */

            cvptr->spd_unit = S1_UNIT (unit);           /* save the unit number referenced in the command */

            if (unit > MAX_UNIT)                        /* if the unit number is illegal */
                cvptr->status = Unit_Unavailable;       /*   then the next status will be Unit Unavailable */
            else                                        /* otherwise a legal unit */
                cvptr->status = Normal_Completion;      /*   clears the controller status */
            break;


        case Request_Sector_Address:
            if (drive_status (duptr) & S2_NOT_READY)            /* if the drive is not ready */
                set_completion (cvptr, cuptr, Status_2_Error);  /*   then finish with Not Ready status */

            else                                                /* otherwise the drive is ready */
                cvptr->buffer [0] =                             /*   so calculate the current sector address */
                  (DL_BUFFER) CURRENT_SECTOR (cvptr, duptr);

            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s returns sector %u\n",
                      unit, opcode_name [Request_Sector_Address], cvptr->buffer [0]);
            break;


        case Clear:
            clear_controller (cvptr, Soft_Clear);               /* clear the controller */
            set_completion (cvptr, cuptr, Normal_Completion);   /*   and schedule the command completion */

            dpprintf (cvptr->device, DL_DEB_CMD, "%s\n", opcode_name [Clear]);
            break;


        case Request_Syndrome:
            if (cvptr->status == Correctable_Data_Error) {      /* if this is a correction override */
                cvptr->buffer [3] = (DL_BUFFER) dop->spd;       /*   then load the displacement */
                cvptr->buffer [4] = (DL_BUFFER) dop->cylinder;  /*     and three */
                cvptr->buffer [5] = (DL_BUFFER) dop->head;      /*       syndrome words */
                cvptr->buffer [6] = (DL_BUFFER) dop->sector;    /*         from the override entry */

                cvptr->dop_index++;                             /* point at the  */
                dop++;                                          /*   next table entry */
                }

            else {                                          /* otherwise no correction data was supplied */
                cvptr->buffer [3] = 0;                      /*   so the displacement is always zero */
                cvptr->buffer [4] = 0;                      /*     as are */
                cvptr->buffer [5] = 0;                      /*       the three */
                cvptr->buffer [6] = 0;                      /*         syndrome words */

                if (cvptr->status == Normal_Completion)         /* if we've been called without an override */
                    cvptr->status = Uncorrectable_Data_Error;   /*   then presume that an uncorrectable error occurred */
                }

            cvptr->buffer [0] = (DL_BUFFER) (cvptr->spd_unit    /* save the Status-1 value */
                                  | S1_STATUS (cvptr->status)); /*   in the buffer */

            set_address (cvptr, 1);                         /* save the CHS values in the buffer */

            dpprintf (cvptr->device, DL_DEB_CMD, "%s returns %sunit %u | %s | cylinder %u head %u sector %u | "
                                                 "syndrome %06o %06o %06o %06o\n",
                      opcode_name [Request_Syndrome], fmt_bitset (cvptr->spd_unit, status_1_format),
                      CM_UNIT (cvptr->spd_unit), dl_status_name (cvptr->status),
                      cvptr->cylinder, cvptr->head, cvptr->sector,
                      cvptr->buffer [3], cvptr->buffer [4], cvptr->buffer [5], cvptr->buffer [6]);

            next_sector (cvptr, cvptr->device->units        /* address the next sector of the last unit used */
                                  + S1_UNIT (cvptr->spd_unit));
            break;


        case Set_File_Mask:
            cvptr->file_mask = CM_FILE_MASK (inbound_data);     /* save the supplied file mask */

            outbound |= CM_RETRY (inbound_data);                /* return the retry count */

            set_completion (cvptr, cuptr, Normal_Completion);   /* schedule the command completion */

            dpprintf (cvptr->device, DL_DEB_CMD, "%s to %sretries %u\n",
                      opcode_name [Set_File_Mask], fmt_bitset (cvptr->file_mask, file_mask_format),
                      CM_RETRY (inbound_data));
            break;


        case Request_Disc_Address:
            set_address (cvptr, 0);                     /* set the controller's CHS values into the buffer */

            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s returns cylinder %u head %u sector %u\n",
                      unit, opcode_name [Request_Disc_Address], cvptr->cylinder, cvptr->head, cvptr->sector);
            break;


        case End:
            dpprintf (cvptr->device, DL_DEB_CMD, "%s\n", opcode_name [End]);

            end_command (cvptr, NULL, Normal_Completion);   /* end the command and idle the controller */
            break;


        case Wakeup:
            set_completion (cvptr, cuptr, Unit_Available);  /* schedule the command completion */

            dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s\n",
                      unit, opcode_name [Wakeup]);
            break;


        /* these commands wait for seek completion before starting */

        case Read_Without_Verify:
            cvptr->verify = FALSE;                      /* do not verify until a track is crossed */
            inbound_data &= ~CM_SPD_MASK;               /* clear the SPD bits to avoid changing the state */

        /* fall into the Initialize case */

        case Initialize:
            cvptr->spd_unit |= CM_SPD (inbound_data);   /* merge the SPD flags with the unit */

        /* fall into the read/write cases */

        case Read:
        case Read_Full_Sector:
        case Write:
        case Write_Full_Sector:
            if (duptr->PHASE == Seek_Phase)                 /* if the unit is currently seeking */
                dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command waiting for seek completion\n",
                          unit, opcode_name [cvptr->opcode]);

            else                                            /* otherwise the unit is idle */
                set_rotation (cvptr, duptr);                /*   so set up the rotation phase and latency */
            break;


        /* these commands take parameters but otherwise require no preliminary work */

        case Seek:
        case Verify:
        case Address_Record:
        case Read_With_Offset:
        case Load_TIO_Register:
            break;


        case Invalid_Opcode:                            /* for completeness; invalid commands are not dispatched */
            break;
        }
    }


if (cvptr->state == Busy_State) {                       /* if the command has not completed immediately */
    if (cuptr->wait != NO_EVENT && cvptr->type == MAC)  /*   then if the controller unit is scheduled */
        activate_unit (cvptr, cuptr);                   /*      then activate it */

    if (duptr && duptr->wait != NO_EVENT)               /*   and if the drive unit is valid and scheduled */
        activate_unit (cvptr, duptr);                   /*      then activate it as well */
    }

if (outbound & WRTIO)                                           /* if status is expected immediately */
    outbound |= S1_STATUS (cvptr->status) | cvptr->spd_unit;    /*   then return the Status-1 value */

return outbound;                                        /* return the data word and function set */
}


/* Continue the current command.

   This routine simulates continuing execution of the controller microcode for
   the current command.  It's called whenever the controller has had to wait for
   action from the CPU interface or the drive unit, and that action has now
   occurred.  Typically, this would be whenever the interface flag status
   changes, or a unit's event service has been entered.  It returns a set of
   action functions and a data word to the caller.  It also sets up the next
   phase of operation on the controller and/or drive unit and schedules the
   unit(s) as appropriate.

   On entry, the "uptr" parameter is set to NULL if the controller was called
   for a CPU interface action, or it points to the unit whose event service was
   just called; this may be either the controller unit or a drive unit.  The
   "inbound_flags" and "inbound_data" parameters contain the CPU interface flags
   and data buffer values.

   If the entry is for a unit service, the unit number of the drive requesting
   service is determined.  If the entry is for the CPU interface, the controller
   is checked; if it's idle, the routine returns because no command is in
   progress.

   While a unit is activated, the current phase indicates the reason for the
   activation, i.e., what the simulated drive is "doing" at the moment, as
   follows:

     Idle_Phase        -- waiting for the next command to be issued (note that
                          this phase, which indicates that the unit is not
                          scheduled, is distinct from the controller Idle_State,
                          which indicates that the controller itself is idle)

     Parameter_Phase   -- waiting for a parameter to be transferred to or from
                          the interface

     Seek_Phase        -- waiting for a seek to complete (explicit or automatic)

     Rotate_Phase      -- waiting for the target sector to arrive under the head

     Data_Phase        -- waiting for the interface to accept or return the next
                          data word to be transferred to or from the drive

     Intersector_Phase -- waiting for the controller to finish executing the
                          end-of-sector microcode

     End_Phase         -- waiting for the controller to finish executing the
                          microcode corresponding to the current command

   Depending on the current command opcode and phase, a number of actions may be
   taken:

     Idle_Phase -- If the controller unit is being serviced, then the 1.74
     second command wait timer has expired while waiting for a new command.
     Reset the file mask and idle the controller.

     Parameter_Phase -- If the controller unit is being serviced, then the 1.74
     second command wait timer has expired while waiting for a parameter from
     the CPU.  Reset the file mask and idle the controller.  Otherwise, for
     outbound parameters, return the next word from the sector buffer and
     restart the timer; if the last word has been sent, end the command.  For
     inbound parameters, store the next word in the appropriate controller state
     variable; if the last word has been received, end the command or set up the
     next operation phase (seek, rotate, etc.).

     Seek_Phase -- If a Seek or Recalibrate has completed, set Drive Attention
     status.  All other commands have been waiting for seek completion before
     starting, so set up the rotate phase to begin the command.

     Rotate_Phase -- Set up the read or write of the current sector.  For all
     commands except Verify, proceed to the data phase to begin the data
     transfer.  For Verify, skip the data phase and proceed directly to the
     end-of-sector processing, but schedule that event as though the full sector
     rotation time had elapsed.

     Data_Phase -- For read transfers, return the next word from the sector
     buffer, or for write transfers, store the next word into the sector buffer,
     and schedule the next data phase transfer if the CPU has not indicated an
     end-of-data condition.  If it has, or if the last word of the sector has
     been transmitted, schedule the intersector phase.

     Intersector_Phase -- Complete the read or write of the current sector.  If
     the CPU has indicated an end-of-data condition, end the command.
     Otherwise, address the next sector and schedule the rotate phase.

     End_Phase -- End the command.  The end phase is used to provide a
     controller delay when an operation has no other command phases.

   At the completion of the current phase, the next phase is scheduled, if
   required, before returning the appropriate function set and data word to the
   caller.

   The commands employ the various phases as follows:
                                                               Inter
       Command                  Param   Seek   Rotate   Data   sector   End
     +------------------------+-------+------+--------+------+--------+-----+
     | Cold Load Read         |   -   |  D   |   D    |  D   |   D    |  -  |
     | Recalibrate            |   -   |  D   |   -    |  -   |   -    |  -  |
     | Seek                   |   c   |  D   |   -    |  -   |   -    |  -  |
     | Request Status         |   c   |  -   |   -    |  -   |   -    |  -  |
     | Request Sector Address |   c   |  -   |   -    |  -   |   -    |  -  |
     | Read                   |   -   |  -   |   D    |  D   |   D    |  -  |
     | Read Full Sector       |   -   |  -   |   D    |  D   |   D    |  -  |
     | Verify                 |   c   |  -   |   D    |  -   |   D    |  -  |
     | Write                  |   -   |  -   |   D    |  D   |   D    |  -  |
     | Write Full Sector      |   -   |  -   |   D    |  D   |   D    |  -  |
     | Clear                  |   -   |  -   |   -    |  -   |   -    |  C  |
     | Initialize             |   -   |  -   |   D    |  D   |   D    |  -  |
     | Address Record         |   c   |  -   |   -    |  -   |   -    |  -  |
     | Request Syndrome       |   c   |  -   |   -    |  -   |   -    |  -  |
     | Read With Offset       |   c   |  -   |   D    |  D   |   D    |  -  |
     | Set File Mask          |   -   |  -   |   -    |  -   |   -    |  C  |
     | Invalid Opcode         |   -   |  -   |   -    |  -   |   -    |  C  |
     | Read Without Verify    |   -   |  -   |   D    |  D   |   D    |  -  |
     | Load TIO Register      |   c   |  -   |   -    |  -   |   -    |  -  |
     | Request Disc Address   |   c   |  -   |   -    |  -   |   -    |  -  |
     | End                    |   -   |  -   |   -    |  -   |   -    |  -  |
     | Wakeup                 |   -   |  -   |   -    |  -   |   -    |  C  |
     +------------------------+-------+------+--------+------+--------+-----+

     Key:
       C = controller unit is scheduled
       c = controller is called directly by the CPU interface
       D = drive unit is scheduled
       - = the phase is not used


   Implementation notes:

    1. The "%.0u" print specification in the trace call absorbs the zero "unit"
       value parameter without printing when the controller unit is specified.

    2. The Seek command does not check for Access Not Ready before issuing the
       seek, so if a prior seek is in progress, the drive will reject it with a
       Seek Check error.  However, the parameters (e.g., controller CHS
       addresses) are still set as though the command had succeeded.

       A Seek command will return to the Poll Loop with Seek Check status set.
       When the seek in progress completes, the controller will interrupt with
       Drive Attention status.  The controller address will differ from the
       drive address, so it's incumbent upon the caller to issue a Request
       Status command after the seek, which will return Status-2 Error status.

    3. The set of interface functions to assert on command completion is
       normally specified by the End_Phase entry in the cmd_functions table,
       regardless of whether or not a command has an end phase.  However, the
       Request Syndrome command returns either Correctable Data Error or
       Uncorrectable Data Error for normal command completion.  These status
       returns would normally include DVEND to indicate that the command should
       be retried, but that's wrong for Request Syndrome, so we explicitly
       override the function set for this command.

    4. Command completion must be detected by the controller state changing to
       "not busy" rather than simply being "not busy" at the end of the routine.
       Otherwise, a seek that completes while the controller is waiting for a
       command would re-issue the end phase functions.

    5. The disc is a synchronous device, so overrun or underrun can occur if the
       interface is not ready when the controller must transfer data.  There are
       four conditions that lead to an overrun or underrun:

         a. The controller is ready with a disc read word (IFCLK * IFIN), but
            the interface buffer is full (DTRDY).

         b. The controller needs a disc write word (IFCLK * IFOUT), but the
            interface buffer is empty (~DTRDY).

         c. The CPU attempts to read a word, but the interface buffer is empty
            (~DTRDY).

         d. The CPU attempts to write a word, but the interface buffer is full
            (DTRDY).

       The 13037 controller ORs the interface-supplied OVRUN signal with an
       internal overrun latch that sets on condition 2 above (write underrun).
       However, both the 13175A HP 1000 interface and the 30229B HP 3000
       interface assert OVRUN for all four conditions, so the latch is not
       simulated.

    6. Not all changes of CPU interface flag status are significant.  If the
       routine is called when it isn't needed, the routine simply returns with
       no action.
*/

static CNTLR_IFN_IBUS continue_command (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET inbound_flags, CNTLR_IBUS inbound_data)
{
const t_bool service_entry = (uptr != NULL);            /* set TRUE if entered via unit service */
CNTLR_OPCODE opcode;
CNTLR_PHASE phase;
CNTLR_IFN_IBUS outbound;
t_bool controller_service, controller_was_busy;
int32 unit;
uint32 sector_count;

if (service_entry) {                                    /* if this is an event service entry */
    unit = (int32) (uptr - cvptr->device->units);       /*   then get the unit number */

    controller_service = (uptr == CNTLR_UPTR            /* set TRUE if the controller is being serviced */
                            && cvptr->type == MAC);
    }

else if (CNTLR_UPTR->PHASE == Idle_Phase)               /* otherwise if this interface entry isn't needed */
    return NO_ACTION;                                   /*   then quit as there's nothing to do */

else {                                                  /* otherwise the controller is expecting the entry */
    uptr = CNTLR_UPTR;                                  /* set up to use the controller unit */
    unit = CNTLR_UNIT;                                  /*   and unit number */
    controller_service = FALSE;                         /*     but note that this isn't a service entry */
    }


opcode = (CNTLR_OPCODE) uptr->OPCODE;                   /* get the current opcode */
phase  = (CNTLR_PHASE)  uptr->PHASE;                    /*   and command phase */

if (controller_service == FALSE || phase == End_Phase)
    dpprintf (cvptr->device, DL_DEB_STATE, (unit == CNTLR_UNIT
                                              ? "Controller unit%.0d %s %s phase entered from %s\n"
                                              : "Unit %d %s %s phase entered from %s\n"),
              (unit == CNTLR_UNIT ? 0 : unit), opcode_name [opcode], phase_name [phase],
              (service_entry ? "service" : "interface"));

controller_was_busy = (cvptr->state == Busy_State);     /* set TRUE if the controller was busy on entry */

outbound = cmd_functions [opcode] [phase];              /* set up the initial function return set */


switch (phase) {                                        /* dispatch the phase */

    case Idle_Phase:                                    /* the command wait timer has expired */
        clear_controller (cvptr, Timeout_Clear);        /*   so idle the controller and clear the file mask */

        outbound = NO_FUNCTIONS;                        /* clear the function set for an idle return */

        dpprintf (cvptr->device, DL_DEB_INCO, "Controller command wait timed out\n");
        break;


    case Parameter_Phase:
        if (controller_service) {                       /* if the parameter wait timer has expired */
            clear_controller (cvptr, Timeout_Clear);    /*   then idle the controller and clear the file mask */

            outbound = NO_FUNCTIONS;                    /* clear the function set for an idle return */

            dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command aborted with parameter wait timeout\n",
                      CM_UNIT (cvptr->spd_unit), opcode_name [opcode]);
            }

        else switch (opcode) {                          /* otherwise dispatch the command */

            case Request_Status:                        /* these commands */
            case Request_Disc_Address:                  /*   return parameters */
            case Request_Sector_Address:                /*     to the interface */
            case Request_Syndrome:
                if (cvptr->length == 0)                         /* if the last parameter has been sent */
                    end_command (cvptr, uptr, cvptr->status);   /*   then terminate the command with the preset status */

                else {                                          /* otherwise there are more to send */
                    outbound |= cvptr->buffer [cvptr->index++]; /*   so return the next value from the buffer */
                    cvptr->length = cvptr->length - 1;          /*     and drop the parameter count */

                    wait_timer (cvptr, SET);                    /* restart the parameter timer */
                    }
                break;


            case Seek:                                          /* these commands receive parameters */
            case Address_Record:                                /*   from the interface */
                cvptr->buffer [cvptr->index++] =                /* save the current one in the buffer */
                   (DL_BUFFER) inbound_data;
                cvptr->length = cvptr->length - 1;              /*   and drop the parameter count */

                if (cvptr->length > 0)                          /* if another parameter is expected */
                    wait_timer (cvptr, SET);                    /*   then restart the parameter timer */

                else {                                              /* otherwise all parameters are in */
                    cvptr->cylinder = cvptr->buffer [0];            /*   so fill in the supplied cylinder */
                    cvptr->head = PI_HEAD (cvptr->buffer [1]);      /*     and head */
                    cvptr->sector = PI_SECTOR (cvptr->buffer [1]);  /*       and sector addresses */

                    if (opcode == Address_Record) {             /* if this is an Address Record command */
                        cvptr->eoc = CLEAR;                     /*   then clear the end-of-cylinder flag */

                        dpprintf (cvptr->device, DL_DEB_CMD, "%s to cylinder %u head %u sector %u\n",
                                  opcode_name [Address_Record],
                                  cvptr->cylinder, cvptr->head, cvptr->sector);

                        end_command (cvptr, uptr,               /* the command is now complete */
                                     Normal_Completion);
                        }

                    else {                                      /* otherwise it's a Seek command */
                        dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s to cylinder %u head %u sector %u\n",
                                  CM_UNIT (cvptr->spd_unit), opcode_name [Seek],
                                  cvptr->cylinder, cvptr->head, cvptr->sector);

                        uptr = cvptr->device->units             /* get the target unit */
                                 + CM_UNIT (cvptr->spd_unit);

                        if (start_seek (cvptr, uptr) == FALSE)  /* start the seek; if it failed, */
                            end_command (cvptr, uptr,           /*   then report the error */
                                         Status_2_Error);

                        else if (cvptr->type == MAC)            /* otherwise if this a MAC controller */
                            end_command (cvptr, uptr,           /*   then complete the command and idle the controller */
                                         Normal_Completion);
                        }                                       /* otherwise an ICD command ends when the seek completes */
                    }
                break;


            case Verify:
                if (inbound_data == 0)                  /* if the sector count is zero */
                    sector_count = 65536;               /*   then use the rollover count */
                else                                    /* otherwise */
                    sector_count = inbound_data;        /*   use the count as is */

                cvptr->count = sector_count * WORDS_PER_SECTOR; /* convert to the number of words to verify */

                dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s %u sector%s\n",
                          CM_UNIT (cvptr->spd_unit), opcode_name [Verify],
                          sector_count, (sector_count == 1 ? "" : "s"));

                wait_timer (cvptr, CLEAR);              /* stop the parameter timer */

                uptr = cvptr->device->units             /* get the target unit */
                         + CM_UNIT (cvptr->spd_unit);

                if (uptr->PHASE == Seek_Phase) {        /* if a seek is in progress, */
                    uptr->wait = NO_EVENT;              /*   then wait for it to complete */

                    dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command waiting for seek completion\n",
                              CM_UNIT (cvptr->spd_unit), opcode_name [Verify]);
                    }

                else                                    /* otherwise the unit is idle */
                    set_rotation (cvptr, uptr);         /*   so set up the rotation phase and latency */
                break;


            case Read_With_Offset:
                dpprintf (cvptr->device, DL_DEB_CMD, "Unit %u %s using %soffset %+d\n",
                          CM_UNIT (cvptr->spd_unit), opcode_name [Read_With_Offset],
                          fmt_bitset (inbound_data, offset_format),
                          (inbound_data & PI_NEG_OFFSET ? - (int) PI_OFFSET (inbound_data)
                                                        :   (int) PI_OFFSET (inbound_data)));

                wait_timer (cvptr, CLEAR);                  /* stop the parameter timer */

                uptr = cvptr->device->units                 /* get the target unit */
                         + CM_UNIT (cvptr->spd_unit);

                if (uptr->PHASE == Seek_Phase) {            /* if a seek is in progress, */
                    uptr->wait = NO_EVENT;                  /*   then wait for it to complete */

                    dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command waiting for seek completion\n",
                              CM_UNIT (cvptr->spd_unit), opcode_name [Read_With_Offset]);
                    }

                else {                                      /* otherwise the unit is idle */
                    uptr->PHASE = Seek_Phase;               /*   so schedule the seek phase */
                    uptr->wait = cvptr->dlyptr->seek_one;   /*     with the offset positioning delay */
                    }
                break;


            case Load_TIO_Register:
                wait_timer (cvptr, CLEAR);                      /* stop the parameter timer */

                dpprintf (cvptr->device, DL_DEB_CMD, "%s with %06o\n",
                          opcode_name [Load_TIO_Register], inbound_data);

                end_command (cvptr, uptr, Normal_Completion);   /* complete the command */

                return inbound_data                             /* return the supplied TIO value */
                         | cmd_functions [Load_TIO_Register] [End_Phase];
                break;


            default:                                    /* the remaining commands */
                break;                                  /*   do not have a parameter phase */
            }

        break;


    case Seek_Phase:
        switch (opcode) {                               /* dispatch the command */

            case Recalibrate:
            case Seek:
                if (cvptr->type == MAC) {               /* if this a MAC controller */
                    uptr->STATUS |= S2_ATTENTION;       /*   set Attention in the unit status */
                    uptr->PHASE = Idle_Phase;           /*     and idle the drive */
                    }

                else                                            /* otherwise this is an ICD or CS/80 drive */
                    end_command (cvptr, uptr, Drive_Attention); /*   so seeks end with Drive Attention status */
                break;


            case Cold_Load_Read:
                cvptr->file_mask = CM_SPARE_EN;             /* enable sparing in surface mode without auto-seek */

            /* fall into the default case */

            default:                                        /* a command was waiting on seek completion */
                set_rotation (cvptr, uptr);                 /*   so set up the rotation phase and latency */
                break;
            }

        break;


    case Rotate_Phase:
        switch (opcode) {                               /* dispatch the command */

            case Write:
            case Write_Full_Sector:
            case Initialize:
                start_write (cvptr, uptr);              /* start the sector write */
                break;


            case Read:
            case Read_Full_Sector:
            case Read_With_Offset:
            case Read_Without_Verify:
            case Cold_Load_Read:
                start_read (cvptr, uptr, inbound_flags);    /* start the sector read */
                break;


            case Verify:
                inbound_flags &= ~EOD;                          /* EOD is not relevant for Verify */

                if (start_read (cvptr, uptr, inbound_flags)) {  /* if the sector read was set up successfully */
                    uptr->PHASE = Intersector_Phase;            /*   then skip the data phase */
                    uptr->wait = cvptr->dlyptr->sector_full;    /*     and reschedule for the sector read time */
                    }
                break;


            default:                                    /* the remaining commands */
                break;                                  /*   do not have a rotate phase */
            }

        break;


    case Data_Phase:
        if (inbound_flags & EOD)                        /* if the transfer has ended */
            outbound = NO_FUNCTIONS;                    /*   then don't assert IFIN/IFOUT on return */

        switch (opcode) {                               /* dispatch the command */

            case Read:
            case Read_With_Offset:
            case Read_Without_Verify:
            case Read_Full_Sector:
            case Cold_Load_Read:
                if ((inbound_flags & EOD) == NO_FLAGS) {            /* if the transfer continues */
                    outbound |= cvptr->buffer [cvptr->index++];     /*   then get the next word from the buffer */

                    cvptr->count  = cvptr->count  + 1;              /* count the */
                    cvptr->length = cvptr->length - 1;              /*   transfer */

                    dpprintf (cvptr->device, DL_DEB_XFER, "Unit %d %s word %u is %06o\n",
                              unit, opcode_name [opcode],
                              cvptr->count, DLIBUS (outbound));
                    }

                uptr->wait = cvptr->dlyptr->data_xfer;              /* set the transfer delay */

                if (cvptr->length == 0 || inbound_flags & EOD) {    /* if the buffer is empty or the transfer is done */
                      uptr->PHASE = Intersector_Phase;              /*   then set up the intersector phase */

                      if (cvptr->device->flags & DEV_REALTIME)      /* if we're in realistic timing mode */
                          uptr->wait = uptr->wait                   /*   then account for the actual delay */
                                         * (cvptr->length + cmd_props [opcode].postamble_size);
                      }
                break;


            case Write:
            case Write_Full_Sector:
            case Initialize:
                if ((inbound_flags & EOD) == NO_FLAGS) {    /* if the transfer continues */
                    cvptr->buffer [cvptr->index++] =        /*   then store the next word in the buffer */
                       (DL_BUFFER) inbound_data;

                    cvptr->count  = cvptr->count  + 1;      /* count the */
                    cvptr->length = cvptr->length - 1;      /*   transfer */

                    dpprintf (cvptr->device, DL_DEB_XFER, "Unit %d %s word %u is %06o\n",
                              unit, opcode_name [opcode],
                              cvptr->count, inbound_data);
                    }

                uptr->wait = cvptr->dlyptr->data_xfer;              /* set the transfer delay */

                if (cvptr->length == 0 || inbound_flags & EOD) {    /* if the buffer is empty or the transfer is done */
                      uptr->PHASE = Intersector_Phase;              /*   then set up the intersector phase */

                      if (cvptr->device->flags & DEV_REALTIME)      /* if we're in realistic timing mode */
                          uptr->wait = uptr->wait                   /*   then account for the actual delay */
                                         * (cvptr->length + cmd_props [opcode].postamble_size);
                      }
                break;


            default:                                    /* the remaining commands */
                break;                                  /*   do not have a data phase */
            }

        break;


    case Intersector_Phase:
        switch (opcode) {                               /* dispatch the command */

            case Read:
            case Read_With_Offset:
            case Read_Without_Verify:
            case Read_Full_Sector:
            case Cold_Load_Read:
                end_read (cvptr, uptr, inbound_flags);  /* end the sector read */
                break;


            case Write:
            case Write_Full_Sector:
            case Initialize:
                end_write (cvptr, uptr, inbound_flags); /* end the sector write */
                break;


            case Verify:
                cvptr->count = cvptr->count - WORDS_PER_SECTOR; /* decrement the word count */

                if (cvptr->count > 0)                   /* if there more sectors to verify */
                    inbound_flags &= ~EOD;              /*   then this is not the end of data */
                else                                    /* otherwise the command is complete */
                    inbound_flags |= EOD;               /*   and this is the end of data */

                end_read (cvptr, uptr, inbound_flags);  /* end the sector read */
                break;


            default:                                    /* the remaining commands */
                break;                                  /*   do not have an intersector phase */
            }

        break;


    case End_Phase:
        end_command (cvptr, uptr, cvptr->status);       /* complete the command with the preset status */
        break;
    }


if (uptr->wait != NO_EVENT)                             /* if the unit has been scheduled */
    activate_unit (cvptr, uptr);                        /*   then activate it */

if (controller_was_busy && cvptr->state != Busy_State) {    /* if the command has just completed */
    if (cvptr->status == Normal_Completion                  /*   then if the command completed normally */
      || opcode == Request_Syndrome)                        /*   or it was a Request Syndrome command */
        outbound = cmd_functions [opcode] [End_Phase];      /*     then use the normal exit function set */
    else                                                    /*   otherwise */
        outbound = status_functions [cvptr->status];        /*     the function set depends on the status */

    if (outbound & WRTIO)                                           /* if the TIO register will be written */
        outbound |= S1_STATUS (cvptr->status) | cvptr->spd_unit;    /*   then include the Status-1 value */
    }

return outbound;                                        /* return the data word and function set */
}


/* Poll the drives for Attention status.

   MAC controllers complete their Seek and Recalibrate commands when the seeks
   are initiated, so that other drives may be serviced during the waits.  A
   drive will set its Attention status when its seek completes, and the
   controller must poll the drives for attention requests when it is idle and
   interrupts are allowed by the CPU interface.

   Starting with the last unit that had previously requested attention, each
   drive is checked in sequence.  If a drive has its Attention status set, the
   controller saves its unit number, sets the result status to Drive Attention,
   and enters the command wait state.  The routine returns a function set that
   indicates that an interrupt should be generated.  The next time the routine
   is called, the poll begins with the last unit that requested attention, so
   that each unit is given an equal chance to respond.

   If no unit is requesting attention, the routine returns an empty function set
   to indicate that no interrupt should be generated.

   ICD controllers do not call this routine, because each controller waits for
   seek completion on its dedicated drive before completing the associated Seek
   or Recalibrate command.
*/

static CNTLR_IFN_IBUS poll_drives (CVPTR cvptr)
{
uint32  unit;
UNIT   *units = cvptr->device->units;

dpprintf (cvptr->device, DL_DEB_INCO, "Controller polled drives for attention\n");

for (unit = 0; unit <= DL_MAXDRIVE; unit++) {           /* check each unit in turn */
    cvptr->poll_unit =                                  /* start with the last unit checked */
      (cvptr->poll_unit + 1) % (DL_MAXDRIVE + 1);       /*   and cycle back to unit 0 */

    if (units [cvptr->poll_unit].STATUS & S2_ATTENTION) {   /* if the unit is requesting attention, */
        units [cvptr->poll_unit].STATUS &= ~S2_ATTENTION;   /*   clear the Attention status */

        dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u requested attention\n",
                  cvptr->poll_unit);

        cvptr->spd_unit = cvptr->poll_unit;             /* set the controller's unit number */
        cvptr->status = Drive_Attention;                /*   and status */

        cvptr->state = Wait_State;                      /* set the controller state to waiting */
        wait_timer (cvptr, SET);                        /* start the command wait timer */

        return status_functions [Drive_Attention]               /* tell the caller to interrupt */
                 | S1_STATUS (cvptr->status) | cvptr->spd_unit; /*   and include the Status-1 value */
        }
    }

return NO_ACTION;                                       /* no drives have attention set */
}


/* Clear the controller.

   A "hard", "timeout", or "soft" clear is performed on the indicated controller
   as specified by the "clear_type" parameter.

   In hardware, four conditions clear the 13037 controller:

     - an initial application of power
     - an assertion of the CLEAR signal by the CPU interface
     - a timeout of the command wait timer
     - a programmed Clear command

   The first two conditions, called "hard clears," are equivalent and cause a
   firmware restart with the PWRON flag set.  The 13175 interface for the HP
   1000 asserts the CLEAR signal in response to the backplane CRS signal if the
   PRESET jumper is installed in the enabled position (which is the usual case).
   The 30229B interface for the HP 3000 asserts CLEAR in response to an IORESET
   signal or a programmed Master Reset if the PRESET DISABLE jumper is installed
   in the enabled position.

   The third condition, a "timeout clear", also causes a firmware restart but
   with the PWRON flag clear.  The last condition, a "soft clear" is executed in
   the command handler and therefore returns to the Command Wait Loop instead of
   the Poll Loop.

   For a hard clear, the 13037 controller will:

     - disconnect the CPU interface
     - zero the controller RAM (no drives held, last polled unit number reset)
     - clear the clock offset
     - clear the file mask
     - issue a Controller Preset to clear all connected drives
     - enter the Poll Loop (which clears the controller status)

   For a timeout clear, the 13037 controller will:

     - disconnect the CPU interface
     - clear the hold bits of any drives held by the interface that timed out
     - clear the clock offset
     - clear the file mask
     - enter the Poll Loop (which clears the controller status)

   For a programmed "soft" clear, the 13037 controller will:

     - clear the controller status
     - issue a Controller Preset to clear all connected drives
     - enter the Command Wait Loop

   Controller Preset is a tag bus command that is sent to all drives connected
   to the controller.  Each drive will:

     - disconnect from the controller
     - clear its internal drive faults
     - clear its head and sector registers
     - clear its illegal head and sector flip-flops
     - reset its seek check, first status, drive fault, and attention status

   On a 7905 or 7906 drive, clearing the head register will change Read-Only
   status to reflect the position of the PROTECT LOWER DISC switch.

   In simulation, a hard clear occurs when an SCP RESET command is entered, or a
   programmed CLC 0 instruction or Master Clear is executed.  A timeout clear
   occurs when the command or parameter wait timer expires.  A soft clear occurs
   when a programmed Clear command is issued.

   Because the controller execution state is implemented by scheduling command
   phases for the target or controller unit, a simulated firmware restart must
   abort any in-process activation.  However, a firmware restart does not affect
   seeks in progress, so these must be allowed to continue to completion so that
   their Attention requests will be honored.


   Implementation notes:

    1. The specific 13365 controller actions on hard or soft clears are not
       documented.  Therefore, an ICD controller clear is handled as a MAC
       controller clear, except that only the current drive is preset (as an ICD
       controller manages only a single drive).

    2. Neither hard nor soft clears affect the controller flags (e.g., EOC) or
       registers (e.g., cylinder address).

    3. In simulation, an internal seek, such as an auto-seek during a Read
       command or the initial seek during a Cold Load Read command, will be
       aborted for a hard clear, whereas in hardware it would complete normally.
       This is OK, however, because an internal seek always clears the drive's
       Attention status on completion, so aborting the simulated seek is
       equivalent to an immediate seek completion.

    4. If a drive unit is disabled, it is still cleared.  A unit cannot be
       disabled while it is active, so it must be in the idle state while
       disabled.  Clearing the status keeps it consistent in case it is
       reenabled later.

    5. A soft clear does not abort commands in progress on a drive unit.
       However, a soft clear is a result of a programmed command that can only
       be issued when the prior command has completed (except for Seek and
       Recalibrate commands, which are never aborted).

    6. In simulation, a Controller Preset only resets the specified status bits,
       as the remainder of the hardware actions are not implemented.
*/

static void clear_controller (CVPTR cvptr, CNTLR_CLEAR clear_type)
{
uint32  unit_count;
UNIT   *uptr;

if (clear_type == Timeout_Clear) {                      /* if this is a timeout clear */
    cvptr->file_mask = 0;                               /*   then clear the file mask */
    idle_controller (cvptr);                            /*     and idle the controller */
    }

else {                                                  /* otherwise */
    if (clear_type == Hard_Clear) {                     /*   if this a hard clear */
        cvptr->file_mask = 0;                           /*     then clear the file mask */

        if (cvptr->type == MAC)                         /* if this is a MAC controller */
            cvptr->poll_unit = 0;                       /*   then clear the last unit polled too */

        idle_controller (cvptr);                        /* idle the controller */
        }

    else                                                /* otherwise it's a soft clear */
        cvptr->status = Normal_Completion;              /*   so clear the status explicitly */

    if (cvptr->type == MAC) {                           /* if this a MAC controller */
        uptr = cvptr->device->units;                    /*   then preset all units */
        unit_count = cvptr->device->numunits - 1;       /*     except the controller unit */
        }

    else {                                              /* otherwise preset */
        uptr = cvptr->device->units + cvptr->poll_unit; /*   only the single unit */
        unit_count = 1;                                 /*     dedicated to the controller */
        }

    while (unit_count > 0) {                            /* preset each drive in turn */
        if (uptr->PHASE != Idle_Phase                   /* if the unit is active */
          && uptr->OPCODE != Seek                       /*   but not seeking */
          && uptr->OPCODE != Recalibrate) {             /*     or recalibrating */
            sim_cancel (uptr);                          /*       then cancel the unit event */
            uptr->PHASE = Idle_Phase;                   /*         and idle it */
            }

        uptr->STATUS &= ~(S2_CPS | S2_READ_ONLY);       /* do a "Controller Preset" on the unit */

        if (uptr->flags & UNIT_PROT_U)                  /* if the (upper) heads are protected */
            uptr->STATUS |= S2_READ_ONLY;               /*   then set read-only status */

        uptr++;                                         /* point at the next unit */
        unit_count = unit_count - 1;                    /*   and count the unit just cleared */
        }
    }

return;
}



/* Disc library global utility routines */


/* Return the name of an opcode.

   A string representing the supplied controller opcode is returned to the
   caller.  If the opcode is illegal or undefined for the indicated controller,
   the string "Invalid" is returned.
*/

const char *dl_opcode_name (CNTLR_TYPE controller, CNTLR_OPCODE opcode)
{
if (controller <= LAST_CNTLR                            /* if the controller type is legal */
  && opcode <= LAST_OPCODE                              /*   and the opcode is legal */
  && cmd_props [opcode].valid [controller])             /*     and is defined for this controller, */
    return opcode_name [opcode];                        /*       then return the opcode name */
else                                                    /* otherwise the type or opcode is illegal */
    return invalid_name;                                /*   so return an error indication */
}


/* Return the name of a command result status.

   A string representing the supplied command result status is returned to the
   caller.  If the status is illegal or undefined, the string "Invalid" is
   returned.
*/

const char *dl_status_name (CNTLR_STATUS status)
{
if (status <= Drive_Attention && status_name [status])  /* if the status is legal */
    return status_name [status];                        /*   then return the status name */
else                                                    /* otherwise the status is illegal */
    return invalid_name;                                /*   so return an error indication */
}



/* Disc library global SCP support routines */


/* Attach a disc image file to a unit.

   The file specified by the supplied filename is attached to the indicated
   unit.  If the attach was successful, the heads are loaded on the drive.


   Implementation notes:

    1. The pointer to the appropriate event delay times is set in case we are
       being called during a RESTORE command (the assignment is redundant
       otherwise).
*/

t_stat dl_attach (CVPTR cvptr, UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = attach_unit (uptr, cptr);                          /* attach the unit */

if (result == SCPE_OK)                                      /* if the attach succeeded */
  result = dl_load_unload (cvptr, uptr, TRUE);              /*   then load the heads */

dl_set_timing (cvptr->device->units,                        /* reestablish */
               (cvptr->device->flags & DEV_REALTIME),       /*   the delay times */
               NULL, (void *) cvptr);                       /*     pointer(s) */

return result;                                              /* return the command result status */
}


/* Detach a disc image file from a unit.

   The heads are unloaded on the drive, and the attached file, if any, is
   detached.
*/

t_stat dl_detach (CVPTR cvptr, UNIT *uptr)
{
t_stat unload, detach;

unload = dl_load_unload (cvptr, uptr, FALSE);           /* unload the heads */

if (unload == SCPE_OK || unload == SCPE_INCOMP) {       /* if the unload succeeded */
    detach = detach_unit (uptr);                        /*   then detach the unit */

    if (detach == SCPE_OK)                              /* if the detach succeeded as well */
        return unload;                                  /*   then return the unload status */
    else                                                /* otherwise */
        return detach;                                  /*   return the detach failure status */
    }

else                                                    /* otherwise the unload failed */
    return unload;                                      /*   so return the failure status */
}


/* Load or unload the drive heads.

   In hardware, a drive's heads are loaded when a disc pack is installed and the
   RUN/STOP switch is set to RUN.  The drive reports First Status when the heads
   load to indicate that the pack has potentially changed.  Setting the switch
   to STOP unloads the heads.  When the heads are unloaded, the drive reports
   Not Ready and Drive Busy status.  In both cases, the drive reports Attention
   status to the controller.  A MAC controller will clear a drive's Attention
   status and will interrupt the CPU when the drives are polled in the Idle
   Loop.  An ICD controller also clears the drive's Attention status but will
   assert a Parallel Poll Response only when the heads unload.

   In simulation, the unit must be attached, corresponding to having a disc pack
   installed in the drive, before the heads may be unloaded or loaded.  If it
   isn't, the routine returns SCPE_UNATT.  Otherwise, the UNIT_UNLOAD flag and
   the drive status are set accordingly.

   If the (MAC) controller is idle, the routine returns SCPE_INCOMP to indicate
   that the caller must then call the controller to poll for drive attention to
   complete the command.  Otherwise, it returns SCPE_OK, and the drives will be
   polled automatically when the current command or command wait completes and
   the controller is idled.


   Implementation notes:

    1. Loading or unloading the heads clears Fault and Seek Check status.

    2. If we are called during a RESTORE command, the unit's flags are not
       changed to avoid upsetting the state that was SAVEd.

    3. Unloading an active unit does not cancel the event.  This ensures that
       the controller will not hang during a transfer but instead will see the
       unloaded unit and fail the transfer with Access_Not_Ready status.
*/

t_stat dl_load_unload (CVPTR cvptr, UNIT *uptr, t_bool load)
{
if ((uptr->flags & UNIT_ATT) == 0)                      /* the unit must be attached to [un]load */
    return SCPE_UNATT;                                  /*   so return "Unit not attached" if it is not */

else if ((sim_switches & SIM_SW_REST) == 0) {           /* if we are not being called during a RESTORE command */
    uptr->STATUS &= ~S2_CPS;                            /*   then do a "Controller Preset" on the unit */

    if (load) {                                         /* if we are loading the heads */
        uptr->flags &= ~UNIT_UNLOAD;                    /*   then clear the unload flag */
        uptr->STATUS |= S2_FIRST_STATUS;                /*     and set First Status */

        if (cvptr->type != ICD)                         /* if this is not an ICD controller */
            uptr->STATUS |= S2_ATTENTION;               /*   then set Attention status also */
        }

    else {                                              /* otherwise we are unloading the heads */
        uptr->flags |= UNIT_UNLOAD;                     /*   so set the unload flag */
        uptr->STATUS |= S2_ATTENTION;                   /*     and Attention status */
        }

    dpprintf (cvptr->device, DL_DEB_CMD, "RUN/STOP switch set to %s\n",
              (load ? "RUN" : "STOP"));

    if (cvptr->type == MAC && cvptr->state == Idle_State)   /* if this is a MAC controller, and it's idle */
        return SCPE_INCOMP;                                 /*   then the controller must be called to poll the drives */
    }                                                       /* otherwise indicate that the command is complete */

return SCPE_OK;                                         /* return normal completion status to the caller */
}


/* Set the drive model.

   This validation routine is called to set the model of the disc drive
   associated with the specified unit.  The "value" parameter indicates the
   model ID, and the unit capacity is set to the size indicated.


   Implementation notes:

    1. If the drive is changed from a 7905 or 7906, which has separate head
       protect switches, to a 7920 or 7925, which has a single protect switch,
       ensure that both protect bits are set so that all heads are protected.
*/

t_stat dl_set_model (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)                             /* if the unit is currently attached */
    return SCPE_ALATT;                                  /*   then the disc model cannot be changed */

uptr->capac = drive_props [GET_MODEL (value)].words;    /* set the capacity to the new value */

if (uptr->flags & UNIT_PROT                             /* if either protect bit is set */
  && (value == UNIT_7920 || value == UNIT_7925))        /*   and the new drive is a 7920 or 7925 */
    uptr->flags |= UNIT_PROT;                           /*     then ensure that both bits are set */

return SCPE_OK;
}


/* Set or clear the write protection status.

   This validation routine is called to set the write protection status of the
   disc drive associated with the specified unit.  The "value" parameter
   indicates whether the drive is to be protected (1) or unprotected (0).

   In hardware, the 7920 and 7925 drives have a READ ONLY switch that write-
   protects all heads in the drive, and Read-Only status directly reflects the
   position of the switch.  The switch is simulated by the SET <unit> PROTECT
   and SET <unit> UNPROTECT commands.

   The 7905 and 7906 drives have separate PROTECT UPPER DISC and PROTECT LOWER
   DISC switches that protect heads 0-1 and 2 (7905) or 2-3 (7906),
   respectively, and Read-Only status reflects the position of the switch
   associated with the head currently addressed by the drive's Head Register.
   The Head Register is loaded by the controller as part of a Seek or Cold Load
   Read command, or during an auto-seek or spare track seek, if permitted by the
   file mask.  A Controller Preset command sent to a drive clears the Head
   Register, so Read-Only status reflects the PROTECT UPPER DISC switch setting.

   The SET <unit> PROTECT=(UPPER | LOWER) and SET <unit> UNPROTECT=(UPPER |
   LOWER) simulation commands are provided to protect or unprotect the upper or
   lower heads individually.  If the option values are omitted, e.g., SET <unit>
   PROTECT, then both upper and lower heads are (un)protected.
*/

t_stat dl_set_protect (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const uint32 model = uptr->flags & UNIT_MODEL;
char gbuf [CBUFSIZE];

if (cptr == NULL)                                       /* if there are no arguments */
    if (value)                                          /*   then if setting the protection status */
        uptr->flags |= UNIT_PROT;                       /*     then protect all heads */
    else                                                /*   otherwise */
        uptr->flags &= ~UNIT_PROT;                      /*     unprotect all heads */

else if (*cptr == '\0')                                 /* otherwise if the argument is empty */
    return SCPE_MISVAL;                                 /*   then reject the command */

else if (model == UNIT_7920 || model == UNIT_7925)      /* otherwise if this is a 7920 or 7925 */
    return SCPE_ARG;                                    /*   then the heads cannot be protected separately */

else {                                                  /* otherwise a 7905/06 argument is present */
    cptr = get_glyph (cptr, gbuf, ';');                 /* get the argument */

    if (strcmp ("LOWER", gbuf) == 0)                    /* if the LOWER option was specified */
        if (value)                                      /*   then if setting the protection status */
            uptr->flags |= UNIT_PROT_L;                 /*     then set the protect lower disc flag */
        else                                            /*   otherwise */
            uptr->flags &= ~UNIT_PROT_L;                 /*    clear the protect lower disc flag */

    else if (strcmp ("UPPER", gbuf) == 0)               /* otherwise if the UPPER option was specified */
        if (value)                                      /*   then if setting the protection status */
            uptr->flags |= UNIT_PROT_U;                 /*     then set the protect upper disc flag */
        else                                            /*   otherwise */
            uptr->flags &= ~UNIT_PROT_U;                /*    clear the protect upper disc flag */

    else                                                /* otherwise some other argument was given */
        return SCPE_ARG;                                /*   so report the error */
    }

return SCPE_OK;
}


/* Show the write protection status.

   This display routine is called to show the write protection status of the
   disc drive associated with the specified unit.  The "value" and "desc"
   parameters are unused.

   The unit flags contain two bits that indicate write protection for heads 0-1
   and heads 2-n.  If both bits are clear, the drive is unprotected.  A 7905 or
   7906 drive may have one or the other or both bits set, indicating that the
   upper, lower, or both platters are protected.  A 7920 or 7925 will have both
   bits set, indicating that the entire drive is protected.
*/

t_stat dl_show_protect (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const uint32 model = uptr->flags & UNIT_MODEL;

if ((uptr->flags & UNIT_PROT) == 0)                     /* if the protection flags are clear */
    fputs ("unprotected", st);                          /*   then report the disc as unprotected */

else if (model == UNIT_7905 || model == UNIT_7906)      /* otherwise if this is a 7905/06 */
    if ((uptr->flags & UNIT_PROT) == UNIT_PROT_L)       /*   then if only the lower disc protect flag is set */
        fputs ("lower protected", st);                  /*     then report it */
    else if ((uptr->flags & UNIT_PROT) == UNIT_PROT_U)  /*   otherwise if only the upper disc protect flag is set */
        fputs ("upper protected", st);                  /*     then report it */
    else                                                /*   otherwise both flags are set */
        fputs ("lower/upper protected", st);            /*     so report them */

else                                                    /* otherwise it's a 7920/25 */
    fputs ("protected", st);                            /*   so either flag set indicates a protected disc */

return SCPE_OK;
}


/* Set or clear the diagnostic override table.

   This validation routine is called to set or clear the diagnostic override
   table associated with the specified unit.  The "value" parameter is either
   the positive maximum table entry count if an entry is to be added, or zero if
   the table is to be cleared, and "desc" is a pointer to the controller.

   If the CPU interface declares a diagnostic override table, it will specify
   the table when initializing the controller variables structure with the
   CNTLR_INIT macro.  This sets the "dop_base" pointer in the structure to point
   at the table.  If the interface does not declare a table, the pointer will be
   NULL.

   If the table is present, new entries may be added with this command:

     SET <dev> DIAG=<cylinder>;<head>;<sector>;<opcode>;<spd>;<status>

   The cylinder, head, and sector values are entered as decimal numbers, the
   opcode and status values are entered as octal numbers, and the SPD value is
   specified as any combination of the letters "S", "P", or "D".

   If a command specifies the opcode value as Request Syndrome (15) and the
   status value as Correctable Data Error (17), then four additional values must
   be specified as part of the command line above:

      ;<displacement>;<syndrome 1>;<syndrome 2>;<syndrome 3>

   The displacement value is entered as a decimal number, and the three syndrome
   values are entered as octal numbers.

   Entering SET <dev> DIAG by itself resets the current entry pointer to the
   first table entry.  Entering SET <dev> NODIAG clears the table.

   If the override table was not declared by the CPU interface, the routine
   returns "Command not allowed."  If SET NODIAG was entered, the "value"
   parameter will be -1, and the table will be cleared by storing the special
   value DL_OVEND into the first table entry.  Otherwise, if SET DIAG is
   entered, the table pointer is reset.  However, if the table is already empty,
   the routine returns "Missing value" to indicate that the table must be
   populated first.

   If a new entry is to be added, the "value" parameter will indicate the size
   of the table in entries.  If no free entry exists, the routine returns
   "Memory exhausted".  Otherwise, the supplied parameters are parsed and
   entered into the table.  An array of maximum allowed values and radixes are
   used during parsing to validate each parameter.  The non-numeric SPD
   parameter is parsed separately.  If, after parsing the first six parameters,
   the opcode and status are Request Syndrome and Correctable Data Error,
   respectively, then four more parameters are sought.  These are stored into
   the following table entry.


   Implementation notes:

    1. Each maximum array element value limits the absolute value of its
       corresponding numeric parameter.  Negative values may be specified for
       unsigned values; no error is returned, and these simply will never match
       their intended controller values.

    2. Each radix array element value is used to parse its corresponding
       parameter.  A radix of 0 is used to indicate the S/P/D parameter, which
       is parsed alphabetically rather than numerically.

    3. One table entry is always reserved for the end-of-table value, so the
       number of configurable entries is one less than the defined table size.
*/

t_stat dl_set_diag (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
typedef struct {
    t_value  max;                               /* maximum allowed value */
    uint32   radix;                             /* numeric parsing radix */
    } PARSER_PROP;

static const PARSER_PROP param [] = {
/*    maximum value  radix */
/*    -------------  ----- */
    {         822,     10  },                   /* cylinder */
    {           8,     10  },                   /* head */
    {          63,     10  },                   /* sector */
    { LAST_OPCODE,      8  },                   /* opcode */
    {           0,      0  },                   /* SPD */
    { LAST_STATUS,      8  },                   /* status */
    {         135,     10  },                   /* displacement */
    {    D16_UMAX,      8  },                   /* syndrome 1 */
    {    D16_UMAX,      8  },                   /* syndrome 2 */
    {    D16_UMAX,      8  }                    /* syndrome 3 */
    };

const CVPTR cvptr = (CVPTR) desc;
DIAG_ENTRY *entry;
uint32 pidx, params [10];
t_stat status;
char gbuf [CBUFSIZE];

if (cvptr->dop_base == NULL)                            /* if the override array is not present */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (value == 0)                                    /* otherwise if this is a NODIAG call */
    if (cptr != NULL)                                   /*   then if something follows the keyword */
        return SCPE_2MARG;                              /*     then report an error */

    else {                                              /* otherwise the command is valid */
        cvptr->dop_index = -1;                          /*   so clear the current entry pointer */
        cvptr->dop_base->cylinder = DL_OVEND;           /*     and mark the first entry as the end */
        }

else if (cptr == NULL)                                  /* otherwise if DIAG is by itself */
    if (cvptr->dop_base->cylinder == DL_OVEND)          /*   then if there are no entries in the table */
        return SCPE_MISVAL;                             /*     then one must be entered first */
    else                                                /*   otherwise */
        cvptr->dop_index = 0;                           /*     reset the current pointer to the first entry */

else if (*cptr == '\0')                                 /* otherwise if there are no parameters */
    return SCPE_MISVAL;                                 /*   then report a missing value */

else {                                                  /* otherwise at least one parameter is present */
    for (entry = cvptr->dop_base;                       /* find the */
         entry->cylinder != DL_OVEND && value > 0;      /*   last entry */
         entry++, value--);                             /*     in the current table */

    if (value <= 1)                                     /* if there's not enough room to add a new entry */
        return SCPE_MEM;                                /*   then report the error to the user */

    else {                                              /* otherwise there's at least one free entry */
        for (pidx = 0; pidx < 10; pidx++) {             /*   so assume a full set of arguments are present */
            if (*cptr == '\0')                          /* if the next argument is not there */
                return SCPE_2FARG;                      /*   then report it missing */

            if (param [pidx].radix == 0) {              /* if this is the SPD argument */
                params [pidx] = 0;                      /*   then parse it specially */

                while (*cptr != ';' && *cptr != '\0') { /* look for multiple arguments */
                    if (*cptr == 'S')                   /* if it's an S */
                        params [pidx] |= CM_SPARE;      /*   then set the spare bit */
                    else if (*cptr == 'P')              /* or if it's a P */
                        params [pidx] |= CM_PROTECTED;  /*   then set the protected bit */
                    else if (*cptr == 'D')              /* or if it's a D */
                        params [pidx] |= CM_DEFECTIVE;  /*   then set the defective bit */
                    else                                /* any other character */
                        return SCPE_ARG;                /*   results in an invalid argument error */

                    cptr++;                             /* point at the next character and continue */
                    }                                   /*   until a separator or terminator is seen */

                if (*cptr == ';')                       /* if a separator was seen */
                    cptr++;                             /*   then move past it */

                status = SCPE_OK;                       /* reassure the compiler that status is not uninitialized */
                }

            else {                                      /* otherwise parse a numeric argument */
                cptr = get_glyph (cptr, gbuf, ';');     /* get the argument */

                if (gbuf [0] == '-') {                  /* if the argument is negative */
                    gbuf [0] = ' ';                     /*   then clear the sign */
                    params [pidx] =                     /*     and negate the resulting value */
                      (uint32) NEG16 (get_uint (gbuf, param [pidx].radix,
                                                param [pidx].max, &status));
                    }

                else                                    /* otherwise the argument is unsigned */
                    params [pidx] =                     /*   so use the value as is */
                      (uint32) get_uint (gbuf, param [pidx].radix,
                                         param [pidx].max, &status);
                }

            if (status != SCPE_OK)                      /* if an error occurred */
                return status;                          /*   then return the parsing status */

            if (pidx == 5                               /* if we have parsed the status */
              && (params [3] != Request_Syndrome        /*   and this is not a Request Syndrome entry */
              || params [5] != Correctable_Data_Error)) /*     with Correctable Data Error status */
                break;                                  /*       then no more parameters are expected */
            }

        if (*cptr != '\0')                              /* if more characters are present */
            return SCPE_2MARG;                          /*   then report the excess */

        else if (pidx == 10 && value <= 2)              /* otherwise if we have syndrome values but no space */
            return SCPE_MEM;                            /*   the report that we can't store them */

        entry->cylinder = params [0];                   /* store the */
        entry->head     = params [1];                   /*   first set */
        entry->sector   = params [2];                   /*     of parameter values */
        entry->opcode   = (CNTLR_OPCODE) params [3];    /*       in the */
        entry->spd      = params [4];                   /*         first available */
        entry->status   = (CNTLR_STATUS) params [5];    /*            empty entry */

        if (pidx == 10) {                               /* if syndrome values were present */
            entry++;                                    /*   then store them in the next available entry */

            entry->spd      = params [6];               /* save the displacement */
            entry->cylinder = params [7];               /*   and the */
            entry->head     = params [8];               /*     three syndrome */
            entry->sector   = params [9];               /*       values */
            entry->opcode   = Request_Syndrome;         /* identify the entry */
            entry->status   = Correctable_Data_Error;   /*   by opcode and status */
            }

        entry++;                                        /* point at the next available entry */
        entry->cylinder = DL_OVEND;                     /*   and mark it as the end of the list */

        cvptr->dop_index = 0;                           /* reset the current pointer to the start of the list */
        }
    }

return SCPE_OK;
}


/* Show the diagnostic override table.

   This display routine is called to show the contents of the diagnostic
   override table.  The "value" parameter is either the positive maximum table
   entry count if the routine was invoked by a SHOW <dev> DIAG command, or -1 if
   the routine was invoked by a SHOW <dev> command.  The "desc" parameter is a
   pointer to the controller.

   If the override table was not declared by the CPU interface, the routine
   returns "Command not allowed."  Otherwise, if the table is empty, the routine
   prints "override disabled".  If the table is populated, then the routine
   prints "override enabled" if it was invoked as part of a general SHOW for the
   device, or it prints the individual table entries if it was invoked as SHOW
   <dev> DIAG.

   Entries are printed in tabular form with the columns corresponding to the SET
   DIAG parameters, except that the opcode and status fields are decoded.  A
   Request Syndrome command entry with Correctable Data Error status is followed
   by an indented second line containing the displacement and syndrome values.
   Numeric values are printed in the same radix as is used to enter them.


   Implementation notes:

    1. The Request Syndrome displacement parameter is a 16-bit signed value
       contained in a 32-bit unsigned array element.  To print it properly, we
       convert the latter to a 16-bit signed value and then sign-extend to "int"
       size for fprintf.

    2. The explicit use of "const CNTLR_VARS *" is necessary to declare a
       pointer to a constant structure.  Using "const CVPTR" declares a constant
       pointer instead.
*/

t_stat dl_show_diag (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const CNTLR_VARS *cvptr = (const CNTLR_VARS *) desc;    /* the controller pointer is supplied */
DIAG_ENTRY *entry;

if (cvptr->dop_base == NULL)                            /* if the table isn't defined */
    return SCPE_NOFNC;                                  /*   then the command is illegal */

else if (cvptr->dop_index < 0) {                        /* otherwise if overrides are currently disabled */
    fputs ("override disabled", st);                    /*   then report it */

    if (value > 0)                                      /* if we were invoked by a SHOW DIAG command */
        fputc ('\n', st);                               /*   then we must add the line terminator */
    }

else if (value < 0)                                     /* otherwise if we were invoked by a SHOW <dev> command */
    fputs ("override enabled", st);                     /*   then print the table status instead of the details */

else for (entry = cvptr->dop_base;                      /* otherwise print each table entry */
          entry->cylinder != DL_OVEND && value > 0;     /*   until the end-of-table marker or count exhaustion */
          entry++, value--) {
    fprintf (st, "%3d  %1d  %2d  %*s  %c%c%c  %*s\n",   /* print the entry */
             entry->cylinder, entry->head, entry->sector,
             - OPCODE_LENGTH, dl_opcode_name (cvptr->type, entry->opcode),
             (entry->spd & CM_SPARE     ? 'S' : ' '),
             (entry->spd & CM_PROTECTED ? 'P' : ' '),
             (entry->spd & CM_DEFECTIVE ? 'D' : ' '),
             - STATUS_LENGTH, dl_status_name (entry->status));

    if (entry->opcode == Request_Syndrome               /* if the current entry is a syndrome request */
      && entry->status == Correctable_Data_Error) {     /*   for a correctable data error */
        entry++;                                        /*     then the next entry contains the values */
        value = value - 1;                              /* drop the entry count to account for it */

        fprintf (st, "            %3d  %06o  %06o  %06o\n", /* print the displacement and syndrome values */
                 (int) INT16 (entry->spd),
                 entry->cylinder, entry->head, entry->sector);
        }
    }

return SCPE_OK;
}


/* Set the controller timing mode.

   This validation routine is called to set the timing mode for the disc
   subsystem.  As this is an extended MTAB call, the "uptr" parameter points to
   the unit array of the device.  The "value" parameter is set non-zero to use
   realistic timing and 0 to use fast timing.  For a MAC controller, the "desc"
   parameter is a pointer to the controller.  For ICD controllers, the "desc"
   parameter is a pointer to the first element of the controller array.  There
   must be one controller for each unit defined by the device associated with
   the controllers.  The "cptr" parameter is not used.

   If fast timing is selected, the controller's timing pointer is set to the
   fast timing pointer supplied by the interface when the controller was
   initialized.  If real timing is selected, the table of real times is searched
   for an entry whose controller type matches the supplied controller.  MAC and
   ICD controllers support several drive models with identical timing
   characteristics.  However, CS/80 controllers allow mixing drives with
   different timing on a single CPU interface.  For these, the drive type must
   match as well.  If a match is found, the controller's timing pointer is set
   to the associated real-time entry.  Otherwise, the routine returns SCPE_IERR.

   Non-MAC controllers are dedicated per-drive, so "desc" points not at a single
   controller instance but at an array of controller instances.  The timing mode
   is common to every controller in the array.
*/

t_stat dl_set_timing (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
CVPTR  cvptr = (CVPTR) desc;                            /* the controller pointer is supplied */
const  DELAY_PROPS *dpptr;
DRIVE_TYPE model;
uint32 delay, cntlr_count;

if (cvptr->type == MAC)                                 /* if this is a MAC controller */
    cntlr_count = 1;                                    /*   then there is one controller for all units */
else                                                    /* otherwise */
    cntlr_count = cvptr->device->numunits;              /*   there is one controller per unit */

while (cntlr_count--) {                                 /* set each controller's timing mode */
    if (value) {                                        /* if realistic timing is requested */
        model = GET_MODEL (uptr->flags);                /*   then get the drive model from the current unit */
        dpptr = real_times;                             /*     and reset the real-time delay table pointer */

        for (delay = 0; delay < DELAY_COUNT; delay++)   /* search for the correct set of times */
            if (dpptr->type == cvptr->type              /* if the controller types match */
              && (dpptr->drive == HP_All                /*   and all drive times are the same */
              || dpptr->drive == model)) {              /*     or the drive types match as well */
                cvptr->dlyptr = dpptr;                  /*       then use this set of times */
                break;
                }
            else                                        /* otherwise */
                dpptr++;                                /*   point at the next array element */

        if (delay == DELAY_COUNT)                       /* if the model was not found in the table */
            return SCPE_IERR;                           /*   then report an internal (impossible) error */
        else                                            /* otherwise */
            cvptr->device->flags |= DEV_REALTIME;       /*   set the real time flag */
        }

    else {                                              /* otherwise fast timing is requested */
        cvptr->device->flags &= ~DEV_REALTIME;          /*   so clear the real time flag */
        cvptr->dlyptr = cvptr->fastptr;                 /*     and set the delays to the FASTTIME settings */
        }

    cvptr++;                                            /* point at the next controller */
    uptr++;                                             /*   and corresponding unit for non-MAC controllers */
    }

return SCPE_OK;
}


/* Show the controller timing mode

   This display routine is called to show the timing mode for the disc
   subsystem.  The "value" parameter is unused; the "desc" parameter is a
   pointer to the controller.


   Implementation notes:

    1. The explicit use of "const CNTLR_VARS *" is necessary to declare a
       pointer to a constant structure.  Using "const CVPTR" declares a constant
       pointer instead.
*/

t_stat dl_show_timing (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const CNTLR_VARS *cvptr = (const CNTLR_VARS *) desc;    /* the controller pointer is supplied */

if (cvptr->device->flags & DEV_REALTIME)                /* if the real time flag is set */
    fputs ("realistic timing", st);                     /*   then we're using realistic timing */
else                                                    /* otherwise */
    fputs ("fast timing", st);                          /*   we're using optimized timing */

return SCPE_OK;
}



/* Disc library local controller routines */


/* Start or stop the command/parameter wait timer.

   A MAC controller uses a 1.74 second timer to ensure that it does not wait
   forever for a non-responding disc drive or CPU interface.  In simulation, MAC
   interfaces supply an additional controller unit that is activated when the
   command or parameter wait timer is started and cancelled when the timer is
   stopped.

   ICD interfaces do not use the wait timer or supply an additional unit.


   Implementation notes:

    1. Absolute activation is used because the timer is restarted between
       parameter word transfers.
*/

static void wait_timer (CVPTR cvptr, FLIP_FLOP action)
{
if (cvptr->type == MAC)                                 /* if this a MAC controller */
    if (action == SET)                                  /*  then if the timer is to be set */
        sim_activate_abs (CNTLR_UPTR, CNTLR_TIMEOUT);   /*    then activate the controller unit */

    else {                                              /*  otherwise the timer is to be stopped */
        sim_cancel (CNTLR_UPTR);                        /*    so cancel the unit */
        CNTLR_UPTR->PHASE = Idle_Phase;                 /*      and idle the controller unit */
        }
return;
}


/* Idle the controller.

   The command wait timer is turned off, the status is reset, and the controller
   is returned to the idle state (Poll Loop).
*/

static void idle_controller (CVPTR cvptr)
{
wait_timer (cvptr, CLEAR);                              /* stop the command wait timer */

cvptr->status = Normal_Completion;                      /* the Poll Loop clears the status */
cvptr->state  = Idle_State;                             /* idle the controller */
return;
}


/* End the current command.

   The currently executing command is completed with the supplied status.  If
   the command completed normally, and it returns to the Poll Loop, the
   controller is idled, and the wait timer is cancelled.  Otherwise, the
   controller enters the Wait Loop, and the wait timer is started.  If the
   command had accessed a drive unit, the unit is idled.  Also, for a MAC
   controller, the controller unit is idled as well.
*/

static void end_command (CVPTR cvptr, UNIT *uptr, CNTLR_STATUS status)
{
cvptr->status = status;                                 /* set the command result status */

if (status == Normal_Completion                         /* if the command completed normally */
  && cmd_props [cvptr->opcode].idle_at_end) {           /*   and this command idles the controller */
    cvptr->state = Idle_State;                          /*     then set idle status */
    wait_timer (cvptr, CLEAR);                          /*       and stop the command wait timer */
    }

else {                                                  /* otherwise */
    cvptr->state = Wait_State;                          /*   the controller waits for a new command */
    wait_timer (cvptr, SET);                            /*     so start the command wait timer */

    if (uptr)                                           /* if the command accessed a drive */
        uptr->PHASE = Idle_Phase;                       /*   then idle it */

    if (cvptr->type == MAC)                             /* if this is a MAC controller */
        CNTLR_UPTR->PHASE = Idle_Phase;                 /*   then idle the controller unit as well */
    }

if (cmd_props [cvptr->opcode].transfer_size > 0)
    dpprintf (cvptr->device, DL_DEB_CMD, (cvptr->opcode == Initialize
                                            ? "Unit %u Initialize %s for %u words (%u sector%s)\n"
                                            : "Unit %u %s for %u words (%u sector%s)\n"),
              CM_UNIT (cvptr->spd_unit),
              (cvptr->opcode == Initialize
                 ? fmt_bitset (cvptr->spd_unit, initialize_format)
                 : opcode_name [cvptr->opcode]),
              cvptr->count,
              cvptr->count / cmd_props [cvptr->opcode].transfer_size + (cvptr->length > 0),
              (cvptr->count <= cmd_props [cvptr->opcode].transfer_size ? "" : "s"));

dpprintf (cvptr->device, DL_DEB_INCO, "Unit %u %s command completed with %s status\n",
          CM_UNIT (cvptr->spd_unit), opcode_name [cvptr->opcode],
          dl_status_name (cvptr->status));

return;
}


/* Start a read operation on the current sector.

   This routine is called at the end of the rotate phase to begin a read
   operation.  The current sector given by the controller address is read from
   the disc image file into the sector buffer in preparation for data transfer
   to the CPU.  If the end of the track had been reached, and the file mask
   permits, an auto-seek is scheduled instead to allow the read to continue.
   The routine returns TRUE if the data is ready to be transferred and FALSE if
   it is not (due to command completion, an error, or an auto-seek that must
   complete first).

   On entry, the end-of-data flag is checked.  If it is set, the current read
   command is completed.  Otherwise, the buffer data offset and verify options
   are set up.  For a Read Full Sector, the sync word is set from the controller
   type, and dummy cylinder and head-sector words are generated from the current
   location (as would be the case in the absence of track sparing).

   The image file is positioned to the correct sector in preparation for
   reading.  If the positioning requires a permitted seek, it is scheduled, and
   the routine returns with the Seek_Phase set to wait for seek completion
   before resuming the read (when the seek completes, the service routine will
   be entered, and we will be called again; this time, the end-of-cylinder flag
   will be clear and positioning will succeed).  If positioning resulted in an
   error, the current read is terminated with the error status set.

   If positioning succeeded within the same track, the sector image is read into
   the buffer at an offset determined by the operation (Read Full Sector leaves
   room at the start of the buffer for the sector header).  If the image read
   failed with a host file system error, it is reported to the simulation
   console and the read ends with an Uncorrectable Data Error.  If it succeeded
   but did not return a full sector, the remainder of the buffer is padded with
   zeros.

   If the image was read correctly, the operation phase is set for the data
   transfer, the index of the first word to transfer is set, and the routine
   returns TRUE to begin the data transfer.


   Implementation notes:

    1. This routine changes the unit phase state as follows:

         Rotate_Phase => Idle_Phase if EOD or error (returns FALSE)
         Rotate_Phase => Seek_Phase if auto-seek (returns FALSE)
         Rotate_Phase => Data_Phase otherwise (returns TRUE)

    2. The position_sector routine sets up the data phase if it succeeds or the
       seek phase if a seek is required.
*/

static t_bool start_read (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags)
{
uint32 count, offset;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OPCODE;

if (flags & EOD) {                                      /* if the end of data is indicated */
    end_command (cvptr, uptr, Normal_Completion);       /*   then complete the command */
    return FALSE;                                       /*     and end the current operation */
    }

if (opcode == Read_Full_Sector) {                       /* if we are starting a Read Full Sector command */
    if (cvptr->type == MAC)                             /*   then if this is a MAC controller */
        cvptr->buffer [0] = 0100376;                    /*     indicate that ECC support is valid */
    else                                                /*   otherwise */
        cvptr->buffer [0] = 0100377;                    /*     indicate that ECC support is not available */

    set_address (cvptr, 1);                             /* set the current address into buffer words 1-2 */
    offset = 3;                                         /*   and start the data after the header */
    }

else                                                    /* otherwise it's a normal read command */
    offset = 0;                                         /*   so data starts at the beginning of the buffer */

if (position_sector (cvptr, uptr) == FALSE)             /* position the sector; if it was not */
    return FALSE;                                       /*   then a seek is in progress or an error occurred */

dpprintf (cvptr->device, DL_DEB_INCO, "Unit %d %s from cylinder %u head %u sector %u\n",
          (int32) (uptr - cvptr->device->units), opcode_name [opcode],
          uptr->CYL, cvptr->head, cvptr->sector);

count = sim_fread (cvptr->buffer + offset,              /* read the sector from the image */
                   sizeof (DL_BUFFER),                  /*   into the sector buffer */
                   WORDS_PER_SECTOR, uptr->fileref);

if (ferror (uptr->fileref)) {                           /* if a host file system error occurred */
    io_error (cvptr, uptr);                             /*   then report it to the simulation console */
    return FALSE;                                       /*     and terminate with Uncorrectable Data Error status */
    }

cvptr->length = cmd_props [opcode].transfer_size;       /* set the appropriate transfer length */
cvptr->index = 0;                                       /*   and reset the data index */

for (count = count + offset; count < cvptr->length; count++)    /* pad the sector as needed */
    cvptr->buffer [count] = 0;                                  /*   e.g., if reading from a new file */

return TRUE;                                            /* the read was successfully started */
}


/* Finish a read operation on the current sector.

   This routine is called at the end of the intersector phase to finish a read
   operation.  Command termination conditions are checked, and the next sector
   is addressed in preparation for the read to continue.

   On entry, the diagnostic override status is checked.  If it is set, then the
   read is terminated with the indicated status.  Otherwise, the data overrun
   flag is checked.  If it is set, the read is terminated with an error.
   Otherwise, the next sector is addressed.

   If the end-of-data flag is set, the current read is completed.  Otherwise,the
   rotate phase is set up in preparation for the next sector read.


   Implementation notes:

    1. This routine changes the unit phase state as follows:

         Intersector_Phase => Idle_Phase if EOD or error
         Intersector_Phase => Rotate_Phase otherwise

    2. The HP 1000 CPU indicates the end of a read data transfer to an ICD
       controller by untalking the drive.  The untalk is done by the driver as
       soon as the DCPC completion interrupt is processed.  However, the time
       from the final DCPC transfer through driver entry to the point where the
       untalk is asserted on the bus varies from 80 instructions (RTE-6/VM with
       OS microcode and the buffer in the system map) to 152 instructions
       (RTE-IVB with the buffer in the user map).  The untalk must occur before
       the start of the next sector, or the drive will begin the data transfer.

       Normally, this is not a problem, as the driver clears the FIFO of any
       received data after DCPC completion.  However, if the read terminates
       after the last sector of a track, and accessing the next sector would
       require an intervening seek, and the file mask disables auto-seeking or
       an enabled seek would move the positioner beyond the drive limits, then
       the controller will indicate an End of Cylinder error if the untalk does
       not arrive before the seek is initiated.

       The RTE driver (DVA32) and various utilities that manage the disc
       directly (e.g., SWTCH) do not appear to account for these bogus errors,
       so the ICD controller hardware must avoid them in some unknown manner.
       We work around the issue by extending the intersector delay to allow time
       for a potential untalk whenever the next access would otherwise fail.

       Note that this issue does not occur with writes because DCPC completion
       asserts EOI concurrently with the final data byte to terminate the
       command explicitly.

       Note also that the delay is a fixed number of instructions, regardless of
       timing mode, to ensure that the CPU makes it through the driver code to
       output the untalk command.
*/

static void end_read (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags)
{
uint32 bound;

if (cvptr->status != Normal_Completion)                 /* if a diagnostic override is present */
    end_command (cvptr, uptr, cvptr->status);           /*   then report the indicated status */

else if (flags & OVRUN)                                 /* otherwise if a read overrun occurred */
    end_command (cvptr, uptr, Data_Overrun);            /*   then terminate the command with an error */

else {                                                  /* otherwise the read succeeded */
    next_sector (cvptr, uptr);                          /*   so address the next sector */

    if (flags & EOD)                                    /* if the end of data is indicated */
        end_command (cvptr, uptr, Normal_Completion);   /*   then complete the command */

    else {                                              /* otherwise reading continues */
        uptr->PHASE = Rotate_Phase;                     /*   so set up the unit for the rotate phase */
        uptr->wait = cvptr->dlyptr->intersector_gap;    /*     with a delay for the intersector time */

        if (cvptr->eoc == SET && cvptr->type == ICD) {      /* if a seek will be required on an ICD controller */
            if ((cvptr->file_mask & CM_AUTO_SEEK_EN) == 0)  /*   then if auto-seek is disabled */
                bound = cvptr->cylinder;                    /*     then the bound is the current cylinder */
            else if (cvptr->file_mask & CM_DECR_SEEK)       /*   otherwise if a decremental seek is enabled */
                bound = 0;                                  /*     then the bound is cylinder 0 */
            else                                            /*   otherwise the enabled bound is the last cylinder */
                bound = drive_props [GET_MODEL (uptr->flags)].cylinders - 1;

            if (cvptr->cylinder == bound)               /* if the positioner is already at the bound */
                uptr->wait = UNTALK_DELAY;              /*   then the seek will fail; delay to allow CPU to untalk */
            }
        }
    }

return;
}


/* Start a write operation on the current sector.

   This routine is called at the end of the rotate phase to begin a write
   operation.  The current sector indicated by the controller address is
   positioned for writing from the sector buffer to the disc image file after
   data transfer from the CPU.  If the end of the track had been reached, and
   the file mask permits, an auto-seek is scheduled instead to allow the write
   to continue.   The routine returns TRUE if the data is ready to be
   transferred and FALSE if it is not (due to an error or an auto-seek that must
   complete first).

   On entry, if writing is not permitted, or formatting is required but not
   enabled, the command is terminated with an error.  Otherwise, the disc image
   file is positioned to the correct sector in preparation for writing.

   If the positioning requires a permitted seek, it is scheduled, and the
   routine returns with the Seek_Phase set to wait for seek completion before
   resuming the write (when the seek completes, the service routine will be
   entered, and we will be called again; this time, the end-of-cylinder flag
   will be clear and positioning will succeed).  If positioning resulted in an
   error, the current write is terminated with the error status set.

   If positioning succeeded within the same track, the operation phase is set
   for the data transfer, the index of the first word to transfer is set, and
   the routine returns TRUE to begin the data transfer.


   Implementation notes:

    1. This routine changes the unit phase state as follows:

         Rotate_Phase => Idle_Phase if write protected or error (returns FALSE)
         Rotate_Phase => Seek_Phase if auto-seek (returns FALSE)
         Rotate_Phase => Data_Phase otherwise (returns TRUE)

    2. The position_sector routine sets up the data phase if it succeeds or the
       seek phase if a seek is required.
*/

static t_bool start_write (CVPTR cvptr, UNIT *uptr)
{
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OPCODE;

if (opcode == Write                                     /* if this is a Write command */
  && cvptr->spd_unit & CM_PROTECTED                     /*   and the track is protected */
  && (uptr->flags & UNIT_FMT) == 0)                     /*     but the FORMAT switch is not set */
    end_command (cvptr, uptr, Protected_Track);         /*       then fail with a protection error */

else if (uptr->STATUS & S2_READ_ONLY                    /* otherwise if the unit is write protected */
  || opcode != Write && (uptr->flags & UNIT_FMT) == 0)  /*   or the FORMAT switch must be set but is not */
    end_command (cvptr, uptr, Status_2_Error);          /*     then fail with a status error */

else if (position_sector (cvptr, uptr) == TRUE) {       /* otherwise if positioning the sector succeeded */
    cvptr->length = cmd_props [opcode].transfer_size;   /*   then set the appropriate transfer length */
    cvptr->index = 0;                                   /*     and reset the data index */

    dpprintf (cvptr->device, DL_DEB_INCO, "Unit %d %s to cylinder %u head %u sector %u\n",
              (int32) (uptr - cvptr->device->units), opcode_name [opcode],
              uptr->CYL, cvptr->head, cvptr->sector);

    return TRUE;                                        /* the write was successfully started */
    }

return FALSE;                                           /* otherwise an error occurred or a seek is required */
}


/* Finish a write operation on the current sector.

   This routine is called at the end of the intersector phase to finish a write
   operation.  The current sector is written from the sector buffer to the disc
   image file at the current file position.  The next sector address is then
   updated to allow writing to continue.

   On entry, the drive is checked to ensure that it is ready for the write.
   Then the sector buffer is padded appropriately if a full sector of data was
   not transferred.  The buffer is written to the disc image file at the
   position corresponding to the controller address as set when the sector was
   started.  The write begins at a buffer offset determined by the command (a
   Write Full Sector has three header words at the start of the buffer that are
   not written to the disc image).

   If the image write failed with a host file system error, it is reported to
   the simulation console and the write ends with an Uncorrectable Data Error.
   If it succeeded, the diagnostic override status is checked.  If it is set,
   then the write is terminated with the indicated status.  Otherwise, the data
   overrun flag is checked.  If it is set, the write is terminated with an
   error.  Otherwise, the next sector is addressed.  If the end-of-data flag is
   set, the current write is completed.  Otherwise,the rotate phase is set up in
   preparation for the next sector write.


   Implementation notes:

    1. This routine changes the unit phase state as follows:

         Intersector_Phase => Idle_Phase if EOD or error
         Intersector_Phase => Rotate_Phase otherwise

    2. A partial sector is filled either with octal 177777 words (ICD) or copies
       of the last word (MAC), per page 7-10 of the ICD/MAC Disc Diagnostic
       manual.
*/

static void end_write (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags)
{
uint32 count;
DL_BUFFER pad;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OPCODE;
const uint32 offset = (opcode == Write_Full_Sector ? 3 : 0);

if (uptr->flags & UNIT_UNLOAD) {                        /* if the drive is not ready */
    end_command (cvptr, uptr, Access_Not_Ready);        /*   then terminate the command */
    return;                                             /*     with an access error */
    }

if (cvptr->index < WORDS_PER_SECTOR + offset) {         /* if a partial sector was transferred */
    if (cvptr->type == ICD)                             /*   then an ICD controller */
        pad = D16_UMAX;                                 /*     pads the sector with -1 words */
    else                                                /*   whereas a MAC controller */
        pad = cvptr->buffer [cvptr->index - 1];         /*     pads with the last word written */

    for (count = cvptr->index; count < WORDS_PER_SECTOR + offset; count++)
        cvptr->buffer [count] = pad;                    /* pad the sector buffer as needed */
    }

sim_fwrite (cvptr->buffer + offset, sizeof (DL_BUFFER), /* write the sector to the file */
            WORDS_PER_SECTOR, uptr->fileref);

if (ferror (uptr->fileref))                             /* if a host file system error occurred, then report it */
    io_error (cvptr, uptr);                             /*    and terminate with Uncorrectable Data Error status */

else if (cvptr->status != Normal_Completion)            /* otherwise if a diagnostic override is present */
    end_command (cvptr, uptr, cvptr->status);           /*   then report the indicated status */

else if (flags & OVRUN)                                 /* otherwise if a write overrun occurred */
    end_command (cvptr, uptr, Data_Overrun);            /*   then terminate the command with an error */

else {                                                  /* otherwise the write succeeded */
    next_sector (cvptr, uptr);                          /*   so address the next sector */

    if (flags & EOD)                                    /* if the end of data is indicated */
        end_command (cvptr, uptr, Normal_Completion);   /*   then complete the command */

    else {                                              /* otherwise writing continues */
        uptr->PHASE = Rotate_Phase;                     /*   so set up the unit for the rotate phase */
        uptr->wait = cvptr->dlyptr->intersector_gap;    /*     with a delay for the intersector time */
        }
    }

return;
}


/* Position the disc image file at the current sector.

   The image file is positioned at the byte address corresponding to the drive's
   current cylinder and the controller's current head and sector addresses.
   Positioning may involve an auto-seek if a prior read or write addressed the
   final sector of a cylinder.  If a seek is initiated or an error is detected,
   the routine returns FALSE to indicate that the positioning was not performed.
   If the file was positioned, the routine returns TRUE.

   On entry, the diagnostic override status is checked.  If it is set to
   anything other than a data error, then positioning is terminated with the
   indicated status to simulate an address verification failure.  Otherwise, if
   the controller's end-of-cylinder flag is set, a prior read or write addressed
   the final sector in the current cylinder.  If the file mask does not permit
   auto-seeking, the command is terminated with an End of Cylinder error.
   Otherwise, the cylinder is incremented or decremented as directed by the file
   mask, and a seek to the new cylinder is started.

   If the increment or decrement resulted in an out-of-bounds value, the seek
   will return Seek Check status, and the command is terminated with an error.
   Otherwise, the seek is legal, and the routine returns with the seek phase set
   to wait for seek completion before resuming the current read or write.  When
   the seek completes, the service routine will be entered, and we will be
   called again; this time, the end-of-cylinder flag will be clear and the read
   or write will continue on the new cylinder.

   If the EOC flag was not set, the drive's position is checked against the
   controller's position if address verification is requested.  If they are
   different, as may occur with an Address Record command that specified a
   different location than the last Seek command, a seek is started to the
   correct cylinder, and the routine returns with the unit set to the seek phase
   to wait for seek completion as above.

   If the drive and controller positions agree or address verification is not
   requested, the CHS addresses are validated against the drive limits.  If they
   are invalid, Seek Check status is set, and the command is terminated with an
   error.

   If the addresses are valid, the drive is checked to ensure that it is ready
   for positioning.  If it is, the file is positioned to a byte offset in the
   image file that is calculated from the CHS address.  The data phase is set up
   to begin the data transfer, and the routine returns TRUE to indicate that the
   file position is set.


   Implementation notes:

    1. The ICD controller returns an End of Cylinder error if an auto-seek
       results in a position beyond the drive limits.  The MAC controller
       returns a Status-2 error.  Both controllers set the Seek Check bit in the
       drive status word.
*/

static t_bool position_sector (CVPTR cvptr, UNIT *uptr)
{
const DRIVE_TYPE model = GET_MODEL (uptr->flags);           /* get the drive model */

if (cvptr->status != Normal_Completion                      /* if a diagnostic override is present */
  && cvptr->status != Uncorrectable_Data_Error              /*   and it's not */
  && cvptr->status != Correctable_Data_Error)               /*     a data error */
    end_command (cvptr, uptr, cvptr->status);               /*       then report it */

else if (cvptr->eoc == SET)                                     /* otherwise if we are at the end of a cylinder */
    if (cvptr->file_mask & CM_AUTO_SEEK_EN) {                   /*   then if an auto-seek is allowed */
        if (cvptr->file_mask & CM_DECR_SEEK)                    /*     then if a decremental seek is requested */
            cvptr->cylinder = cvptr->cylinder - 1 & D16_MASK;   /*       then decrease the address with wraparound */
        else                                                    /*     otherwise an incremental seek is requested */
            cvptr->cylinder = cvptr->cylinder + 1 & D16_MASK;   /*       so increase the address with wraparound */

        start_seek (cvptr, uptr);                               /* start the auto-seek */

        dpprintf (cvptr->device, DL_DEB_INCO, "Unit %d %s%s autoseek to cylinder %u head %u sector %u\n",
                  (int32) (uptr - cvptr->device->units), opcode_name [uptr->OPCODE],
                  (uptr->STATUS & S2_SEEK_CHECK ? " seek check on" : ""),
                  cvptr->cylinder, cvptr->head, cvptr->sector);

        if (uptr->STATUS & S2_SEEK_CHECK)                   /* if a seek check occurred */
            if (cvptr->type == ICD)                         /*   then if this is an ICD controller */
                end_command (cvptr, uptr, End_of_Cylinder); /*     then report it as an End of Cylinder error */
            else                                            /*   otherwise */
                end_command (cvptr, uptr, Status_2_Error);  /*     report it as a Status-2 error */
        }

    else                                                    /* otherwise the file mask does not permit an auto-seek */
        end_command (cvptr, uptr, End_of_Cylinder);         /*   so terminate with an EOC error */

else if (cvptr->verify                                      /* if address verification is enabled */
  && (uint32) uptr->CYL != cvptr->cylinder) {               /*   and the positioner is on the wrong cylinder */
    start_seek (cvptr, uptr);                               /*     then start a seek to the correct cylinder */

    dpprintf (cvptr->device, DL_DEB_INCO, "Unit %d %s%s reseek to cylinder %u head %u sector %u\n",
              (int32) (uptr - cvptr->device->units), opcode_name [uptr->OPCODE],
              (uptr->STATUS & S2_SEEK_CHECK ? " seek check on" : ""),
              cvptr->cylinder, cvptr->head, cvptr->sector);

    if (uptr->STATUS & S2_SEEK_CHECK)                       /* if a seek check occurred */
        end_command (cvptr, uptr, Status_2_Error);          /*   then report a Status-2 error */
    }

else if (((uint32) uptr->CYL >= drive_props [model].cylinders)  /* otherwise the heads are positioned correctly */
  || (cvptr->head >= drive_props [model].heads)                 /*   but if the cylinder */
  || (cvptr->sector >= drive_props [model].sectors)) {          /*     or head or sector is out of bounds */
    uptr->STATUS |= S2_SEEK_CHECK;                              /*       then set Seek Check status */
    end_command (cvptr, uptr, Status_2_Error);                  /*         and terminate with an error */
    }

else if (uptr->flags & UNIT_UNLOAD)                     /* otherwise if the drive is not ready for positioning */
    end_command (cvptr, uptr, Access_Not_Ready);        /*   then terminate with an access error */

else {                                                  /* otherwise we are ready to move the heads */
    set_file_pos (cvptr, uptr, model);                  /*   so calculate the new position */

    sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);     /* set the image file position */

    uptr->PHASE = Data_Phase;                           /* set up the data transfer phase */

    if (cvptr->device->flags & DEV_REALTIME)            /* if the real time mode is enabled */
        uptr->wait = cvptr->dlyptr->data_xfer           /*   then base the delay on the sector preamble size */
                       * cmd_props [uptr->OPCODE].preamble_size;
    else                                                /* otherwise */
        uptr->wait = cvptr->dlyptr->data_xfer;          /*   start the transfer with a nominal delay */

    return TRUE;                                        /* report that positioning was accomplished */
    }

return FALSE;                                           /* positioning failed or was deferred */
}


/* Address the next sector.

   This routine is called after a sector has been successfully read or written
   in preparation for continuing the transfer.  It is also called after the
   Request Syndrome command returns the correction status for a sector in error.

   The controller's CHS address is incremented to point at the next sector.  If
   the next sector number is valid, the routine returns.  Otherwise, the sector
   number is reset to sector 0 and the address verification state is reset to
   enable it for a Read_Without_Verify command.  If the file mask is set for
   cylinder mode, the head is incremented, and if the new head number is valid,
   the routine returns.  If the head number is invalid, it is reset to head 0,
   and the end-of-cylinder flag is set.  The EOC flag is also set if the file
   mask is set for surface mode.

   The new cylinder address is not set here, because cylinder validation must
   only occur when the next sector is actually accessed.  Otherwise, reading or
   writing the last sector on a track or cylinder with auto-seek disabled would
   cause an End of Cylinder error, even if the transfer ended with that sector.
   Instead, we set the EOC flag to indicate that a cylinder update is pending.

   As a result of this deferred update method, the state of the EOC flag must be
   considered when returning the disc address to the CPU.
*/

static void next_sector (CVPTR cvptr, UNIT *uptr)
{
const DRIVE_TYPE model = GET_MODEL (uptr->flags);       /* get the disc model */

cvptr->sector = cvptr->sector + 1;                      /* increment the sector number */

if (cvptr->sector < drive_props [model].sectors)        /* if we at not the end of the track */
    return;                                             /*   then the next sector value is OK */

cvptr->sector = 0;                                          /* otherwise wrap the sector number */
cvptr->verify = cmd_props [uptr->OPCODE].verify_address;    /*   and set the address verification flag */

if (cvptr->file_mask & CM_CYL_MODE) {                   /* if the controller is in cylinder mode */
    cvptr->head = cvptr->head + 1;                      /*   then increment the head */

    if (cvptr->head < drive_props [model].heads)        /* if we are not at the end of the cylinder */
        return;                                         /*   then the next head value is OK */

    cvptr->head = 0;                                    /* otherwise wrap the head number */
    }

cvptr->eoc = SET;                                       /* set the end-of-cylinder flag */
return;                                                 /*   to indicate that an update is required */
}


/* Start a seek.

   A seek is initiated on the indicated unit if the drive is ready and the
   cylinder, head, and sector values in the controller are valid for the current
   drive model.  The routine returns TRUE if the unit is seeking and FALSE if
   the seek failed to start.

   If the drive is not ready, the seek fails immediately with a Status-2 error.
   If the drive is already seeking, Seek Check status will occur, and the
   routine will return TRUE to allow the current seek to complete normally.

   Otherwise, a seek is initiated to cylinder 0 if the current command is
   Recalibrate or to the cylinder value stored in the controller if it is not.
   EOC is reset for a seek but not for recalibrate, so that a reseek will return
   to the same location as was current when the recalibration was done.

   If the controller cylinder is beyond the drive's limit, Seek Check status is
   set in the unit, and the heads are not moved.  Otherwise, the relative
   cylinder position change is calculated, and the heads are moved to the new
   position.

   If the controller head or sector is beyond the drive's limit, Seek Check
   status is set in the unit.  Otherwise, Seek Check status is cleared.

   In hardware, the controller issues tag bus SEEK and ADR (address record)
   commands to the drive to load the drive's cylinder, head, and sector
   registers.  On the 7905 and 7906 drives, loading the head register
   establishes the drive's Read-Only status in conjunction with the PROTECT
   UPPER/LOWER DISC switch settings.

   A seek check for either the cylinder, head, or sector terminates the current
   command for an ICD controller.  For a MAC controller, the seek check is noted
   in the drive status, but processing will continue until the drive sets
   Attention status.

   Finally, the unit is set to the seek phase, and the scheduling delay is
   calculated by the distance the heads traversed (in real time mode), or
   it is set to a fixed delay (in fast time mode).


   Implementation notes:

    1. For ICD drives, a seek check will terminate the command immediately with
       a Status-2 error.  A seek-in-progress seek check cannot occur on an ICD
       drive, however, because the second seek command will not be started until
       the first seek completes.

    2. In hardware, a seek to the current location will set Drive Busy status
       for 1.3 milliseconds (the head settling time).  In simulation, disc
       service is scheduled as though a one-cylinder seek was requested.

    3. The head register contents does not affect Read-Only status on the 7920
       or 7925, which is established solely by the switch setting.  However, we
       set the drive status here anyway as a convenience.
*/

static t_bool start_seek (CVPTR cvptr, UNIT *uptr)
{
int32 delta;
uint32 target_cylinder;
const DRIVE_TYPE model = GET_MODEL (uptr->flags);       /* get the drive model */

if (uptr->flags & UNIT_UNLOAD)                          /* if the heads are unloaded */
    return FALSE;                                       /*   then the seek fails as the drive was not ready */

else if (uptr->PHASE == Seek_Phase) {                   /* otherwise if a seek is in progress */
    uptr->STATUS |= S2_SEEK_CHECK;                      /*   then set Seek Check status */
    return TRUE;                                        /*     and return to let the seek complete */
    }

else if (uptr->OPCODE == Recalibrate)                   /* otherwise if the unit is recalibrating */
    target_cylinder = 0;                                /*   then seek to cylinder 0 and don't reset the EOC flag */

else {                                                  /* otherwise it's a Seek command or an auto-seek request */
    target_cylinder = cvptr->cylinder;                  /*   so seek to the controller cylinder */
    cvptr->eoc = CLEAR;                                 /*     and clear the end-of-cylinder flag */
    }

if (target_cylinder >= drive_props [model].cylinders) { /* if the cylinder is out of bounds */
    delta = 0;                                          /*   then don't change the positioner */
    uptr->STATUS |= S2_SEEK_CHECK;                      /*     and set Seek Check status */
    }

else {                                                  /* otherwise the cylinder value is OK */
    delta = abs (uptr->CYL - (int32) target_cylinder);  /* calculate the relative movement */
    uptr->CYL = target_cylinder;                        /*   and move the positioner */

    if (cvptr->head >= drive_props [model].heads        /* if the head */
      || cvptr->sector >= drive_props [model].sectors)  /*   or the sector is out of bounds */
        uptr->STATUS |= S2_SEEK_CHECK;                  /*     then set Seek Check status */

    else {                                              /* otherwise the head and sector are OK */
        uptr->STATUS &= ~S2_SEEK_CHECK;                 /*   so clear Seek Check status */

        if (uptr->flags &                               /* if the selected head is protected */
          (cvptr->head > 1 ? UNIT_PROT_L : UNIT_PROT_U))
            uptr->STATUS |= S2_READ_ONLY;               /*   then set read-only status */
        else                                            /* otherwise */
            uptr->STATUS &= ~S2_READ_ONLY;              /*   clear it */
        }
    }

if (uptr->STATUS & S2_SEEK_CHECK && cvptr->type == ICD) /* if a seek check occurred on an ICD controller */
    return FALSE;                                       /*   then the command fails immediately */

else {                                                  /* otherwise the seek was OK or this is a MAC controller */
    uptr->PHASE = Seek_Phase;                           /*   so set the unit to the seek phase */

    uptr->wait = cvptr->dlyptr->seek_one                /* set the seek delay, based on the relative movement */
                   + delta * (cvptr->dlyptr->seek_full - cvptr->dlyptr->seek_one)
                   / drive_props [model].cylinders;
    }

return TRUE;                                            /* the seek is underway */
}


/* Report an I/O error.

   Errors indicated by the host file system are printed on the simulation
   console, and the current command is terminated with an Uncorrectable Data
   Error indication from the controller.  The target OS will retry the
   operation; if it continues to fail, the OS will handle it appropriately.
*/

static void io_error (CVPTR cvptr, UNIT *uptr)
{
cprintf ("%s simulator disc library I/O error: %s\n",   /* report the error to the console */
         sim_name, strerror (errno));

clearerr (uptr->fileref);                               /* clear the error */

end_command (cvptr, uptr, Uncorrectable_Data_Error);    /* terminate the command with a bad data error */
return;
}


/* Set up the controller completion.

   This routine performs a scheduled "end_command" to complete a command after a
   short delay.  It is called for commands that execute to completion with no
   drive or CPU interface interaction.  An otherwise unused "end phase" is
   scheduled just so that the command does not appear to complete
   instantaneously.
*/

static void set_completion (CVPTR cvptr, UNIT *uptr, CNTLR_STATUS status)
{
cvptr->status = status;                                 /* save the supplied status */
uptr->PHASE   = End_Phase;                              /* schedule the end phase */
uptr->wait    = cvptr->dlyptr->overhead / 2;            /*   with a short delay */
return;
}



/* Disc library local utility routines */


/* Set the current controller address into the buffer.

   The controller's current cylinder, head, and sector are packed into two words
   and stored in the sector buffer, starting at the index specified.  If the
   end-of-cylinder flag is set, the cylinder is incremented to reflect the
   auto-seek that will be attempted when the next sequential access is made.


   Implementation notes:

    1. The 13037 firmware always increments the cylinder number if the EOC flag
       is set, rather than checking cylinder increment/decrement bit in the file
       mask.
*/

static void set_address (CVPTR cvptr, uint32 index)
{
cvptr->buffer [index] =                                 /* update the cylinder if EOC is set */
  (DL_BUFFER) cvptr->cylinder + (cvptr->eoc == SET ? 1 : 0);

cvptr->buffer [index + 1] =                             /* merge the head and sector */
  (DL_BUFFER) (PO_HEAD (cvptr->head) | PO_SECTOR (cvptr->sector));

return;
}


/* Return the drive status (Status-2).

   This routine returns the formatted unit status for the indicated drive unit.

   In hardware, the controller outputs the Address Unit command on the drive tag
   bus and the unit number on the drive control bus.  The addressed drive then
   responds by setting its internal "selected" flag.  The controller then
   outputs the Request Status command on the tag bug, and the selected drive
   returns its status on the control bus.  If a drive is selected but the heads
   are unloaded, the drive returns Not Ready and Busy status.  If no drive is
   selected, the control bus floats inactive.  This is interpreted by the
   controller as Not Ready status (because the drive returns an inactive Ready
   status).

   In simulation, an enabled but detached unit corresponds to "selected but
   heads unloaded," and a disabled unit corresponds to a non-existent unit.


   Implementation notes:

    1. The Attention, Read-Only, First Status, and Seek Check bits are stored
       in the unit status field.  The other status bits are determined
       dynamically (the Drive Fault bit is not simulated).

    2. The Drive Busy bit is set if the unit is in the seek phase.  In hardware,
       this bit indicates that the heads are not positioned over a track, i.e.,
       that a seek is in progress.  A Request Status command is accepted only
       when the controller is waiting for seek completion or for a new command.
       Therefore, the unit will be either in the seek phase or the idle phase,
       respectively, when status is returned.
*/

static HP_WORD drive_status (UNIT *uptr)
{
HP_WORD status;

if (uptr == NULL)                                       /* if the unit is invalid */
    return S2_ERROR | S2_NOT_READY;                     /*   then it does not respond */

status =                                                /* start with the drive type and unit status */
  S2_DRIVE_TYPE (GET_MODEL (uptr->flags)) | uptr->STATUS;

if (uptr->flags & UNIT_FMT)                             /* if the format switch is enabled */
    status |= S2_FORMAT_EN;                             /*   then set the Format status bit */

if (uptr->flags & UNIT_DIS)                             /* if the unit does not exist */
    status |= S2_NOT_READY;                             /*   then set the Not Ready bit */

else if (uptr->flags & UNIT_UNLOAD)                     /* if the heads are unloaded */
    status |= S2_NOT_READY | S2_BUSY;                   /*   then set the Not Ready and Drive Busy bits */

if (uptr->PHASE == Seek_Phase)                          /* if a seek is in progress */
    status |= S2_BUSY;                                  /*   then set the Drive Busy bit */

if (status & S2_ERRORS)                                 /* if there any Status-2 errors */
    status |= S2_ERROR;                                 /*   then set the Error summary bit */

return status;                                          /* return the unit status */
}


/* Activate the unit.

   The specified unit is activated using the unit's "wait" time.  If tracing
   is enabled, the activation is logged to the debug file.


   Implementation notes:

    1. The "%.0u" print specification in the trace call absorbs the zero "unit"
       value parameter without printing when the controller unit is specified.
*/

static t_stat activate_unit (CVPTR cvptr, UNIT *uptr)
{
t_stat result;
const int32 unit = (int32) (uptr - cvptr->device->units);   /* the unit number */

dpprintf (cvptr->device, DL_DEB_SERV, (unit == CNTLR_UNIT
                                         ? "Controller unit%.0d %s %s phase delay %d service scheduled\n"
                                         : "Unit %d %s %s phase delay %d service scheduled\n"),
          (unit == CNTLR_UNIT ? 0 : unit), opcode_name [uptr->OPCODE],
          phase_name [uptr->PHASE], uptr->wait);

result = sim_activate (uptr, uptr->wait);               /* activate the unit */
uptr->wait = NO_EVENT;                                  /*   and reset the activation time */

return result;                                          /* return the activation status */
}


/* Set up the rotation phase.

   The supplied unit is set to the rotate phase at the start of a read or write
   command.  In real time mode, the rotational latency is determined by the
   distance between the "current" sector location and the target sector
   location.  The former is estimated from the current "simulation time," which
   is the number of event ticks since the simulation run was started, and the
   simulated disc rotation time, as follows:

     (simulation_time / per_sector_time) MOD sectors_per_track

   The distance is then:

     (sectors_per_track + target_sector - current_sector) MOD sectors_per_track

   ...and the latency is then:

     distance * per_sector_time

   In fast time mode, the latency is fixed at the specified per-sector time.
*/

static void set_rotation (CVPTR cvptr, UNIT *uptr)
{
uint32 sectors_per_track;
double distance;

uptr->PHASE = Rotate_Phase;                             /* set the phase */

if (cvptr->device->flags & DEV_REALTIME) {              /* if the mode is real time */
    sectors_per_track =                                 /*   then calculate the latency as above */
      drive_props [GET_MODEL (uptr->flags)].sectors;

    distance =
      fmod (sectors_per_track + cvptr->sector - CURRENT_SECTOR (cvptr, uptr),
            sectors_per_track);

    uptr->wait = (int32) (cvptr->dlyptr->sector_full * distance);
    }

else                                                    /* otherwise the mode is fast time */
    uptr->wait = cvptr->dlyptr->sector_full;            /*   so use the specified time directly */
}


/* Set the image file position.

   A cylinder/head/sector address is converted into a byte offset to pass to the
   host file I/O routines.  The cylinder is supplied by the drive unit, and the
   head and sector addresses are supplied by the controller.  The disc image
   file is laid out in one or two pieces, depending on whether a fixed platter
   is present in the drive.  If it is, then the area corresponding to the
   removable platter precedes the area corresponding to the fixed platter.  If
   not, then the file contains a single area encompassing all of the (removable)
   heads.

   In either case, the target track within the area is:

     cylinder * heads_per_cylinder + head

   ...and the target byte position in the file is:

     (target_track * sectors_per_track + sector) * bytes_per_sector
*/

static void set_file_pos (CVPTR cvptr, UNIT *uptr, uint32 model)
{
uint32 track;

if (cvptr->head < drive_props [model].remov_heads)              /* if the head is on a removable platter */
    track = uptr->CYL * drive_props [model].remov_heads         /*   then the tracks in the file are contiguous */
              + cvptr->head;

else                                                            /* otherwise the head is on a fixed platter */
    track = drive_props [model].cylinders                       /*   so the target track is located */
              * drive_props [model].remov_heads                 /*     in the second area */
              + uptr->CYL * drive_props [model].fixed_heads     /*       that is offset from the first */
              + cvptr->head - drive_props [model].remov_heads;  /*         by the size of the removable platter */

uptr->pos = (track * drive_props [model].sectors + cvptr->sector)   /* set the byte offset in the file */
              * WORDS_PER_SECTOR * sizeof (DL_BUFFER);              /*   of the CHS target sector */

return;
}
