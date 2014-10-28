/* hp_disclib.c: HP MAC/ICD disc controller simulator library

   Copyright (c) 2011-2014, J. David Bryan
   Copyright (c) 2004-2011, Robert M. Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   27-Oct-14    JDB     Corrected the relative movement calculation in start_seek
   20-Dec-12    JDB     sim_is_active() now returns t_bool
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   07-May-12    JDB     Corrected end-of-track delay time logic
   02-May-12    JDB     First release
   09-Nov-11    JDB     Created disc controller common library from DS simulator

   References:
   - 13037 Disc Controller Technical Information Package (13037-90902, Aug-1980)
   - HP 13365 Integrated Controller Programming Guide (13365-90901, Feb-1980)
   - HP 1000 ICD/MAC Disc Diagnostic Reference Manual (5955-4355, Jun-1984)
   - RTE-IVB System Manager's Manual (92068-90006, Jan-1983)
   - DVR32 RTE Moving Head Driver source (92084-18711, Revision 5000)


   This library provides common functions required by HP disc controllers.  It
   implements the 13037 MAC and 13365 ICD controller command sets used with the
   7905/06/20/25 and 7906H/20H/25H disc drives.

   The library is an adaptation of the code originally written by Bob Supnik
   for the DS simulator.  DS simulates a 13037 controller connected via a 13175
   disc interface to an HP 1000 computer.  To create the library, the functions
   of the controller were separated from the functions of the interface.  This
   allows the library to work with other CPU interfaces, such as the 12821A
   HP-IB disc interface, that use substantially different communication
   protocols.  The library functions implement the controller command set for
   the drive units.  The interface functions handle the transfer of commands and
   data to and from the CPU.

   As a result of this separation, the library does not handle the data transfer
   between the controller and the interface directly.  Instead, data is moved
   between the interface and a sector buffer by the interface simulator, and
   then the buffer is passed to the disc library for reading or writing.  This
   buffer is also used to pass disc commands and parameters to the controller,
   and to receive status information from the controller.  Only one buffer is
   needed per interface, regardless of the number of controllers or units
   handled, as a single interface cannot perform data transfers concurrently
   with controller commands.

   The library provides routines to prepare, start, and end commands, service
   units, and poll drives for Attention status.  In addition, routines are
   provided to attach and detach disc images from drive units, load and unload
   disc heads, classify commands, and provide opcode and phase name strings for
   debugging.

   Autosizing is supported when attaching a disc image.  If enabled, the model
   of the drive is set to match the disc image size.  For example, if a 50 MB
   disc image is attached to a unit set for autosizing, the unit's model will be
   set to a 7920(H).

   The interface simulator declares a structure that contains the state
   variables for a controller.  A MAC controller may handle multiple disc units.
   An ICD controller handles only a single disc unit, but multiple controllers
   may be employed to support several drives on a given interface.  The type of
   the controller (MAC or ICD) is contained in the structure, which is passed to
   the disc library routines.  The minor differences in controller action
   between the two are handled internally.  A macro (CNTLR_INIT) is provided to
   initialize the structure.

   The interface simulator also declares the sector buffer.  The buffer is an
   array containing DL_BUFSIZE 16-bit elements.  The address of the buffer is
   stored in the controller state structure.  The controller maintains the
   current index into the buffer, as well as the length of valid data stored
   there.  Other than setting the length when the controller places data into
   the buffer and resetting the index at the start of a sector read or write,
   the interface simulator is free to manipulate these values as desired.

   In general, a user of the library is free to read any of the controller state
   variable structure fields.  Writing to the fields generally will interfere
   with controller operations, with these exceptions:

     Field Name   Description
     ===========  ============================
     status       controller status
     eod          end of data flag
     index        data buffer index
     length       data buffer length
     seek_time    per-cylinder seek delay time
     sector_time  intersector delay time
     cmd_time     command response time
     data_time    data transfer response time
     wait_time    command wait time

   In hardware, the controller executes in three basic states:

    1. In the Poll Loop, which looks for commands and drive attention requests.

       In each pass of the loop, the next CPU interface in turn is checked for a
       command; if present, it is executed.  If none are pending, all drives are
       checked in turn until one is found with Attention status; if none are
       found, the loop continues.  If a drive is requesting attention, the
       associated CPU interface is connected to check for a command; if present,
       it is executed.  If not, and the interface allows interrupts, an
       interrupt request is made and the Command Wait Loop is entered.  If
       interrupts are not allowed, the Poll Loop continues.

    2. In the Command Wait Loop, which looks for commands.

       In each pass of the loop, the current CPU interface is checked for a
       command; if present, it is executed.  If not, the Command Wait Loop
       continues.  While in the loop, a 1.8 second timer is running.  If it
       expires before a command is received, the file mask is reset, and the
       Poll Loop is entered.

    3. In command execution, which processes the current command.

       During command execution, the waits for input parameters, seek
       completion, data transfers, and output status words are handled
       internally.  Each wait is governed by the 1.8 second timer; if it
       expires, the command is aborted.

   In simulation, these states are represented by the values cntlr_idle,
   cntlr_wait, and cntlr_busy, respectively.

   A MAC controller operates from one to eight drives, represented by an array
   of one to eight units.  When operating multiple units, a pointer to the first
   unit of a contiguous array is passed, and the unit number present in the
   command is used to index to the target unit.

   A MAC controller emulation also requires an array of two contiguous auxiliary
   units containing a controller unit and a command wait timeout unit.  Commands
   that do not access the drive, such as Address Record, are scheduled on the
   controller unit to allow controller commands to execute while drive units are
   seeking.  The command wait timer limits the amount of time the controller
   will wait for the interface to supply a command or parameter.  A pointer to
   the auxiliary unit array is set up during controller state variable
   initialization.  The auxiliary array may be separate or an extension of the
   drive unit array.

   An ICD controller manages a single unit corresponding to the drive in which
   the controller is integrated.  An interface declares a unit array
   corresponding to the number of drives supported and passes the unit number to
   use to the command preparation and start routines.  Auxiliary units are not
   used, and all commands are scheduled on the drive unit associated with a
   given controller.

   The library provides a unit service routine to handle all of the disc
   commands.  The routine is called from the interface service routine to handle
   the common disc actions, while the interface routine handles actions specific
   to the operation of the interface (such as data transfer).

   The service routine schedules the unit to continue command execution under
   these conditions:

     1. A Seek or Recalibrate command is waiting for the seek completion.

     2. A read or write command is waiting for the first data transfer of a
        sector to start.

     3. A read or write command is waiting for the next sector to start after
        the final data transfer of the preceding sector.

     4. A Verify command is waiting for the end of the current sector.

   The library also provides controller and timer service routines for MAC
   emulations.  All three (unit, controller, and timer) must be called from
   their respective interface service routines before any interface-specific
   actions, if any, are taken.

   On return from the library unit or controller service routines, the "wait"
   field of the UNIT structure will be set to the activation time if the unit
   is to be scheduled.  The caller is responsible for activating the unit.  If
   the caller uses this feature, the field should be reset to zero before the
   next service call.

   The MAC timer unit is activated by the library, and its "wait" field is not
   used.  The timer starts when a command other than End, Seek, or Recalibrate
   completes, or when the controller is waiting for the interface to supply or
   accept a parameter during command execution.  It stops when an End, Seek, or
   Recalibrate command completes, a command is prepared for execution, or the
   final parameter has been supplied or accepted by the interface during command
   execution.

   The controller maintains six variables in each drive's unit structure:

     wait       -- the current service activation time
     pos        -- the current byte offset into the disc image file
     u3 (CYL)   -- the current drive cylinder
     u4 (STAT)  -- the drive status (Status-2)
     u5 (OP)    -- the drive operation in process
     u6 (PHASE) -- the current operation phase

   These and other definitions are in the file hp_disclib.h, which must be
   included in the interface simulator.

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

    3. The library does not simulate interface signals or function bus orders,
       except for EOD (End of Data) and BUSY.  The interface simulators must
       decide for themselves what actions to take (e.g., interrupting the CPU)
       on the basis of the controller state.

    4. The command/sector buffer is an array of 16-bit elements.  Byte-oriented
       interface simulators, such as the 12821A HP-IB Disc Interface, must do
       their own byte packing and unpacking.

    5. The SAVE command does not save the "wait" and "pos" fields of the UNIT
       structure automatically.  To ensure that they are saved, they are
       referenced by hidden, read-only registers.
*/



#include <math.h>

#include "hp_disclib.h"



/* Command accessors */

#define DL_V_OPCODE      8                              /* bits 12- 8: general opcode */
#define DL_V_HOLD        7                              /* bits  7- 7: general hold flag */
#define DL_V_UNIT        0                              /* bits  3- 0: general unit number */

#define DL_V_SPD        13                              /* bits 15-13: Initialize S/P/D flags */
#define DL_V_CHEAD       6                              /* bits  7- 6: Cold Load Read head number */
#define DL_V_CSECT       0                              /* bits  5- 0: Cold Load Read sector number */
#define DL_V_FRETRY      4                              /* bits  7- 4: Set File Mask retry count */
#define DL_V_FDECR       3                              /* bits  3- 3: Set File Mask seek decrement */
#define DL_V_FSPEN       2                              /* bits  2- 2: Set File Mask sparing enable */
#define DL_V_FCYLM       1                              /* bits  1- 1: Set File Mask cylinder mode */
#define DL_V_FAUTSK      0                              /* bits  0- 0: Set File Mask auto seek */

#define DL_V_FMASK       0                              /* bits  3- 0: Set File Mask (flags combined) */


#define DL_M_OPCODE     037                             /* opcode mask */
#define DL_M_UNIT       017                             /* unit mask */

#define DL_M_SPD        007                             /* S/P/D flags mask */
#define DL_M_CHEAD      003                             /* Cold Load Read head number mask */
#define DL_M_CSECT      077                             /* Cold Load Read sector number mask */
#define DL_M_FRETRY     017                             /* Set File Mask retry count mask */
#define DL_M_FMASK      017                             /* Set File Mask flags mask */


#define GET_OPCODE(c)   (CNTLR_OPCODE) (((c) >> DL_V_OPCODE) & DL_M_OPCODE)
#define GET_UNIT(c)     (((c) >> DL_V_UNIT)   & DL_M_UNIT)

#define GET_SPD(c)      (((c) >> DL_V_SPD)    & DL_M_SPD)
#define GET_CHEAD(c)    (((c) >> DL_V_CHEAD)  & DL_M_CHEAD)
#define GET_CSECT(c)    (((c) >> DL_V_CSECT)  & DL_M_CSECT)
#define GET_FRETRY(c)   (((c) >> DL_V_FRETRY) & DL_M_FRETRY)
#define GET_FMASK(c)    (((c) >> DL_V_FMASK)  & DL_M_FMASK)

#define DL_FDECR        (1 << DL_V_FDECR)
#define DL_FSPEN        (1 << DL_V_FSPEN)
#define DL_FCYLM        (1 << DL_V_FCYLM)
#define DL_FAUTSK       (1 << DL_V_FAUTSK)


/* Parameter accessors */

#define DL_V_HEAD       8                               /* bits 12- 8: head number */
#define DL_V_SECTOR     0                               /* bits  7- 0: sector number */

#define DL_M_HEAD       0017                            /* head number mask */
#define DL_M_SECTOR     0377                            /* sector number mask */

#define GET_HEAD(p)     (((p) >> DL_V_HEAD) & DL_M_HEAD)
#define GET_SECTOR(p)   (((p) >> DL_V_SECTOR) & DL_M_SECTOR)

#define SET_HEAD(c)     (((c)->head & DL_M_HEAD) << DL_V_HEAD)
#define SET_SECTOR(c)   (((c)->sector & DL_M_SECTOR) << DL_V_SECTOR)


/* Drive properties table.

   In hardware, drives report their Drive Type numbers to the controller upon
   receipt of a Request Status tag bus command.  The drive type is used to
   determine the legal range of head and sector addresses (the drive itself will
   validate the cylinder address during seeks).

   In simulation, we set up a table of drive properties and use the model ID as
   an index into the table.  The table is used to validate seek parameters and
   to provide the mapping between CHS addresses and the linear byte addresses
   required by the host file access routines.

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

   This variable-access geometry is accomplished by defining additional "heads
   per cylinder" values for the fixed and removable sections of each drive that
   indicates the number of heads that should be grouped for locality.  The
   removable values are set to 2 on the 7905 and 7906, indicating that those
   drives typically use cylinders consisting of two heads.  They are set to the
   number of heads per drive for the 7920 and 7925, as those typically use
   cylinders encompassing the entire pack.
*/

#define D7905_RH        2
#define D7905_FH        (D7905_HEADS - D7905_RH)

#define D7906_RH        2
#define D7906_FH        (D7906_HEADS - D7906_RH)

#define D7920_RH        D7920_HEADS
#define D7920_FH        (D7920_HEADS - D7920_RH)

#define D7925_RH        D7925_HEADS
#define D7925_FH        (D7925_HEADS - D7925_RH)

typedef struct {
    uint32 sectors;                                     /* sectors per head */
    uint32 heads;                                       /* heads per cylinder*/
    uint32 cylinders;                                   /* cylinders per drive */
    uint32 words;                                       /* words per drive */
    uint32 type;                                        /* drive type */
    uint32 remov_heads;                                 /* number of removable-platter heads */
    uint32 fixed_heads;                                 /* number of fixed-platter heads */
    } DRIVE_PROPERTIES;


static const DRIVE_PROPERTIES drive_props [] = {
    { D7905_SECTS, D7905_HEADS, D7905_CYLS, D7905_WORDS, D7905_TYPE, D7905_RH, D7905_FH },
    { D7906_SECTS, D7906_HEADS, D7906_CYLS, D7906_WORDS, D7906_TYPE, D7906_RH, D7906_FH },
    { D7920_SECTS, D7920_HEADS, D7920_CYLS, D7920_WORDS, D7920_TYPE, D7920_RH, D7920_FH },
    { D7925_SECTS, D7925_HEADS, D7925_CYLS, D7925_WORDS, D7925_TYPE, D7925_RH, D7925_FH }
    };

#define PROPS_COUNT     (sizeof (drive_props) / sizeof (drive_props [0]))


/* Convert a CHS address to a block offset.

   A cylinder/head/sector address is converted into a linear block address that
   may be used to calculate a byte offset to pass to the file access routines.
   The conversion logic is:

     if Head < removable_heads_per_cylinder then
        tracks := Cylinder * removable_heads_per_cylinder + Head;
     else
        tracks := cylinders_per_drive * removable_heads_per_cylinder +
                  Cylinder * fixed_heads_per_cylinder + (Head - removable_heads_per_cylinder);

     block := tracks * sectors_per_track + Sector;

     byte_offset := block * words_per_sector * bytes_per_word;

   The byte offset is calculated in two steps to allow for future controller
   enhancements to support the CS/80 command set and its associated linear block
   addressing mode.
*/

#define TO_BLOCK(cylinder,head,sector,model) \
          (((head) < drive_props [model].remov_heads \
            ? (cylinder) * drive_props [model].remov_heads + (head) \
            : drive_props [model].cylinders * drive_props [model].remov_heads \
              + ((cylinder) * drive_props [model].fixed_heads + (head) - drive_props [model].remov_heads)) \
          * drive_props [model].sectors + (sector))

#define TO_OFFSET(block)    ((block) * DL_WPSEC * sizeof (uint16))


/* Estimate the current sector.

   The sector currently passing under the disc heads is estimated from the
   current simulator time (i.e., the count of instructions since startup) and
   the simulated disc rotation time.  The computation logic is:

     per_sector_time := word_transfer_time * words_per_sector + intersector_time;

     current_sector := (current_time / per_sector_time) MOD sectors_per_track;
*/

#define GET_CURSEC(cvptr,uptr) \
          ((uint16) fmod (sim_gtime() / (double) ((cvptr->data_time * DL_WPSEC + cvptr->sector_time)), \
                          (double) drive_props [GET_MODEL (uptr->flags)].sectors))


/* Command properties table.

   The validity of each command for a specified controller type is checked
   against the command properties table when it is prepared.  The table also
   includes the count of inbound and outbound properties, the class of the
   command, and flags to indicate certain common actions that should be taken.
*/

typedef struct {
    uint32       params_in;                             /* count of input parameters */
    uint32       params_out;                            /* count of output parameters */
    CNTLR_CLASS  classification;                        /* command classification */
    t_bool       valid [type_count];                    /* per-type command validity */
    t_bool       clear_status;                          /* command clears the controller status */
    t_bool       unit_field;                            /* command has a unit field */
    t_bool       unit_check;                            /* command checks the unit number validity */
    t_bool       unit_access;                           /* command accesses the drive unit */
    t_bool       seek_wait;                             /* command waits for seek completion */
    } DS_PROPS;

typedef const DS_PROPS *PRPTR;

#define T   TRUE
#define F   FALSE

static const DS_PROPS cmd_props [] = {
/*   par par  opcode           valid for  clear unit  unit  unit  seek */
/*   in  out  classification   MAC  ICD   stat  field check acces wait */
    { 0,  0,  class_read,     { T,   T },   T,    F,    T,    T,    F },   /* 00 = cold load read */
    { 0,  0,  class_control,  { T,   T },   T,    T,    T,    T,    T },   /* 01 = recalibrate */
    { 2,  0,  class_control,  { T,   T },   T,    T,    T,    T,    F },   /* 02 = seek */
    { 0,  2,  class_status,   { T,   T },   F,    T,    F,    F,    F },   /* 03 = request status */
    { 0,  1,  class_status,   { T,   T },   T,    T,    T,    F,    F },   /* 04 = request sector address */
    { 0,  0,  class_read,     { T,   T },   T,    T,    T,    T,    T },   /* 05 = read */
    { 0,  0,  class_read,     { T,   T },   T,    T,    T,    T,    T },   /* 06 = read full sector */
    { 1,  0,  class_read,     { T,   T },   T,    T,    T,    T,    T },   /* 07 = verify */
    { 0,  0,  class_write,    { T,   T },   T,    T,    T,    T,    T },   /* 10 = write */
    { 0,  0,  class_write,    { T,   T },   T,    T,    T,    T,    T },   /* 11 = write full sector */
    { 0,  0,  class_control,  { T,   T },   T,    F,    F,    F,    F },   /* 12 = clear */
    { 0,  0,  class_write,    { T,   T },   T,    T,    T,    T,    T },   /* 13 = initialize */
    { 2,  0,  class_control,  { T,   T },   T,    F,    F,    F,    F },   /* 14 = address record */
    { 0,  7,  class_status,   { T,   F },   F,    F,    F,    F,    F },   /* 15 = request syndrome */
    { 1,  0,  class_read,     { T,   T },   T,    T,    T,    T,    T },   /* 16 = read with offset */
    { 0,  0,  class_control,  { T,   T },   T,    F,    F,    F,    F },   /* 17 = set file mask */
    { 0,  0,  class_invalid,  { F,   F },   T,    F,    F,    F,    F },   /* 20 = invalid */
    { 0,  0,  class_invalid,  { F,   F },   T,    F,    F,    F,    F },   /* 21 = invalid */
    { 0,  0,  class_read,     { T,   T },   T,    T,    T,    T,    T },   /* 22 = read without verify */
    { 1,  0,  class_status,   { T,   F },   T,    F,    F,    F,    F },   /* 23 = load TIO register */
    { 0,  2,  class_status,   { T,   T },   F,    F,    F,    F,    F },   /* 24 = request disc address */
    { 0,  0,  class_control,  { T,   T },   T,    F,    F,    F,    F },   /* 25 = end */
    { 0,  0,  class_control,  { T,   F },   T,    T,    T,    F,    F }    /* 26 = wakeup */
    };


/* Auxiliary unit indices */

typedef enum {
    controller = 0,                                     /* controller unit index */
    timer                                               /* command wait timer index */
    } AUX_INDEX;


/* Controller opcode names */

static const char invalid_name [] = "invalid";

static const char *opcode_name [] = {
    "cold load read",                                   /* 00 */
    "recalibrate",                                      /* 01 */
    "seek",                                             /* 02 */
    "request status",                                   /* 03 */
    "request sector address",                           /* 04 */
    "read",                                             /* 05 */
    "read full sector",                                 /* 06 */
    "verify",                                           /* 07 */
    "write",                                            /* 10 */
    "write full sector",                                /* 11 */
    "clear",                                            /* 12 */
    "initialize",                                       /* 13 */
    "address record",                                   /* 14 */
    "request syndrome",                                 /* 15 */
    "read with offset",                                 /* 16 */
    "set file mask",                                    /* 17 */
    invalid_name,                                       /* 20 = invalid */
    invalid_name,                                       /* 21 = invalid */
    "read without verify",                              /* 22 */
    "load TIO register",                                /* 23 */
    "request disc address",                             /* 24 */
    "end",                                              /* 25 */
    "wakeup"                                            /* 26 */
    };

/* Controller phase names */

static const char *phase_name [] = {
    "start",
    "data",
    "end"
    };



/* Disc library local controller routines */

static t_bool start_seek      (CVPTR cvptr, UNIT *uptr, CNTLR_OPCODE next_opcode, CNTLR_PHASE next_phase);
static t_stat start_read      (CVPTR cvptr, UNIT *uptr);
static void   end_read        (CVPTR cvptr, UNIT *uptr);
static void   start_write     (CVPTR cvptr, UNIT *uptr);
static t_stat end_write       (CVPTR cvptr, UNIT *uptr);
static t_bool position_sector (CVPTR cvptr, UNIT *uptr, t_bool verify);
static void   next_sector     (CVPTR cvptr, UNIT *uptr);
static t_stat io_error        (CVPTR cvptr, UNIT *uptr);

/* Disc library local utility routines */

static void   set_address  (CVPTR cvptr, uint32    index);
static void   set_timer    (CVPTR cvptr, FLIP_FLOP action);
static uint16 drive_status (UNIT  *uptr);



/* Disc library global controller routines */


/* Prepare a command for execution.

   On entry, the first word of the controller buffer contains the command to
   prepare, the "cvptr" parameter points at the controller state variable
   structure, and the "units" parameter points at the first unit of the unit
   array.  For a MAC controller, the "unit limit" parameter indicates the last
   valid unit number, and the unit to use is taken from the unit field of the
   command word.  For an ICD controller, the parameter indicates the number
   of the unit to use directly.

   If a valid command was prepared for execution, the routine returns TRUE and
   sets the controller state to "busy."  If the command is illegal, the routine
   returns FALSE and sets the controller state to "waiting."  In the latter
   case, the controller status will indicate the reason for the rejection.

   The opcode and unit number (for MAC controllers) are obtained from the buffer
   and checked for legality.  If either is illegal, the controller status is set
   appropriately, and the routine returns FALSE.

   For a valid command and an available unit, the controller's opcode field is
   set from the buffer, the length field is set to the number of inbound
   parameter words expected, and the index field is set to 1 to point at the
   first parameter entry in the buffer.
*/

t_bool dl_prepare_command (CVPTR cvptr, UNIT *units, uint32 unit_limit)
{
uint32 unit;
PRPTR props;
CNTLR_OPCODE opcode;

set_timer (cvptr, CLEAR);                               /* stop the command wait timer */

opcode = GET_OPCODE (cvptr->buffer [0]);                /* get the opcode from the command */

if (opcode > Last_Opcode)                               /* is the opcode invalid? */
    props = &cmd_props [Invalid_Opcode];                /* undefined commands clear prior status */
else                                                    /* the opcode is potentially valid */
    props = &cmd_props [opcode];                        /* get the command properties */

if (cvptr->type == MAC)                                 /* is this a MAC controller? */
    if (props->unit_field)                              /* is the unit field defined for this command? */
        unit = GET_UNIT (cvptr->buffer [0]);            /* get the unit from the command */
    else                                                /* no unit specified in the command */
        unit = 0;                                       /*   so the unit is always unit 0 */

else                                                    /* an ICD controller */
    unit = unit_limit;                                  /*   uses the supplied unit number */

if (props->clear_status) {                              /* clear the prior controller status */
    cvptr->status = normal_completion;                  /*   if indicated for this command */
    cvptr->spd_unit = SET_S1UNIT (unit);                /* save the unit number for status requests */
    }

if (cvptr->type <= last_type                            /* is the controller type legal, */
  && props->valid [cvptr->type])                        /*   and the opcode defined for this controller? */
    if (props->unit_check && unit > DL_MAXUNIT)         /* if the unit number is checked and is illegal, */
        dl_end_command (cvptr, unit_unavailable);       /*   end with a unit unavailable error */

    else {
        cvptr->state = cntlr_busy;                      /* legal unit, so controller is now busy */
        cvptr->opcode = opcode;                         /* save the controller opcode */
        cvptr->length = props->params_in;               /* set the inbound parameter count */
        cvptr->index = 1;                               /* point at the first parameter element (if any) */

        if (cvptr->type == MAC && cvptr->length) {      /* is this a MAC controller with inbound parameters? */
            cvptr->aux [controller].OP = opcode;        /* save the opcode */
            cvptr->aux [controller].PHASE = data_phase; /*   and set the phase for parameter pickup */
            set_timer (cvptr, SET);                     /* start the timer to wait for the first parameter */
            }

        return TRUE;                                    /* the command is now prepared for execution */
        }

else                                                    /* the opcode is undefined */
    dl_end_command (cvptr, illegal_opcode);             /*   so set bad opcode status */

return FALSE;                                           /* the preparation has failed */
}


/* Start a command.

   On entry, the controller's opcode field contains the command to start, and
   the buffer contains the command word in element 0 and the parameters required
   by the command, if any, beginning in element 1.  The call parameters are the
   same as those supplied to the "prepare command" routine.

   If the command was started successfully, the routine returns a pointer to the
   unit to be activated and sets that unit's "wait" field to the activation
   time.  The caller should activate the unit upon return to complete or
   continue command processing.  If the command did not start, the routine
   returns NULL.

   If a seek is in progress on a drive when a command accessing that drive is
   started, the unit pointer is returned but the unit's "wait" field is set to
   zero.  In this case, the unit must not be activated (as it already is).
   Instead, the unit's opcode and phase fields will have been set to start the
   command automatically when the seek completes.

   For commands that return status from the controller, the buffer will contain
   the returned value(s), the buffer index will be zero, and the buffer length
   will be set to the number of words returned in the buffer.  These words must
   be returned to the CPU via the interface.


   Implementation notes:

    1. A command must have been prepared by calling dl_prepare_command first.
       After preparation, the controller's opcode will be valid, and the unit
       number field will be legal (but not necessarily valid) for those commands
       that check the unit.

       Unit numbers 0-7 represent valid drive addresses.  However, the MAC
       controller firmware allows access to unit numbers 8-10 without causing a
       Unit Unavailable error.  Instead, the controller reports these units as
       permanently offline.

    2. Commands that check for a valid unit do some processing before failing
       with a Status-2 (not ready) error if the unit is invalid.  For example,
       the Seek command accepts its parameters from the CPU and sets the CHS
       values into the controller before failing.

    3. In hardware, read, write, and recalibrate commands wait in an internal
       loop for a pending seek completion and clear the resulting Attention
       status before executing.  In simulation, we change a seeking drive unit's
       opcode and phase fields from seek completion to the start of the next
       command.  This eliminates the setting of the Attention status and begins
       command execution automatically when the seek completes.

       If the seek completed between the command preparation and start,
       Attention will have been set.  If the unit is idle on entry, we clear the
       Attention status unilaterally (it doesn't matter whether or not it was
       set; Attention always is clear when commands start).

    4. The Seek and Cold Load Read commands do not check for a seek or
       recalibrate in progress.  If the heads are moving, the drive will reject
       a seek command with a Seek Check error.  The firmware does not test
       explicitly for Access Not Ready before executing the command, so the
       parameters (e.g., controller CHS addresses) are still set as though the
       command had succeeded.

       A Seek command will return to the Poll Loop with Seek Check status set.
       When the seek in progress completes, the controller will interrupt with
       Drive Attention status.  The controller address will differ from the
       drive address, so it's incumbent upon the caller to issue a Request
       Status command after the seek, which will return Status-2 Error status.

       A Cold Load Read command issues a seek to cylinder 0 and then begins a
       read, which first waits for seek completion.  The Seek Check error will
       abort the command at this point with Status-2 Error status.

       In simulation, a Seek command allows the seek in progress to complete
       normally, whereas a Cold Load Read command modifies the unit command
       and phase from the end phase of Seek or Recalibrate to the start
       phase of Read, which will catch the Seek Check error as in hardware.

    5. The Cold Load Read command checks if the drive is ready before setting
       the file mask.  Therefore, we normally defer setting the file mask until
       the unit service is called.  However, if a seek is in progress, then the
       drive must be ready, so we set the file mask here.

    6. ECC is not simulated, so the Request Syndrome command always returns zero
       values for the displacement and patterns.

    7. The Request Status, Request Sector Address, and Wakeup commands reference
       drive units but are scheduled on the controller unit because they may be
       issued while a drive is processing a seek.

    8. The activation time is set to the intersector time (latency) for read and
       write commands, and to the controller processing time for all others.
       The read/write start time cannot be shorter than 20 instructions, or
       DVR32 will be unable to start DCPC in time to avoid an over/underrun.
*/

UNIT *dl_start_command (CVPTR cvptr, UNIT *units, uint32 unit_limit)
{
UNIT *uptr, *rptr;
uint32 unit;
PRPTR props;
t_bool is_seeking = FALSE;

props = &cmd_props [cvptr->opcode];                     /* get the command properties */

if (cvptr->type == MAC) {                               /* is this a MAC controller? */
    if (props->unit_field)                              /* is the unit field defined for this command? */
        unit = GET_UNIT (cvptr->buffer [0]);            /* get the unit number from the command */
    else                                                /* no unit is specified in the command */
        unit = 0;                                       /*   so the unit number defaults to 0 */

    if (unit > unit_limit)                              /* if the unit number is invalid, */
        uptr = NULL;                                    /*   it does not correspond to a unit */
    else if (props->unit_access)                        /* if the command accesses a drive, */
        uptr = units + unit;                            /*   get the address of the unit */
    else                                                /* the command accesses the controller only */
        uptr = cvptr->aux + controller;                 /*   so use the controller unit */
    }

else {                                                  /* for an ICD controller, */
    unit = 0;                                           /*   the unit value is ignored */
    uptr = units + unit_limit;                          /*     and we use the indicated unit */
    }

if (props->unit_check && !uptr                                  /* if the unit number is checked and is invalid */
  || props->seek_wait && (drive_status (uptr) & DL_S2STOPS)) {  /*   or if we're waiting for an offline drive */
    dl_end_command (cvptr, status_2_error);                     /*     then the command ends with a Status-2 error */
    uptr = NULL;                                                /* prevent the command from starting */
    }

else if (uptr) {                                        /* otherwise, we have a valid unit */
    uptr->wait = cvptr->cmd_time;                       /* most commands use the command delay */

    if (props->unit_access) {                           /* does the command access the unit? */
        is_seeking = sim_is_active (uptr);              /* see if the unit is busy */

        if (is_seeking)                                 /* if a seek is in progress, */
            uptr->wait = 0;                             /*   set for no unit activation */

        else {                                          /* otherwise, the unit is idle */
            uptr->STAT &= ~DL_S2ATN;                    /* clear the drive Attention status */

             if (props->classification == class_read    /* if a read command */
              || props->classification == class_write)  /*   or a write command */
                uptr->wait = cvptr->sector_time;        /*     schedule the sector start latency */
            }
        }
    }

cvptr->index = 0;                                       /* reset the buffer index */
cvptr->length = props->params_out;                      /* set the count of outbound parameters */
cvptr->eod = CLEAR;                                     /* clear the end of data flag */


switch (cvptr->opcode) {                                /* dispatch the command */

    case Cold_Load_Read:
        cvptr->cylinder = 0;                            /* set the cylinder address to 0 */
        cvptr->head = GET_CHEAD (cvptr->buffer [0]);    /* set the head */
        cvptr->sector = GET_CSECT (cvptr->buffer [0]);  /*   and sector from the command */

        if (is_seeking) {                               /* if a seek is in progress, */
            uptr->STAT |= DL_S2SC;                      /*   a Seek Check occurs */
            cvptr->file_mask = DL_FSPEN;                /* enable sparing */
            uptr->OP = Read;                            /* start the read on the seek completion  */
            uptr->PHASE = start_phase;                  /*   and reset the command phase */
            return uptr;                                /*     to allow the seek to complete normally */
            }

        else                                            /* the drive is not seeking */
            uptr->wait = cvptr->cmd_time;               /* the command starts with a seek, not a read */

        break;


    case Seek:
        cvptr->cylinder = cvptr->buffer [1];            /* get the supplied cylinder */
        cvptr->head = GET_HEAD (cvptr->buffer [2]);     /*   and head */
        cvptr->sector = GET_SECTOR (cvptr->buffer [2]); /*     and sector addresses */

        if (is_seeking) {                               /* if a seek is in progress, */
            uptr->STAT |= DL_S2SC;                      /*   a Seek Check occurs */
            dl_idle_controller (cvptr);                 /* return the controller to the idle condition */
            return uptr;                                /*   to allow the seek to complete normally */
            }

        break;


    case Request_Status:
        cvptr->buffer [0] =                             /* set the Status-1 value */
          cvptr->spd_unit | SET_S1STAT (cvptr->status); /*   into the buffer */

        if (cvptr->type == MAC)                         /* is this a MAC controller? */
            if (unit > unit_limit)                      /* if the unit number is invalid */
                rptr = NULL;                            /*   it does not correspond to a unit */
            else                                        /* otherwise, the unit is valid */
                rptr = &units [unit];                   /*   so get the address of the referenced unit */
        else                                            /* if not a MAC controller */
            rptr = uptr;                                /*   then the referenced unit is the current unit */

        cvptr->buffer [1] = drive_status (rptr);        /* set the Status-2 value */

        if (rptr)                                       /* if the unit is valid */
            rptr->STAT &= ~DL_S2FS;                     /*   clear the First Status bit */

        cvptr->spd_unit = SET_S1UNIT (unit);            /* save the unit number */

        if (unit > DL_MAXUNIT)                          /* if the unit number is illegal, */
            cvptr->status = unit_unavailable;           /*   the next status will be Unit Unavailable */
        else                                            /* a legal unit */
            cvptr->status = normal_completion;          /*   clears the controller status */

        break;


    case Request_Disc_Address:
        set_address (cvptr, 0);                         /* return the CHS values in buffer 0-1 */
        break;


    case Request_Sector_Address:
        if (unit > unit_limit)                              /* if the unit number is invalid */
            rptr = NULL;                                    /*   it does not correspond to a unit */
        else                                                /* otherwise, the unit is valid */
            rptr = &units [unit];                           /*   so get the address of the referenced unit */

        if (drive_status (rptr) & DL_S2NR)                  /* if the drive is not ready, */
            dl_end_command (cvptr, status_2_error);         /*   terminate with not ready status */
        else                                                /* otherwise, the drive is ready */
            cvptr->buffer [0] = GET_CURSEC (cvptr, rptr);   /*   so calculate the current sector address */
        break;


    case Request_Syndrome:
        cvptr->buffer [0] =                             /* return the Status-1 value in buffer 0 */
          cvptr->spd_unit | SET_S1STAT (cvptr->status);

        set_address (cvptr, 1);                         /* return the CHS values in buffer 1-2 */

        cvptr->buffer [3] = 0;                          /* the displacement is always zero */
        cvptr->buffer [4] = 0;                          /* the syndrome is always zero */
        cvptr->buffer [5] = 0;
        cvptr->buffer [6] = 0;
        break;


    case Address_Record:
        cvptr->cylinder = cvptr->buffer [1];            /* get the supplied cylinder */
        cvptr->head = GET_HEAD (cvptr->buffer [2]);     /*   and head */
        cvptr->sector = GET_SECTOR (cvptr->buffer [2]); /*     and sector addresses */
        cvptr->eoc = CLEAR;                             /* clear the end-of-cylinder flag */
        break;


    case Set_File_Mask:
        cvptr->file_mask = GET_FMASK (cvptr->buffer [0]);   /* get the supplied file mask */

        if (cvptr->type == MAC)                             /* if this is a MAC controller, */
            cvptr->retry = GET_FRETRY (cvptr->buffer [0]);  /*   the retry count is supplied too */
        break;


    case Initialize:
        if (uptr)                                       /* if the unit is valid, */
            cvptr->spd_unit |=                          /*   merge the SPD flags */
              SET_S1SPD (GET_SPD (cvptr->buffer [0]));  /*     from the command word */
        break;


    case Verify:
        cvptr->verify_count = cvptr->buffer [1];        /* get the supplied sector count */
        break;


    default:                                            /* the remaining commands */
        break;                                          /*   are handled by the service routines */
    }


if (uptr) {                                             /* if the command accesses a valid unit */
    uptr->OP = cvptr->opcode;                           /*   save the opcode in the unit */

    if (cvptr->length)                                  /* if the command has outbound parameters, */
        uptr->PHASE = data_phase;                       /*   set up the data phase for the transfer */
    else                                                /* if there are no parameters, */
        uptr->PHASE = start_phase;                      /*   set up the command phase for execution */

    return uptr;                                        /* return a pointer to the scheduled unit */
    }

else
    return NULL;                                        /* the command did not start */
}


/* Complete a command.

   The current command is completed with the indicated status.  The command
   result status is set, the controller enters the command wait state, and the
   CPU timer is restarted.
*/

void dl_end_command (CVPTR cvptr, CNTLR_STATUS status)
{
cvptr->status = status;                                 /* set the command result status */
cvptr->state = cntlr_wait;                              /* set the controller state to waiting */
set_timer (cvptr, SET);                                 /* start the command wait timer */
return;
}


/* Poll the drives for Attention status.

   If interrupts are enabled on the interface, this routine is called to check
   if any drive is requesting attention.  The routine returns TRUE if a drive is
   requesting attention and FALSE if not.

   Starting with the last unit requesting attention, each drive is checked in
   sequence.  If a drive has its Attention status set, the controller saves its
   unit number, sets the result status to Drive Attention, and enters the
   command wait state.  The routine returns TRUE to indicate that an interrupt
   should be generated.  The next time the routine is called, the poll begins
   with the last unit that requested attention, so that each unit is given an
   equal chance to respond.

   If no unit is requesting attention, the routine returns FALSE to indicate
   that no interrupt should be generated.
*/

t_bool dl_poll_drives (CVPTR cvptr, UNIT *units, uint32 unit_limit)
{
uint32 unit;

for (unit = 0; unit <= unit_limit; unit++) {                /* check each unit in turn */
    cvptr->poll_unit =                                      /* start with the last unit checked */
      (cvptr->poll_unit + 1) % (unit_limit + 1);            /*   and cycle back to unit 0 */

    if (units [cvptr->poll_unit].STAT & DL_S2ATN) {         /* if the unit is requesting attention, */
        units [cvptr->poll_unit].STAT &= ~DL_S2ATN;         /*   clear the Attention status */
        cvptr->spd_unit = SET_S1UNIT (cvptr->poll_unit);    /* set the controller's unit number */
        cvptr->status = drive_attention;                    /*   and status */
        cvptr->state = cntlr_wait;                          /*     and wait for a command */
        return TRUE;                                        /* tell the caller to interrupt */
        }
    }

return FALSE;                                               /* no requests, so do not generate an interrupt */
}


/* Service the disc drive unit.

   The unit service routine is called to execute scheduled controller commands
   for the specified unit.  The actions to be taken depend on the current state
   of the controller and the unit.

   In addition to the controller state variables supplied in the call, the
   service routine accesses these six variables in the UNIT structure:

     wait       -- the current service activation time
     pos        -- the current byte offset into the disc image file
     u3 (CYL)   -- the current drive cylinder
     u4 (STAT)  -- the drive status (Status-2)
     u5 (OP)    -- the drive operation in process
     u6 (PHASE) -- the current operation phase

   The activation time is set non-zero if the service should be rescheduled.
   The caller is responsible upon return for activating the unit.  The file
   offset indicates the byte position in the disc image file for the next read
   or write operation.

   The drive cylinder gives the current location of the head positioner.  This
   may differ from the cylinder value in the controller if the Address Record
   command has been used.  The drive status maintains various per-drive
   conditions (e.g., the state of the read-only and format switches, drive
   ready, first status).  The operation in process and operation phase define
   the action to be taken by this service routine.

   Initially, the operation in process is set to the opcode field of the command
   when it is started.  However, the operation in process may change during
   execution (the controller opcode never does).  This is to aid code reuse in
   the service routine.  For example, a Cold Load Read command is changed to a
   Read command once the seek portion is complete, and a Read Without Verify
   command is changed to a normal Read command after a track boundary is
   crossed.

   The operation phase provides different substates for those commands that
   transfer data or that have different starting and ending actions.  Three
   phases are defined: start, data, and end.  Commands that do not transfer data
   to or from the CPU interface do not have data phases, and commands that
   complete upon first service do not have end phases.  The service routine
   validates phase assignments and returns SCPE_IERR (Internal Error) if entry
   is made with an illegal operation phase or a phase that is not valid for a
   given operation.

   An operation in the data phase is in the process of transferring data between
   the CPU and sector buffer.  Because this process is interface-specific, the
   service routine does nothing (other than validate) in this phase.  It is up
   to the caller to transition from the data phase to the end phase when the
   transfer is complete.

   If an operation is completed, or an error has occurred, the controller state
   on return will be either idle or waiting, instead of busy.  The caller should
   check the controller status to determine if normal completion or error
   recovery is appropriate.

   If the command is continuing, the service activation time will be set
   appropriately.  The caller should then call sim_activate to schedule the next
   service and clear the "wait" field in preparation for the next service call.


   Implementation notes:

    1. The Cold Load Read and Seek commands check only the drive's Not Ready
       status because seeking clears a Seek Check.  The other commands that
       access the unit (e.g., Read and Write) have already checked in the
       command start routine for Not Ready, Seek Check, or Fault status and
       terminated with a Status-2 error.

    2. Several commands (e.g., Set File Mask, Address Record) are executed
       completely within the dl_start_command routine, so all we do here is
       finish the command with the expected status.  The service routine is
       called only to provide the proper command execution delay.

    3. If a host file system error occurs, the service routine returns SCPE_IERR
       to stop simulation.  If simulation is resumed, the controller will behave
       as though an uncorrectable data error had occurred.
*/

t_stat dl_service_drive (CVPTR cvptr, UNIT *uptr)
{
t_stat result = SCPE_OK;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OP;

switch ((CNTLR_PHASE) uptr->PHASE) {                    /* dispatch the phase */

    case start_phase:
        switch (opcode) {                               /* dispatch the current operation */

            case Recalibrate:
            case Seek:
                if (start_seek (cvptr, uptr, opcode, end_phase)     /* start the seek; if it succeeded, */
                  && (cvptr->type == MAC))                          /*   and this a MAC controller, */
                    dl_idle_controller (cvptr);                     /*     then go idle until it completes */
                break;


            case Cold_Load_Read:
                if (start_seek (cvptr, uptr, Read, start_phase))    /* start the seek; did it succeed? */
                    cvptr->file_mask = DL_FSPEN;                    /* set sparing enabled now */
                break;


            case Read:
            case Read_With_Offset:
            case Read_Without_Verify:
                cvptr->length = DL_WPSEC;               /* transfer just the data */
                result = start_read (cvptr, uptr);      /* start the sector read */
                break;


            case Read_Full_Sector:
                cvptr->length = DL_WPFSEC;              /* transfer the header/data/trailer */
                result = start_read (cvptr, uptr);      /* start the sector read */
                break;


            case Verify:
                cvptr->length = 0;                                  /* no data transfer needed */
                result = start_read (cvptr, uptr);                  /* start the sector read */

                if (uptr->PHASE == data_phase) {                    /* did the read start successfully? */
                    uptr->PHASE = end_phase;                        /* skip the data phase */
                    uptr->wait = cvptr->sector_time                 /* reschedule for the intersector time */
                                   + cvptr->data_time * DL_WPSEC;   /*   plus the data read time */
                    }
                break;


            case Write:
            case Initialize:
                cvptr->length = DL_WPSEC;               /* transfer just the data */
                start_write (cvptr, uptr);              /* start the sector write */
                break;


            case Write_Full_Sector:
                cvptr->length = DL_WPFSEC;              /* transfer the header/data/trailer */
                start_write (cvptr, uptr);              /* start the sector write */
                break;


            case Request_Status:
            case Request_Sector_Address:
            case Clear:
            case Address_Record:
            case Request_Syndrome:
            case Set_File_Mask:
            case Load_TIO_Register:
            case Request_Disc_Address:
            case End:
            case Wakeup:
                dl_service_controller (cvptr, uptr);    /* the controller service handles these */
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of start phase handlers */


    case data_phase:
        switch (opcode) {                               /* dispatch the current operation */
            case Read:
            case Read_Full_Sector:
            case Read_With_Offset:
            case Read_Without_Verify:
            case Write:
            case Write_Full_Sector:
            case Initialize:
                break;                                  /* data transfers are handled by the caller */


            default:                                    /* entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of data phase handlers */


    case end_phase:
        switch (opcode) {                               /* dispatch the operation command */

            case Recalibrate:
            case Seek:
                if (cvptr->type == ICD)                         /* is this an ICD controller? */
                    dl_end_command (cvptr, drive_attention);    /* seeks end with Drive Attention status */
                else                                            /* if not an ICD controller, */
                    uptr->STAT |= DL_S2ATN;                     /*   set Attention in the unit status */
                break;


            case Read:
            case Read_Full_Sector:
            case Read_With_Offset:
                end_read (cvptr, uptr);                 /* end the sector read */
                break;


            case Read_Without_Verify:
                if (cvptr->sector == 0)                 /* have we reached the end of the track? */
                    uptr->OP = Read;                    /* begin verifying the next time */

                end_read (cvptr, uptr);                 /* end the sector read */
                break;


            case Verify:
                cvptr->verify_count =                   /* decrement the count */
                  (cvptr->verify_count - 1) & DMASK;    /*   modulo 65536 */

                if (cvptr->verify_count == 0)           /* are there more sectors to verify? */
                    cvptr->eod = SET;                   /* no, so terminate the command cleanly */

                end_read (cvptr, uptr);                 /* end the sector read */
                break;


            case Write:
            case Write_Full_Sector:
            case Initialize:
                result = end_write (cvptr, uptr);       /* end the sector write */
                break;


            case Request_Status:
            case Request_Sector_Address:
            case Request_Disc_Address:
                dl_service_controller (cvptr, uptr);    /* the controller service handles these */
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of end phase handlers */
    }                                                   /* end of phase dispatch */

return result;                                          /* return the result of the service */
}


/* Service the controller unit.

   The controller service routine is called to execute scheduled controller
   commands that do not access drive units.  It is also called to obtain command
   parameters from the interface and to return command result values to the
   interface.  The actions to be taken depend on the current state of the
   controller.

   Controller commands are scheduled on a separate unit to allow concurrent
   processing while seeks are in progress.  For example, a seek may be started
   on unit 0.  While the seek is in progress, the CPU may request status from
   the controller.  In between returning the first and second status words to
   the CPU, the seek may complete.  Separating the controller unit allows seek
   completion to be handled while the controller is "busy" waiting for the CPU
   to indicate that it is ready for the second word.

   For ICD controllers, the controller unit is not used, and all commands are
   scheduled on the drive unit.  This is possible because ICD controllers always
   wait for seeks to complete before executing additional commands.  To reduce
   code duplication, however, the drive unit service calls the controller
   service directly to handle controller commands.

   The service routine validates phase assignments and returns SCPE_IERR
   (Internal Error) if entry is made with an illegal operation phase or a phase
   that is not valid for a given operation.

   Implementation notes:

    1. While the interface simulator is responsible for data phase transfers,
       the controller service routine is responsible for (re)starting and
       stopping the command wait timer for each parameter sent to and received
       from the interface.
*/

t_stat dl_service_controller (CVPTR cvptr, UNIT *uptr)
{
t_stat result = SCPE_OK;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OP;

switch ((CNTLR_PHASE) uptr->PHASE) {                    /* dispatch the phase */

    case start_phase:
    case end_phase:
        switch (opcode) {                               /* dispatch the current operation */
            case Request_Status:
                dl_end_command (cvptr, cvptr->status);  /* the command completes with no status change */
                break;


            case Clear:
                dl_clear_controller (cvptr, uptr, soft_clear);  /* clear the controller */
                dl_end_command (cvptr, normal_completion);      /* the command is complete */
                break;


            case Request_Sector_Address:
            case Address_Record:
            case Request_Syndrome:
            case Set_File_Mask:
            case Load_TIO_Register:
            case Request_Disc_Address:
                dl_end_command (cvptr, normal_completion);      /* the command is complete */
                break;


            case End:
                dl_idle_controller (cvptr);             /* the command completes with the controller idle */
                break;


            case Wakeup:
                dl_end_command (cvptr, unit_available); /* the command completes with Unit Available status */
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of start and end phase handlers */


    case data_phase:
        switch (opcode) {                               /* dispatch the current operation */

            case Seek:
            case Verify:
            case Address_Record:
            case Read_With_Offset:
            case Load_TIO_Register:
                if (cvptr->length > 1)                  /* at least one more parameter to input? */
                    set_timer (cvptr, SET);             /* restart the timer for the next parameter */
                else                                    /* this is the last one */
                    set_timer (cvptr, CLEAR);           /*   so stop the command wait timer */
                break;


            case Request_Status:
            case Request_Sector_Address:
            case Request_Syndrome:
            case Request_Disc_Address:
                if (cvptr->length > 0)                  /* at least one more to parameter output? */
                    set_timer (cvptr, SET);             /* restart the timer for the next parameter */
                else                                    /* this is the last one */
                    set_timer (cvptr, CLEAR);           /*   so stop the command wait timer */
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of data phase handlers */
    }                                                   /* end of phase dispatch */

return result;                                          /* return the result of the service */
}


/* Service the command wait timer unit.

   The command wait timer service routine is called if the command wait timer
   expires.  This indicates that the CPU did not respond to a parameter transfer
   or did not issue a new command within the ~1.8 second timeout period.  The
   timer is used with the MAC controller to ensure that a hung CPU does not tie
   up the controller, preventing it from servicing other CPUs or drives.  ICD
   controllers do not use the command wait timer; they will wait forever, as
   each controller is dedicated to a single interface.

   When a timeout occurs, the controller unit is cancelled in case the cause was
   a parameter timeout.  Then the file mask is reset, and the controller is
   idled.

   The interface is responsible for polling for a new command and for drive
   attention when a timeout occurs.

   Implementation notes:

    1. Only the controller unit may be active when the command wait timer
       expires.  A unit is never active because the timer is cancelled when
       commands are executing and is restarted after the command completes.
*/

t_stat dl_service_timer (CVPTR cvptr, UNIT *uptr)
{
sim_cancel (cvptr->aux);                                /* cancel any controller activation */

dl_idle_controller (cvptr);                             /* idle the controller */
cvptr->file_mask = 0;                                   /* clear the file mask */

return SCPE_OK;
}


/* Clear the controller.

   The controller connected to the specified unit is cleared as directed.  A MAC
   controller is connected to several units, so the unit is used to find the
   associated device and thereby the unit array.  An ICD controller is connected
   only to the specified unit.

   In hardware, four conditions clear the 13037 controller:

    - an initial application of power
    - an assertion of the CLEAR signal by the CPU interface
    - a timeout of the command wait timer
    - a programmed Clear command

   The first two conditions, called "hard clears," are equivalent and cause a
   firmware restart with the PWRON flag set.  The 13175 interface for the HP
   1000 asserts the CLEAR signal in response to the backplane CRS signal if the
   PRESET ENABLE jumper is not installed (which is the usual case).  The third
   condition also causes a firmware restart but with the PWRON flag clear.  The
   last condition is executed in the command handler and therefore returns to
   the Command Wait Loop instead of the Poll Loop.

   For a hard clear, the 13037 controller will:

    - disconnect the CPU interface
    - zero the controller RAM (no drives held, last polled unit number reset)
    - issue a Controller Preset to clear all connected drives
    - clear the clock offset
    - clear the file mask
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

   In simulation, a hard clear occurs when a RESET -P or RESET command is
   issued, or a programmed CLC 0 instruction is executed.  A soft clear occurs
   when a programmed Clear command is started.  A timeout clear occurs when the
   command wait timer unit is serviced, but this action is handled in the timer
   unit service.

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
       aborted for a hard or timeout clear, whereas in hardware it would
       complete normally.  This is OK, however, because an internal seek always
       clears the drive's Attention status on completion, so aborting the
       simulated seek is equivalent to an immediate seek completion.

    4. In simulation, a Controller Preset only resets the specified status bits,
       as the remainder of the hardware actions are not implemented.
*/

t_stat dl_clear_controller (CVPTR cvptr, UNIT *uptr, CNTLR_CLEAR clear_type)
{
uint32 unit, unit_count;
DEVICE *dptr = NULL;

if (clear_type == hard_clear) {                         /* is this a hard clear? */
    dl_idle_controller (cvptr);                         /* idle the controller */
    cvptr->file_mask = 0;                               /* clear the file mask */
    cvptr->poll_unit = 0;                               /* clear the last unit polled */
    }

if (cvptr->type == ICD)                                 /* is this an ICD controller? */
    unit_count = 1;                                     /* there is only one unit per controller */

else {                                                  /* a MAC controller clears all units */
    dptr = find_dev_from_unit (uptr);                   /* find the associated device */

    if (dptr == NULL)                                   /* the device doesn't exist?!? */
        return SCPE_IERR;                               /* this is an impossible condition! */
    else                                                /* the device was found */
        unit_count = dptr->numunits;                    /*   so get the number of units */
    }

for (unit = 0; unit < unit_count; unit++) {             /* loop through the unit(s) */
    if (dptr)                                           /* pick up the unit from the device? */
        uptr = dptr->units + unit;                      /* yes, so get the next unit */

    if (!(uptr->flags & UNIT_DIS)) {                    /* is the unit enabled? */
        if (clear_type == hard_clear                    /* a hard clear cancels */
          && uptr->OP != Seek                           /*   only if not seeking */
          && uptr->OP != Recalibrate)                   /*     or recalibrating */
            sim_cancel (uptr);                          /* cancel the service */

        uptr->STAT &= ~DL_S2CPS;                        /* do "Controller Preset" for the unit */
        }
    }

return SCPE_OK;
}


/* Idle the controller.

   The command wait timer is turned off, the status is reset, and the controller
   is returned to the idle state (Poll Loop).
*/

void dl_idle_controller (CVPTR cvptr)
{
cvptr->state = cntlr_idle;                              /* idle the controller */
cvptr->status = normal_completion;                      /* the Poll Loop clears the status */

set_timer (cvptr, CLEAR);                               /* stop the command wait timer */
return;
}



/* Load or unload the drive heads.

   In hardware, a drive's heads are loaded when a disc pack is installed and the
   RUN/STOP switch is set to RUN.  The drive reports First Status when the heads
   load to indicate that the pack has potentially changed.  Setting the switch
   to STOP unloads the heads.  When the heads are unloaded, the drive reports
   Not Ready and Drive Busy status.

   In simulation, the unit must be attached before the heads may be unloaded or
   loaded.  As the heads should be automatically loaded when a unit is attached
   and unloaded when a unit is detached, this routine must be called after
   attaching and before detaching.


   Implementation notes:

    1. The drive sets its Attention status bit when the heads load or unload.
       However, the ICD controller reports Attention only for head unloading.

    2. Loading or unloading the heads clears Fault and Seek Check status.

    3. If we are called during a RESTORE command, the unit's flags are not
       changed to avoid upsetting the state that was SAVEd.
*/

t_stat dl_load_unload (CVPTR cvptr, UNIT *uptr, t_bool load)
{
if ((uptr->flags & UNIT_ATT) == 0)                      /* the unit must be attached to [un]load */
    return SCPE_UNATT;                                  /* return "Unit not attached" if not */

else if (!(sim_switches & SIM_SW_REST))                 /* modify the flags only if not restoring */
    if (load) {                                         /* are we loading the heads? */
        uptr->flags = uptr->flags & ~UNIT_UNLOAD;       /* clear the unload flag */
        uptr->STAT = DL_S2FS;                           /*   and set First Status */

        if (cvptr->type != ICD)                         /* if this is not an ICD controller */
            uptr->STAT |= DL_S2ATN;                     /*   set Attention status also */
        }

    else {                                              /* we are unloading the heads */
        uptr->flags = uptr->flags | UNIT_UNLOAD;        /* set the unload flag */
        uptr->STAT = DL_S2ATN;                          /*   and Attention status */
        }

return SCPE_OK;
}



/* Disc library global utility routines */


/* Classify the current controller opcode.

   The controller opcode is classified as a read, write, control, or status
   command, and the classification is returned to the caller.  If the opcode is
   illegal or undefined for the indicated controller, the classification is
   marked as invalid.
*/

CNTLR_CLASS dl_classify (CNTLR_VARS cntlr)
{
if (cntlr.type <= last_type                             /* if the controller type is legal */
  && cntlr.opcode <= Last_Opcode                        /*   and the opcode is legal */
  && cmd_props [cntlr.opcode].valid [cntlr.type])       /*   and is defined for this controller, */
    return cmd_props [cntlr.opcode].classification;     /*     then return the command classification */
else                                                    /* the type or opcode is illegal */
    return class_invalid;                               /*   so return an invalid classification */
}


/* Return the name of an opcode.

   A string representing the supplied controller opcode is returned to the
   caller.  If the opcode is illegal or undefined for the indicated controller,
   the string "invalid" is returned.
*/

const char *dl_opcode_name (CNTLR_TYPE controller, CNTLR_OPCODE opcode)
{
if (controller <= last_type                             /* if the controller type is legal */
  && opcode <= Last_Opcode                              /*   and the opcode is legal */
  && cmd_props [opcode].valid [controller])             /*   and is defined for this controller, */
    return opcode_name [opcode];                        /*     then return the opcode name */
else                                                    /* the type or opcode is illegal, */
    return invalid_name;                                /*   so return an error indication */
}


/* Return the name of a command phase.

   A string representing the supplied phase is returned to the caller.  If the
   phase is illegal, the string "invalid" is returned.
*/

const char *dl_phase_name (CNTLR_PHASE phase)
{
if (phase <= last_phase)                                /* if the phase is legal, */
    return phase_name [phase];                          /*   return the phase name */
else                                                    /* the phase is illegal, */
    return invalid_name;                                /*   so return an error indication */
}



/* Disc library global VM routines */


/* Attach a disc image file to a unit.

   The file specified by the supplied filename is attached to the indicated
   unit.  If the attach was successful, the heads are loaded on the drive.

   If the drive is set to autosize, the size of the image file is compared to
   the table of drive capacities to determine which model of drive was used to
   create it.  If the image file is new, then the previous drive model is
   retained.
*/

t_stat dl_attach (CVPTR cvptr, UNIT *uptr, char *cptr)
{
uint32 id, size;
t_stat result;

result = attach_unit (uptr, cptr);                          /* attach the unit */

if (result != SCPE_OK)                                      /* did the attach fail? */
    return result;                                          /* yes, so return the error status */

dl_load_unload (cvptr, uptr, TRUE);                         /* if the attach succeeded, load the heads */

if (uptr->flags & UNIT_AUTO) {                              /* is autosizing enabled? */
    size = sim_fsize (uptr->fileref) / sizeof (uint16);     /* get the file size in words */

    if (size > 0)                                           /* a new file retains the current drive model */
        for (id = 0; id < PROPS_COUNT; id++)                /* find the best fit to the drive models */
            if (size <= drive_props [id].words              /* if the file size fits the drive capacity */
              || id == PROPS_COUNT - 1) {                   /*   or this is the largest available drive */
                uptr->capac = drive_props [id].words;       /*     then set the capacity */
                uptr->flags = (uptr->flags & ~UNIT_MODEL)   /*       and the model */
                  | SET_MODEL (id);
                break;
                }
    }

return SCPE_OK;                                             /* the unit was successfully attached */
}


/* Detach a disc image file from a unit.

   The heads are unloaded on the drive, and the attached file, if any, is
   detached.
*/

t_stat dl_detach (CVPTR cvptr, UNIT *uptr)
{
dl_load_unload (cvptr, uptr, FALSE);                    /* unload the heads if attached */
return detach_unit (uptr);                              /*   and detach the unit */
}


/* Set the drive model.

   This validation routine is called to set the model of disc drive associated
   with the specified unit.  The "value" parameter indicates the model ID, and
   the unit capacity is set to the size indicated.
*/

t_stat dl_set_model (UNIT *uptr, int32 value, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)                                 /* we cannot alter the disc model */
    return SCPE_ALATT;                                      /*   if the unit is attached */

if (value != UNIT_AUTO)                                     /* if we are not autosizing */
    uptr->capac = drive_props [GET_MODEL (value)].words;    /*   set the capacity to the new value */

return SCPE_OK;
}



/* Disc library local controller routines */


/* Start a read operation on the current sector.

   The current sector indicated by the controller address is read from the disc
   image file into the sector buffer in preparation for data transfer to the
   CPU.  If the end of the track had been reached, and the file mask permits,
   an auto-seek is scheduled instead to allow the read to continue.

   On entry, the end-of-data flag is checked.  If it is set, the current read is
   completed.  Otherwise, the buffer data offset and verify options are set up.
   For a Read Full Sector, the sync word is set from the controller type, and
   dummy cylinder and head-sector words are generated from the current location
   (as would be the case in the absence of track sparing).

   The image file is positioned to the correct sector in preparation for
   reading.  If the positioning requires a permitted seek, it is scheduled, and
   the routine returns with the operation phase unchanged to wait for seek
   completion before resuming the read (when the seek completes, the service
   routine will be entered, and we will be called again; this time, the
   end-of-cylinder flag will be clear and positioning will succeed).  If
   positioning resulted in an error, the current read is terminated with the
   error status set.

   If positioning succeeded within the same cylinder, the sector image is read
   into the buffer at an offset determined by the operation (Read Full Sector
   leaves room at the start of the buffer for the sector header).  If the image
   file read did not return a full sector, the remainder of the buffer is padded
   with zeros.  If the image read failed with a file system error, SCPE_IOERR is
   returned from the service routine to cause a simulation stop; resumption is
   handled as an Uncorrectable Data Error.

   If the image was read correctly, the next sector address is updated, the
   operation phase is set for the data transfer, and the index of the first word
   to transfer is set.


   Implementation notes:

    1. The length of the transfer required (cvptr->length) must be set before
       entry.

    2. Entry while executing a Read Without Verify or Read Full Sector command
       inhibits address verification.  The unit opcode is tested instead of the
       controller opcode because a Read Without Verify is changed to a Read to
       begin verifying after a track switch occurs.
*/

static t_stat start_read (CVPTR cvptr, UNIT *uptr)
{
uint32 count, offset;
t_bool verify;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OP;

if (cvptr->eod == SET) {                                /* is the end of data indicated? */
    dl_end_command (cvptr, normal_completion);          /* complete the command */
    return SCPE_OK;
    }

if (opcode == Read_Full_Sector) {                       /* are we starting a Read Full Sector command? */
    if (cvptr->type == ICD)                             /* is this an ICD controller? */
        cvptr->buffer [0] = 0100377;                    /* ICD does not support ECC */
    else
        cvptr->buffer [0] = 0100376;                    /* MAC does support ECC */

    set_address (cvptr, 1);                             /* set the current address into buffer 1-2 */
    offset = 3;                                         /* start the data after the header */
    verify = FALSE;                                     /* set for no address verification */
    }

else {                                                  /* it's another read command */
    offset = 0;                                         /* data starts at the beginning */
    verify = (opcode != Read_Without_Verify);           /* set for address verification unless it's a RWV */
    }

if (! position_sector (cvptr, uptr, verify))            /* position the sector */
    return SCPE_OK;                                     /* a seek is in progress or an error occurred */

count = sim_fread (cvptr->buffer + offset,              /* read the sector from the image */
                   sizeof (uint16), DL_WPSEC,           /*   into the sector buffer */
                   uptr->fileref);

for (count = count + offset; count < cvptr->length; count++)    /* pad the sector as needed */
    cvptr->buffer [count] = 0;                                  /*   e.g., if reading from a new file */

if (ferror (uptr->fileref))                             /* did a host file system error occur? */
    return io_error (cvptr, uptr);                      /* set up the data error status and stop the simulation */

next_sector (cvptr, uptr);                              /* address the next sector */

uptr->PHASE = data_phase;                               /* set up the data transfer phase */
cvptr->index = 0;                                       /* reset the data index */

return SCPE_OK;                                         /* the read was successfully started */
}


/* Finish a read operation on the current sector.

   On entry, the end-of-data flag is checked.  If it is set, the current read is
   completed.  Otherwise, the command phase is reset to start the next sector,
   and the disc service is set to allow for the intersector delay.


   Implementation notes:

    1. The CPU indicates the end of a read data transfer to an ICD controller by
       untalking the drive.  The untalk is done by the driver as soon as the
       DCPC completion interrupt is processed.  However, the time from the final
       DCPC transfer through driver entry to the point where the untalk is
       asserted on the bus varies from 80 instructions (RTE-6/VM with OS
       microcode and the buffer in the system map) to 152 instructions (RTE-IVB
       with the buffer in the user map).  The untalk must occur before the start
       of the next sector, or the drive will begin the data transfer.

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
       command.
*/

static void end_read (CVPTR cvptr, UNIT *uptr)
{
uint32 limit;

if (cvptr->eod == SET)                                  /* is the end of data indicated? */
    dl_end_command (cvptr, normal_completion);          /* complete the command */

else {                                                  /* reading continues */
    uptr->PHASE = start_phase;                          /* reset to the start phase */
    uptr->wait = cvptr->sector_time;                    /* delay for the intersector time */

    if (cvptr->eoc == SET && cvptr->type == ICD) {      /* seek will be required and controller is ICD? */
        if (!(cvptr->file_mask & DL_FAUTSK))            /* if auto-seek is disabled */
            limit = cvptr->cylinder;                    /*   then the limit is the current cylinder */
        else if (cvptr->file_mask & DL_FDECR)           /* else if enabled and decremental seek */
            limit = 0;                                  /*   then the limit is cylinder 0 */
        else                                            /* else the enabled limit is the last cylinder */
            limit = drive_props [GET_MODEL (uptr->flags)].cylinders;

        if (cvptr->cylinder == limit)                   /* is positioner at the limit? */
            uptr->wait = cvptr->eot_time;               /* seek will fail; delay to allow CPU to untalk */
        }
    }

return;
}


/* Start a write operation on the current sector.

   The current sector indicated by the controller address is positioned for
   writing from the sector buffer to the disc image file after data transfer
   from the CPU.  If the end of the track had been reached, and the file mask
   permits, an auto-seek is scheduled instead to allow the write to continue.

   On entry, if writing is not permitted, or formatting is required but not
   enabled, the command is terminated with an error.  Otherwise, the disc image
   file is positioned to the correct sector in preparation for writing.

   If the positioning requires a permitted seek, it is scheduled, and the
   routine returns with the operation phase unchanged to wait for seek
   completion before resuming the write (when the seek completes, the service
   routine will be entered, and we will be called again; this time, the
   end-of-cylinder flag will be clear and positioning will succeed).  If
   positioning resulted in an error, the current write is terminated with the
   error status set.

   If positioning succeeded within the same cylinder, the operation phase is set
   for the data transfer, and the index of the first word to transfer is set.


   Implementation notes:

    1. Entry while executing a Write Full Sector or Initialize command inhibits
       address verification.  In addition, the drive's FORMAT switch must be set
       to the enabled position for these commands to succeed.
*/

static void start_write (CVPTR cvptr, UNIT *uptr)
{
const t_bool verify = (CNTLR_OPCODE) uptr->OP == Write; /* only Write verifies the sector address */

if ((uptr->flags & UNIT_WPROT)                          /* is the unit write protected, */
  || !verify && !(uptr->flags & UNIT_FMT))              /*   or is formatting required but not enabled? */
    dl_end_command (cvptr, status_2_error);             /* terminate the write with an error */

else if (position_sector (cvptr, uptr, verify)) {       /* writing is permitted; position the sector */
    uptr->PHASE = data_phase;                           /* positioning succeeded; set up data transfer phase */
    cvptr->index = 0;                                   /* reset the data index */
    }

return;
}


/* Finish a write operation on the current sector.

   The current sector is written from the sector buffer to the disc image file
   at the current file position.  The next sector address is then updated to
   allow writing to continue.

   On entry, the drive is checked to ensure that it is ready for the write.
   Then the sector buffer is padded appropriately if a full sector of data was
   not transferred.  The buffer is written to the disc image file at the
   position corresponding to the controller address as set when the sector was
   started.  The write begins at a buffer offset determined by the command (a
   Write Full Sector has header words at the start of the buffer that are not
   written to the disc image).

   If the image write failed with a file system error, SCPE_IOERR is returned
   from the service routine to cause a simulation stop; resumption is handled as
   an Uncorrectable Data Error.  If the image was written correctly, the next
   sector address is updated.  If the end-of-data flag is set, the current write
   is completed.  Otherwise, the command phase is reset to start the next
   sector, and the disc service is scheduled to allow for the intersector delay.


   Implementation notes:

    1. A partial sector is filled with 177777B words (ICD) or copies of the last
       word (MAC) per page 7-10 of the ICD/MAC Disc Diagnostic manual.
*/

static t_stat end_write (CVPTR cvptr, UNIT *uptr)
{
uint32 count;
uint16 pad;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OP;
const uint32 offset = (opcode == Write_Full_Sector ? 3 : 0);

if (uptr->flags & UNIT_UNLOAD) {                        /* if the drive is not ready, */
    dl_end_command (cvptr, access_not_ready);           /*   terminate the command with an error */
    return SCPE_OK;
    }

if (cvptr->index < DL_WPSEC + offset) {                 /* was a partial sector transferred? */
    if (cvptr->type == ICD)                             /* an ICD controller */
        pad = DMASK;                                    /*   pads the sector with -1 */
    else                                                /* a MAC controller */
        pad = cvptr->buffer [cvptr->index - 1];         /*   pads with the last word written */

    for (count = cvptr->index; count < DL_WPSEC + offset; count++)
        cvptr->buffer [count] = pad;                    /* pad the sector buffer as needed */
    }

sim_fwrite (cvptr->buffer + offset, sizeof (uint16),    /* write the sector to the file */
            DL_WPSEC, uptr->fileref);

if (ferror (uptr->fileref))                             /* did a host file system error occur? */
    return io_error (cvptr, uptr);                      /* set up the data error status and stop the simulation */

next_sector (cvptr, uptr);                              /* address the next sector */

if (cvptr->eod == SET)                                  /* is the end of data indicated? */
    dl_end_command (cvptr, normal_completion);          /* complete the command */

else {                                                  /* writing continues */
    uptr->PHASE = start_phase;                          /* reset to the start phase */
    uptr->wait = cvptr->sector_time;                    /* delay for the intersector time */
    }

return SCPE_OK;
}


/* Position the disc image file at the current sector.

   The image file is positioned at the byte address corresponding to the drive's
   current cylinder and the controller's current head and sector addresses.
   Positioning may involve an auto-seek if a prior read or write addressed the
   final sector of a cylinder.  If a seek is initiated or an error is detected,
   the routine returns FALSE to indicate that the positioning was not performed.
   If the file was positioned, the routine returns TRUE.

   On entry, if the controller's end-of-cylinder flag is set, a prior read or
   write addressed the final sector in the current cylinder.  If the file mask
   does not permit auto-seeking, the current command is terminated with an End
   of Cylinder error.  Otherwise, the cylinder is incremented or decremented as
   directed by the file mask, and a seek to the new cylinder is started.

   If the increment or decrement resulted in an out-of-bounds value, the seek
   will return Seek Check status, and the command is terminated with an error.
   If the seek is legal, the routine returns with the disc service scheduled for
   seek completion and the command state unchanged.  When the service is
   reentered, the read or write will continue on the new cylinder.

   If the EOC flag was not set, the drive's position is checked against the
   controller's position if address verification is requested.  If they are
   different (as may occur with an Address Record command that specified a
   different location than the last Seek command), a seek is started to the
   correct cylinder, and the routine returns with the disc service scheduled for
   seek completion as above.

   If the drive and controller positions agree or verification is not requested,
   the CHS addresses are validated against the drive limits.  If they are
   invalid, Seek Check status is set, and the command is terminated with an
   error.

   If the addresses are valid, the drive is checked to ensure that it is ready
   for positioning.  If it is, the the byte offset in the image file is
   calculated from the CHS address, and the file is positioned.  The disc
   service is scheduled to begin the data transfer, and the routine returns TRUE
   to indicate that the file position was set.


   Implementation notes:

    1. The ICD controller returns an End of Cylinder error if an auto-seek
       results in a position beyond the drive limits.  The MAC controller
       returns a Status-2 error.  Both controllers set the Seek Check bit in the
       drive status word.
*/

static t_bool position_sector (CVPTR cvptr, UNIT *uptr, t_bool verify)
{
uint32 block;
uint32 model = GET_MODEL (uptr->flags);

if (cvptr->eoc == SET)                                          /* are we at the end of a cylinder? */
    if (cvptr->file_mask & DL_FAUTSK) {                         /* is an auto-seek allowed? */
        if (cvptr->file_mask & DL_FDECR)                        /* is a decremental seek requested? */
            cvptr->cylinder = (cvptr->cylinder - 1) & DMASK;    /* decrease the cylinder address with wraparound */
        else                                                    /* an incremental seek is requested */
            cvptr->cylinder = (cvptr->cylinder + 1) & DMASK;    /* increase the cylinder address with wraparound */

        start_seek (cvptr, uptr,                                /* start the auto-seek */
                   (CNTLR_OPCODE) uptr->OP,                     /*   with the current operation */
                   (CNTLR_PHASE) uptr->PHASE);                  /*     and phase unchanged */

        if (uptr->STAT & DL_S2SC)                               /* did a seek check occur? */
            if (cvptr->type == ICD)                             /* is this ICD controller? */
                dl_end_command (cvptr, end_of_cylinder);        /* report it as an End of Cylinder error */
            else                                                /* it is a MAC controller */
                dl_end_command (cvptr, status_2_error);         /* report it as a Status-2 error */
        }

    else                                                        /* the file mask does not permit an auto-seek */
        dl_end_command (cvptr, end_of_cylinder);                /*   so terminate with an EOC error */

else if (verify && (uint32) uptr->CYL != cvptr->cylinder) {     /* is the positioner on the wrong cylinder? */
    start_seek (cvptr, uptr,                                    /* start a seek to the correct cylinder */
               (CNTLR_OPCODE) uptr->OP,                         /*   with the current operation */
               (CNTLR_PHASE) uptr->PHASE);                      /*     and phase unchanged */

    if (uptr->STAT & DL_S2SC)                                   /* did a seek check occur? */
        dl_end_command (cvptr, status_2_error);                 /* report a Status-2 error */
    }

else if (((uint32) uptr->CYL >= drive_props [model].cylinders)  /* is the cylinder out of bounds? */
  || (cvptr->head >= drive_props [model].heads)                 /*   or the head? */
  || (cvptr->sector >= drive_props [model].sectors)) {          /*   or the sector? */
    uptr->STAT = uptr->STAT | DL_S2SC;                          /* set Seek Check status */
    dl_end_command (cvptr, status_2_error);                     /*   and terminate with an error */
    }

else if (uptr->flags & UNIT_UNLOAD)                     /* is the drive ready for positioning? */
    dl_end_command (cvptr, access_not_ready);           /* terminate the command with an access error */

else {                                                  /* we are ready to position the image file */
    block = TO_BLOCK (uptr->CYL, cvptr->head,           /* calculate the new block position */
                      cvptr->sector, model);            /*   (for inspection only) */
    uptr->pos = TO_OFFSET (block);                      /*     and then convert to a byte offset */

    sim_fseek (uptr->fileref, uptr->pos, SEEK_SET);     /* set the image file position */

    uptr->wait = cvptr->data_time;                      /* delay for the data access time */
    return TRUE;                                        /*   and report that positioning was accomplished */
    }

return FALSE;                                           /* report that positioning failed or was deferred */
}


/* Address the next sector.

   The controller's CHS address is incremented to point at the next sector.  If
   the next sector number is valid, the routine returns.  Otherwise, the sector
   number is reset to sector 0.  If the file mask is set for cylinder mode, the
   head is incremented, and if the new head number is valid, the routine
   returns.  If the head number is invalid, it is reset to head 0, and the
   end-of-cylinder flag is set.  The EOC flag is also set if the file mask is
   set for surface mode.

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
const uint32 model = GET_MODEL (uptr->flags);           /* get the disc model */

cvptr->sector = cvptr->sector + 1;                      /* increment the sector number */

if (cvptr->sector < drive_props [model].sectors)        /* are we at the end of the track? */
    return;                                             /* no, so the next sector value is OK */

cvptr->sector = 0;                                      /* wrap the sector number */

if (cvptr->file_mask & DL_FCYLM) {                      /* are we in cylinder mode? */
    cvptr->head = cvptr->head + 1;                      /* yes, so increment the head */

    if (cvptr->head < drive_props [model].heads)        /* are we at the end of the cylinder? */
        return;                                         /* no, so the next head value is OK */

    cvptr->head = 0;                                    /* wrap the head number */
    }

cvptr->eoc = SET;                                       /* set the end-of-cylinder flag to */
return;                                                 /*   indicate that an update is required */
}


/* Start a seek.

   A seek is initiated on the indicated unit if the drive is ready and the
   cylinder, head, and sector values in the controller are valid for the current
   drive model.  If the current operation is a recalibrate, a seek is initiated
   to cylinder 0 instead of the cylinder value stored in the controller.  The
   routine returns TRUE if the drive was ready for the seek and FALSE if it was
   not.

   If the controller cylinder is beyond the drive's limit, Seek Check status is
   set in the unit, and the heads are not moved.  Otherwise, the relative
   cylinder position change is calculated, and the heads are moved to the new
   position.

   If the controller head or sector is beyond the drive's limit, Seek Check
   status is set in the unit.  Otherwise, Seek Check status is cleared, and the
   new file offset is calculated.

   A seek check terminates the current command for an ICD controller.  For a MAC
   controller, the seek check is noted in the drive status, but processing will
   continue until the drive sets Attention status.

   Finally, the drive operation and phase are set to the supplied values before
   returning.


   Implementation notes:

    1. EOC is not reset for recalibrate so that a reseek will return to the same
       location as was current when the recalibrate was done.

    2. Calculation of the file offset is performed here simply to keep the unit
       position register available for inspection.  The actual file positioning
       is done in position_sector.

    3. In hardware, a seek to the current location will set Drive Busy status
       for 1.3 milliseconds (the head settling time).  In simulation, disc
       service is scheduled as though a one-cylinder seek was requested.
*/

static t_bool start_seek (CVPTR cvptr, UNIT *uptr, CNTLR_OPCODE next_opcode, CNTLR_PHASE next_phase)
{
int32 delta;
uint32 block, target_cylinder;
const uint32 model = GET_MODEL (uptr->flags);           /* get the drive model */

if (uptr->flags & UNIT_UNLOAD) {                        /* are the heads unloaded? */
    dl_end_command (cvptr, status_2_error);             /* the seek ends with Status-2 error */
    return FALSE;                                       /*   as the drive was not ready */
    }

if ((CNTLR_OPCODE) uptr->OP == Recalibrate)             /* is the unit recalibrating? */
    target_cylinder = 0;                                /* seek to cylinder 0 and don't reset the EOC flag */

else {                                                  /* it's a Seek command or an auto-seek request */
    target_cylinder = cvptr->cylinder;                  /* seek to the controller cylinder */
    cvptr->eoc = CLEAR;                                 /* clear the end-of-cylinder flag */
    }

if (target_cylinder >= drive_props [model].cylinders) { /* is the cylinder out of bounds? */
    delta = 0;                                          /* don't change the positioner */
    uptr->STAT = uptr->STAT | DL_S2SC;                  /*   and set Seek Check status */
    }

else {                                                  /* the cylinder value is OK */
    delta = abs (uptr->CYL - (int32) target_cylinder);  /* calculate the relative movement */
    uptr->CYL = target_cylinder;                        /*   and move the positioner */

    if ((cvptr->head >= drive_props [model].heads)          /* if the head */
      || (cvptr->sector >= drive_props [model].sectors))    /*   or the sector is out of bounds, */
        uptr->STAT = uptr->STAT | DL_S2SC;                  /*     set Seek Check status */

    else {                                              /* the head and sector are OK */
        uptr->STAT = uptr->STAT & ~DL_S2SC;             /* clear Seek Check status */

        block = TO_BLOCK (uptr->CYL, cvptr->head,       /* set up the new block position */
                          cvptr->sector, model);        /*   (for inspection only) */
        uptr->pos = TO_OFFSET (block);                  /*     and then convert to a byte offset */
        }
    }

if ((uptr->STAT & DL_S2SC) && cvptr->type == ICD)       /* did a Seek Check occur for an ICD controller? */
    dl_end_command (cvptr, status_2_error);             /* the command ends with a Status-2 error */

else {                                                  /* the seek was OK or this is a MAC controller */
    if (delta == 0)                                     /* if the seek is to the same cylinder, */
        delta = 1;                                      /*   then schedule as a one-cylinder seek */

    uptr->wait = cvptr->seek_time * delta;              /* the seek delay is based on the relative movement */
    }

uptr->OP    = next_opcode;                              /* set the next operation */
uptr->PHASE = next_phase;                               /*   and command phase */
return TRUE;                                            /*     and report that the drive was ready */
}


/* Report an I/O error.

   Errors indicated by the host file system are reported to the console, and
   simulation is stopped with an "I/O error" message.  If the simulation is
   continued, the CPU will receive an Uncorrectable Data Error indication from
   the controller.
*/

static t_stat io_error (CVPTR cvptr, UNIT *uptr)
{
dl_end_command (cvptr, uncorrectable_data_error);       /* terminate the command with an error */

perror ("DiscLib I/O error");                           /* report the error to the console */
clearerr (uptr->fileref);                               /*   and clear the error in case we resume */

return SCPE_IOERR;                                      /* return an I/O error to stop the simulator */
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
cvptr->buffer [index] = cvptr->cylinder + (cvptr->eoc == SET ? 1 : 0);  /* update the cylinder if EOC is set */
cvptr->buffer [index + 1] = SET_HEAD (cvptr) | SET_SECTOR (cvptr);      /* merge the head and sector */
return;
}


/* Start or stop the command wait timer.

   A MAC controller uses a 1.8 second timer to ensure that it does not wait
   forever for a non-responding disc drive or CPU interface.  In simulation, MAC
   interfaces supply an auxiliary timer unit that is activated when the command
   wait timer is started and cancelled when the timer is stopped.

   ICD interfaces do not use the command wait timer or supply an auxiliary unit.


   Implementation notes:

    1. Absolute activation is used because the timer is restarted between
       parameter word transfers.
*/

static void set_timer (CVPTR cvptr, FLIP_FLOP action)
{
if (cvptr->type == MAC)                                 /* is this a MAC controller? */
    if (action == SET)                                  /* should we start the timer? */
        sim_activate_abs (cvptr->aux + timer,           /* activate the auxiliary unit */
                          cvptr->wait_time);
    else                                                /* we stop the timer */
        sim_cancel (cvptr->aux + timer);                /*   by canceling the unit */
return;
}


/* Return the drive status (status word 2).

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

    1. The Attention, Drive Fault, First Status, and Seek Check bits are stored
       in the unit status word.  The other status bits are determined
       dynamically.

    2. The Drive Busy bit is set if the unit service is scheduled.  In hardware,
       this bit indicates that the heads are not positioned over a track, i.e.,
       that a seek is in progress.  In simulation, the only time a Request
       Status command is allowed is either when the controller is waiting for
       seek completion or for a new command.  In the latter case, unit service
       will not be scheduled, so activation can only be for seek completion.
*/

static uint16 drive_status (UNIT *uptr)
{
uint16 status;
uint32 model;

if (uptr == NULL)                                       /* if the unit is invalid */
    return DL_S2ERR | DL_S2NR;                          /*   then it does not respond */

model = GET_MODEL (uptr->flags);                        /* get the drive model */
status = drive_props [model].type | uptr->STAT;         /* start with the drive type and unit status */

if (uptr->flags & UNIT_WPROT)                           /* is the write protect switch set? */
    status |= DL_S2RO;                                  /* set the Protected status bit */

if (uptr->flags & UNIT_FMT)                             /* is the format switch enabled? */
    status |= DL_S2FMT;                                 /* set the Format status bit */

if (uptr->flags & UNIT_DIS)                             /* is the unit non-existent? */
    status |= DL_S2NR;                                  /* set the Not Ready bit */

else if (uptr->flags & UNIT_UNLOAD)                     /* are the heads unloaded? */
    status |= DL_S2NR | DL_S2BUSY;                      /* set the Not Ready and Drive Busy bits */

if (sim_is_active (uptr))                               /* is the drive positioner moving? */
    status |= DL_S2BUSY;                                /* set the Drive Busy bit */

if (status & DL_S2ERRORS)                               /* are there any Status-2 errors? */
    status |= DL_S2ERR;                                 /* set the Error bit */

return status;                                          /* return the unit status */
}
