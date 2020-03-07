/* hp2100_di_da.c: HP 12821A HP-IB Disc Interface simulator for Amigo disc drives

   Copyright (c) 2011-2018, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   DA           12821A Disc Interface with Amigo disc drives

   11-Jul-18    JDB     Revised I/O model, changed global scope of variables to local
   21-Feb-18    JDB     ATTACH -N now creates a full-size disc image
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   15-Mar-17    JDB     Changed DEBUG_PRI calls to tprintfs
   09-Mar-17    JDB     Deprecated LOCKED/WRITEENABLED for PROTECT/UNPROTECT
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   17-Jan-17    JDB     Changed to use new byte accessors in hp2100_defs.h
   13-May-16    JDB     Modified for revised SCP API function parameter types
   04-Mar-16    JDB     Name changed to "hp2100_disclib" until HP 3000 integration
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Use T_ADDR_FMT with t_addr values for 64-bit compatibility
                        Removed redundant global declarations
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   07-May-12    JDB     Cancel the intersector delay if an untalk is received
   29-Mar-12    JDB     First release
   04-Nov-11    JDB     Created DA device

   References:
     - HP 13365 Integrated Controller Programming Guide
         (13365-90901, February 1980)
     - HP 7910 Disc Drive Service Manual
         (07910-90903, April 1981)
     - 12745D Disc Controller (13037) to HP-IB Adapter Kit Installation and Service Manual
         (12745-90911, September 1983)
     - HP's 5 1/4-Inch Winchester Disc Drive Service Documentation
         (09134-90032, August 1983)
     - HP 12992 Loader ROMs Installation Manual
         (12992-90001, April 1986)
     - RTE Driver DVA32 Source
         (92084-18708, revision 2540)
     - IEEE Standard Digital Interface for Programmable Instrumentation
         (IEEE-488A-1980, September 1979)


   The HP 7906H, 7920H, and 7925H Integrated Controller Disc (ICD) drives were
   connected via an 12821A disc interface and provided 20MB, 50MB, and 120MB
   capacities.  The drives were identical to the 7906M, 7920M, and 7925M
   Multi-Access Controller (MAC) units but incorporated internal two-card
   controllers in each drive and connected to the CPU interface via the
   Hewlett-Packard Interface Bus (HP-IB), HP's implementation of IEEE-488.  Each
   controller was dedicated to a single drive and operated similarly to the
   12745 Disc Controller to HP-IB Adapter option for the 13037 Disc Controller
   chassis.  The 7906H was introduced in 1980 (there was no 7905H version, as
   the 7905 was obsolete by that time).  Up to four ICD drives could be
   connected to a single 12821A card.  The limitation was imposed by the bus
   loading and the target data transfer rate.

   The ICD command set essentially was the MAC command set modified for
   single-unit operation.  The unit number and CPU hold bit fields in the opcode
   words were unused in the ICD implementation.  The Load TIO Register, Wakeup,
   and Request Syndrome commands were removed, as Load TIO was used with the HP
   3000, Wakeup was used in a multi-CPU environment, and the simpler ICD
   controller did not support ECC.  Controller status values 02B (Unit
   Available) and 27B (Unit Unavailable) were dropped as the controller
   supported only single units, 12B (I/O Program Error) was reused to indicate
   HP-IB protocol errors, 13B (Sync Not Received) was added, and 17B (Possibly
   Correctable Data Error) was removed as error correction was not supported.

   Some minor redefinitions also occurred.  For example, status 14B (End of
   Cylinder) was expanded to include an auto-seek beyond the drive limits, and
   37B (Drive Attention) was restricted just head unloads from head loads and
   unloads.

   The command set was expanded to include several commands related to HP-IB
   operation.  These were, in large part, adapted from the Amigo disc command
   protocol outlined in the service manual for the HP 9133/34/35 series of
   5-1/4" Winchester drives.  They include the Amigo Identify and Amigo Clear
   sequences, Read and Write Loopback channel tests, and controller Self Test
   commands.

   This simulator implements the Amigo disc protocol.  It calls the 12821A Disc
   Interface (DI) simulator to send and receive bytes across the HP-IB to and
   from the CPU, and it calls the HP Disc Library to implement the controller
   functions related to disc unit operation (e.g., seek, read, write, etc.).
   Four units are provided, and any combination of 7906H/20H/25H drives may be
   defined.

   Unfortunately, the primary reference for the ICD controller (the HP 13365
   Integrated Controller Programming Guide) does not indicate parallel poll
   responses for these HP-IB commands.  Therefore, the responses have been
   derived from the sequences in the 7910 and 12745 manuals, although they
   sometimes conflict.

   The drives respond to the following commands; the secondary and opcode
   numeric values are in hex, and the bus addressing state is indicated by U
   [untalk], L [listen], and T [talk]:

     Bus  Sec  Op  Operation
     ---  ---  --  --------------------------------
      U   MSA  --  Amigo Identify

      L   00   --  Write Data
      L   08   00  Cold Load Read
      L   08   01  Recalibrate
      L   08   02  Seek
      L   08   03  Request Status
      L   08   04  Request Sector Address
      L   08   05  Read
      L   08   06  Read Full Sector
      L   08   07  Verify
      L   08   08  Write
      L   08   09  Write Full Sector
      L   08   0A  Clear
      L   08   0B  Initialize
      L   08   0C  Address Record
      L   08   0E  Read with Offset
      L   08   0F  Set File Mask
      L   08   12  Read without Verify
      L   08   14  Request Logical Disc Address
      L   08   15  End
      L   09   --  Cyclic Redundancy Check
      L   10   --  Amigo Clear
      L   1E   --  Write Loopback
      L   1F   ss  Initiate Self-Test <ss>

      T   00   --  Read Data
      T   08   --  Read Status
      T   09   --  Cyclic Redundancy Check
      T   10   --  Device Specified Jump
      T   1E   --  Read Loopback
      T   1F   --  Return Self-Test Result

   In addition, the controller responds to the Selected Device Clear primary
   (04).


   HP-IB Transaction Sequences
   ===========================

   Amigo Identify

       ATN  UNT     Untalk
       ATN  MSA     My secondary address
            DAB     ID data byte #1 = 00H
       EOI  DAB     ID data byte #2 = 03H
       ATN  OTA     Talk 30


   Amigo Clear

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 10H
            ppd     Parallel poll disabled
       EOI  DAB     Unused data byte
       ATN  SDC     Selected device clear
       ATN  UNL     Unlisten
            ...
            ppe     Parallel poll enabled when clear completes


   CRC

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 09H
            ppd     Parallel poll disabled
            DAB     Data byte #1
            ...
       EOI  DAB     Data byte #n
            ppe     Parallel poll enabled
       ATN  UNT     Untalk

   or

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 09H
            ppd     Parallel poll disabled
            DAB     Data byte #1
            ...
       EOI  DAB     Data byte #n
            ppe     Parallel poll enabled
       ATN  UNL     Unlisten


   Device Specified Jump

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 10H
            ppd     Parallel poll disabled
       EOI  DAB     DSJ data byte
       ATN  UNT     Untalk


   Initiate Self-Test and Return Self-Test Result

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 1FH
            ppd     Parallel poll disabled
       EOI  DAB     Self-test number
            ppe     Parallel poll enabled
       ATN  UNL     Unlisten

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 1FH
            ppd     Parallel poll disabled
       EOI  DAB     Result data byte
            ppe     Parallel poll enabled
       ATN  UNT     Untalk


   Write Loopback and Read Loopback

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 1EH
            ppd     Parallel poll disabled
            DAB     Loopback data byte #1
            ...
       EOI  DAB     Loopback data byte #256
            ppe     Parallel poll enabled
       ATN  UNL     Unlisten

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 1EH
            ppd     Parallel poll disabled
            DAB     Loopback data byte #1
            ...
       EOI  DAB     Loopback data byte #16
            ppe     Parallel poll enabled
       ATN  UNT     Untalk


   Recalibrate and Seek

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 01H, 02H
            ...     (one to five
       EOI  DAB        parameter bytes)
       ATN  UNL     Unlisten
            ...
            ppe     Parallel poll enabled when seek completes


   Clear, Address Record, and Set File Mask

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 0AH, 0CH, 0FH
            ...     (one to five
       EOI  DAB        parameter bytes)
            ppe     Parallel poll enabled
       ATN  UNL     Unlisten


   End

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 15H
       EOI  DAB     Unused data byte
       ATN  UNL     Unlisten


   Request Status, Request Sector Address, and Request Logical Disc Address

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 03H, 04H, 14H
       EOI  DAB     Unused data byte
       ATN  UNL     Unlisten

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 08H
            DAB     Status byte #1
            ...     (two to four
       EOI  DAB        status bytes)
            ppe     Parallel poll enabled
       ATN  UNT     Untalk


   Cold Load Read, Read, Read Full Sector, Verify, Read with Offset, and Read
   without Verify

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 00H, 05H, 06H, 07H, 0EH, 12H
       EOI  DAB     Unused data byte
       ATN  UNL     Unlisten

       ATN  MTA     My talk address
       ATN  SCG     Secondary command 00H
            DAB     Read data byte #1
            ...
            DAB     Read data byte #n
       ATN  UNT     Untalk
            ...
            ppe     Parallel poll enabled when sector ends


   Write, Write Full Sector, and Initialize

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 08H
            ppd     Parallel poll disabled
            DAB     Opcode 08H, 09H, 0BH
       EOI  DAB     Unused data byte
       ATN  UNL     Unlisten

       ATN  MLA     My listen address
       ATN  SCG     Secondary command 00H
            DAB     Write data byte #1
            ...
       EOI  DAB     Write data byte #n
            ppe     Parallel poll enabled
       ATN  UNL     Unlisten


   Implementation notes:

    1. The 12745 does not alter the parallel poll response for the
       Device-Specified Jump command.

    2. The 7910 does not perform a parallel poll response enable and disable
       between the Initiate Self-Test and Return Self-Test Result commands.

    3. The 12745 does not disable the parallel poll response for the Read
       Loopback command.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"
#include "hp2100_di.h"
#include "hp2100_disclib.h"



/* Program constants */

#define DA_UNITS        4                       /* number of addressable disc units */


/* Interface states */

typedef enum {
    idle = 0,                                   /* idle = default for reset */
    opcode_wait,                                /* waiting for opcode reception */
    parameter_wait,                             /* waiting for parameter reception */
    read_wait,                                  /* waiting for send read data secondary */
    write_wait,                                 /* waiting for receive write data secondary */
    status_wait,                                /* waiting for send status secondary */
    command_exec,                               /* executing an interface command */
    command_wait,                               /* waiting for command completion */
    read_xfer,                                  /* sending read data or status */
    write_xfer,                                 /* receiving write data */
    error_source,                               /* sending bytes for error recovery */
    error_sink                                  /* receiving bytes for error recovery */
    } IF_STATE;


/* Interface state names */

static const char * const if_state_name [] = {
    "idle",
    "opcode wait",
    "parameter wait",
    "read wait",
    "write wait",
    "status wait",
    "command execution",
    "command wait",
    "read transfer",
    "write transfer",
    "error source",
    "error sink"
    };


/* Next interface state after command recognition */

static const IF_STATE next_state [] = {
    read_wait,                                  /* cold load read */
    command_exec,                               /* recalibrate */
    command_exec,                               /* seek */
    status_wait,                                /* request status */
    status_wait,                                /* request sector address */
    read_wait,                                  /* read */
    read_wait,                                  /* read full sector */
    command_exec,                               /* verify */
    write_wait,                                 /* write */
    write_wait,                                 /* write full sector */
    command_exec,                               /* clear */
    write_wait,                                 /* initialize */
    command_exec,                               /* address record */
    idle,                                       /* request syndrome */
    read_wait,                                  /* read with offset */
    command_exec,                               /* set file mask */
    idle,                                       /* invalid */
    idle,                                       /* invalid */
    read_wait,                                  /* read without verify */
    idle,                                       /* load TIO register */
    status_wait,                                /* request disc address */
    command_exec,                               /* end */
    idle                                        /* wakeup */
    };


/* Interface commands */

typedef enum {
    invalid = 0,                                /* invalid = default for reset */
    disc_command,                               /* MLA 08 */
    crc_listen,                                 /* MLA 09 */
    amigo_clear,                                /* MLA 10 */
    write_loopback,                             /* MLA 1E */
    initiate_self_test,                         /* MLA 1F */
    crc_talk,                                   /* MTA 09 */
    device_specified_jump,                      /* MTA 10 */
    read_loopback,                              /* MTA 1E */
    return_self_test_result,                    /* MTA 1F */
    amigo_identify                              /* UNT MSA */
    } IF_COMMAND;

/* Interface command names */

static const char * const if_command_name [] = {
    "invalid",
    "disc command",
    "CRC listen",
    "Amigo clear",
    "write loopback",
    "initiate self-test",
    "CRC talk",
    "device specified jump",
    "read loopback",
    "return self-test result",
    "Amigo identify"
    };



/* Amigo disc state variables */

static uint16 buffer [DL_BUFSIZE];              /* command/status/sector buffer */

static uint8      if_dsj     [DA_UNITS];        /* ICD controller DSJ values */
static IF_STATE   if_state   [DA_UNITS];        /* ICD controller state */
static IF_COMMAND if_command [DA_UNITS];        /* ICD controller command */

static CNTLR_VARS icd_cntlr [DA_UNITS] =        /* ICD controllers: */
    { { CNTLR_INIT (ICD, buffer, NULL) },       /*   unit 0 controller */
      { CNTLR_INIT (ICD, buffer, NULL) },       /*   unit 1 controller */
      { CNTLR_INIT (ICD, buffer, NULL) },       /*   unit 2 controller */
      { CNTLR_INIT (ICD, buffer, NULL) } };     /*   unit 3 controller */



/* Amigo disc local VM routines */

static t_stat da_reset   (DEVICE *dptr);
static t_stat da_boot    (int32  unitno, DEVICE *dptr);
static t_stat da_attach  (UNIT   *uptr, CONST char *cptr);
static t_stat da_detach  (UNIT   *uptr);

/* Amigo disc local SCP routines */

static t_stat da_service     (UNIT *uptr);
static t_stat da_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

/* Amigo disc local utility routines */

static t_bool start_command     (uint32 unit);
static void   abort_command     (uint32 unit, CNTLR_STATUS status, IF_STATE state);
static void   complete_read     (uint32 unit);
static void   complete_write    (uint32 unit);
static void   complete_abort    (uint32 unit);
static uint8  get_buffer_byte   (CVPTR  cvptr);
static void   put_buffer_byte   (CVPTR  cvptr, uint8 data);
static t_stat activate_unit     (UNIT   *uptr);



/* Amigo disc VM global data structures.

   da_dib       DA device information block
   da_unit      DA unit list
   da_reg       DA register list
   da_mod       DA modifier list
   da_dev       DA device descriptor


   Implementation notes:

    1. The IFSTAT and IFCMD registers are declared to accommodate the
       corresponding arrays of enums.  Arrayed registers assume that elements
       are allocated space only to the integral number of bytes implied by the
       "width" field.  The storage size of an enum is implementation-defined, so
       we must determine the number of bits for "width" at compile time.
       PV_LEFT is used to avoid the large number of leading zeros that would be
       displayed if an implementation stored enums in full words.

    2. The CNVARS register is included to ensure that the controller state
       variables array is saved by a SAVE command.  It is declared as a hidden,
       read-only byte array of a depth compatible with the size of the array.

       There does not appear to be a way to expose the fields of the four
       controller state variables as arrayed registers.  Access to an array
       always assumes that elements appear at memory offsets equal to the
       element size, i.e., a 32-bit arrayed register has elements at four-byte
       offsets.  There's no way to specify an array of structure elements where
       a given 32-bit field appears at, say, 92-byte offsets (i.e., the size of
       the structure).
*/

DEVICE da_dev;

static DIB da_dib = {
    &di_interface,                              /* the device's I/O interface function pointer */
    DI_DA,                                      /* the device's select code (02-77) */
    da,                                         /* the card index */
    "12821A Disc Interface",                    /* the card description */
    "12992H 7906H/7920H/7925H/9895 Disc Loader" /* the ROM description */
    };

#define UNIT_FLAGS  (UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE | UNIT_DISABLE | UNIT_UNLOAD)

static UNIT da_unit [] = {
    { UDATA (&da_service, UNIT_FLAGS | MODEL_7906 | SET_BUSADR (0), D7906_WORDS) }, /* drive unit 0 */
    { UDATA (&da_service, UNIT_FLAGS | MODEL_7906 | SET_BUSADR (1), D7906_WORDS) }, /* drive unit 1 */
    { UDATA (&da_service, UNIT_FLAGS | MODEL_7906 | SET_BUSADR (2), D7906_WORDS) }, /* drive unit 2 */
    { UDATA (&da_service, UNIT_FLAGS | MODEL_7906 | SET_BUSADR (3), D7906_WORDS) }  /* drive unit 3 */
    };

static REG da_reg [] = {
    DI_REGS (da),

    { BRDATA (BUFFER, buffer,      8, 16,       DL_BUFSIZE)                              },

    { BRDATA (DSJ,    if_dsj,     10,  2,                             DA_UNITS)          },
    { BRDATA (ISTATE, if_state,   10, sizeof (IF_STATE) * CHAR_BIT,   DA_UNITS), PV_LEFT },
    { BRDATA (ICMD,   if_command, 10, sizeof (IF_COMMAND) * CHAR_BIT, DA_UNITS), PV_LEFT },

    { VBRDATA (CNVARS, icd_cntlr, 10, CHAR_BIT, sizeof (CNTLR_VARS) * DA_UNITS), REG_HRO },

    { NULL }
    };

static MTAB da_mod [] = {
    DI_MODS (da_dev, da_dib),

/*    Mask Value    Match Value   Print String       Match String     Validation        Display  Descriptor */
/*    ------------  ------------  -----------------  ---------------  ----------------  -------  ---------- */
    { UNIT_UNLOAD,  UNIT_UNLOAD,  "heads unloaded",  "UNLOADED",      &da_load_unload,  NULL,    NULL       },
    { UNIT_UNLOAD,  0,            "heads loaded",    "LOADED",        &da_load_unload,  NULL,    NULL       },

    { UNIT_WLK,     UNIT_WLK,     "protected",       "PROTECT",       NULL,             NULL,    NULL       },
    { UNIT_WLK,     0,            "unprotected",     "UNPROTECT",     NULL,             NULL,    NULL       },

    { UNIT_WLK,     UNIT_WLK,     NULL,              "LOCKED",        NULL,             NULL,    NULL       },
    { UNIT_WLK,     0,            NULL,              "WRITEENABLED",  NULL,             NULL,    NULL       },

    { UNIT_FMT,     UNIT_FMT,     "format enabled",  "FORMAT",        NULL,             NULL,    NULL       },
    { UNIT_FMT,     0,            "format disabled", "NOFORMAT",      NULL,             NULL,    NULL       },

    { UNIT_MODEL,   MODEL_7906,   "7906H",           "7906H",         &dl_set_model,    NULL,    NULL       },
    { UNIT_MODEL,   MODEL_7920,   "7920H",           "7920H",         &dl_set_model,    NULL,    NULL       },
    { UNIT_MODEL,   MODEL_7925,   "7925H",           "7925H",         &dl_set_model,    NULL,    NULL       },

    { 0 }
    };

DEVICE da_dev = {
    "DA",                                       /* device name */
    da_unit,                                    /* unit array */
    da_reg,                                     /* register array */
    da_mod,                                     /* modifier array */
    DA_UNITS,                                   /* number of units */
    10,                                         /* address radix */
    26,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &da_reset,                                  /* reset routine */
    &da_boot,                                   /* boot routine */
    &da_attach,                                 /* attach routine */
    &da_detach,                                 /* detach routine */
    &da_dib,                                    /* device information block */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    di_deb,                                     /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Amigo disc global VM routines */


/* Service an Amigo disc drive I/O event.

   The service routine is called to execute commands and control the transfer of
   data to and from the HP-IB card.  The actions to be taken depend on the
   current state of the ICD interface.  The possibilities are:

    1. A command is pending on the interface.  This occurs only when a command
       is received while a Seek or Recalibrate command is in progress.

    2. A command is executing.

    3. Data is being sent or received over the HP-IB during command execution.

    4. Dummy bytes are being sent or received over the HP-IB due to a command
       error.

   Entry to the the service routine in any other interface state or to process a
   command not allowed in a valid state will return an Internal Error to cause a
   simulator stop.  Exit from the routine will be either in one of the above
   states, or in the idle state if the operation is complete.

   The specific actions taken for the various interface states are as follows:

   command_wait
   ============

     We are entered in this state only if a unit that was busy (still seeking)
     was addressed to listen or talk.  The card has been held off by asserting
     NRFD after receiving MLA or MTA.  Upon entry, we complete the seek and then
     release the interface by denying NRFD to allow the remainder of the command
     sequence to be received from the card.

   command_exec
   ============

     We are entered in this state to initiate, continue, or complete a command.
     The command may be a disc command, such as Seek or Read, or an interface
     command, such as Amigo Identify or Device-Specified Jump.

     Disc commands call the disc library service routine to perform all of the
     common controller actions.  Any ICD-specific actions needed, such as
     setting the DSJ value, are performed after the call.

     Certain disc commands require multiple execution phases.  For example, the
     Read command has a start phase that reads data from the disc image file
     into the sector buffer, a data phase that transfers bytes from the buffer
     to the card, and an end phase that schedules the intersector gap time and
     resets to the start phase.  Data phase transfers are performed in the
     read_xfer or write_xfer interface states.

     The results of the disc library service are inferred by the controller
     state.  If the controller is busy, then the command continues in a new
     phase.  Otherwise, the command either has completed normally or has
     terminated with an error.  If an error has occurred during a disc command
     that transfers data, DSJ is set to 1, and the interface state is changed to
     source or sink dummy bytes to complete the command sequence.

     Interface commands may either complete immediately (e.g., Amigo Clear) or
     transfer data (e.g., DSJ).

   read_xfer
   =========

     Commands that send data to the CPU enter the service routine to source a
     byte to the bus.  Bytes are transferred only when ATN and NRFD are denied;
     if they are not, we simply exit, as we will be rescheduled when the lines
     are dropped.  Otherwise, we get a byte from the sector buffer and send it
     to the card.  If the card has stopped listening, or the buffer is now
     empty, then we terminate the transfer and move to the end phase of the
     command.  Otherwise, we reschedule the next data phase byte transfer.

     Disc and interface commands are handled separately, as EOI is always
     asserted on the last byte of an interface command transfer and never on a
     (good) disc command transfer.

   write_xfer
   ==========

     Commands that receive data from the CPU enter the service routine to
     determine whether or not to continue the transfer.  Our bus accept routine
     has already stored the received byte in the sector buffer and has asserted
     NRFD to hold off the card.  If the buffer is now full, or the byte was
     tagged with EOI, then we terminate the transfer and move to the end phase
     of the command.  Otherwise, we deny NRFD and exit; we will be rescheduled
     when the next byte arrives.

   error_source
   ============

     If an error occurred during the data transfer phase of a read or status
     command, a dummy byte tagged with EOI is sourced to the bus.  This allows
     the OS driver for the card to terminate the command and request the
     controller's status.

   error_sink
   ==========

     If an error occurred during the data transfer phase of a write command,
     dummy bytes are sunk from the bus until EOI is seen or the card is
     unaddressed.  This allows the OS driver to complete the command as expected
     and then determine the cause of the failure by requesting the controller's
     status.


   Implementation notes:

    1. The disc library sets the controller state to idle for a normal End,
       Seek, or Recalibrate command and to wait for all other commands that end
       normally.  So we determine command completion by checking if the
       controller is not busy, rather than checking if the controller is idle.

       Drive Attention status is the normal result of the completion of a Seek
       or Recalibrate command.  Normal Completion status is the normal result of
       all other commands.

    2. The disc library returns the buffer length in words.  We double the
       return value to count bytes.

    3. Some commands, such as DSJ, could be completed in the bus accept routine.
       They are serviced here instead to avoid presenting a zero execution time
       to the CPU.

    4. The Amigo command set does not provide the disc with the number of bytes
       that will be read, and the unit expects to be untalked when the read is
       to terminate.  The RTE ICD bootstrap extension does not do this.
       Instead, it resets the card via CLC 0,C to terminate the Cold Load Read
       that was started by the ICD boot loader ROM.

       In hardware, if the LSTN control bit is cleared, e.g., by CRS,
       transmission stops because the card denies NDAC and NRFD (the HP-IB
       handshake requires NDAC and NRFD to be asserted to start the handshake
       sequence; TACS * SDYS * ~NDAC * ~NRFD is an error condition).  In
       simulation, we handle this by terminating a read transfer if the card
       stops accepting.  If we did not, then the disc would continue to source
       bytes to the bus, overflowing the card FIFO (a FIFO full condition cannot
       assert NRFD if the LSTN control bit is clear).
*/

static t_stat da_service (UNIT *uptr)
{
uint8 data;
CNTLR_CLASS command_class;
const int32 unit = uptr - da_unit;                          /* get the disc unit number */
const CVPTR cvptr = &icd_cntlr [unit];                      /* get a pointer to the controller */
t_stat result = SCPE_OK;
t_bool release_interface = FALSE;

switch (if_state [unit]) {                                  /* dispatch the interface state */

    case command_wait:                                      /* command is waiting */
        release_interface = TRUE;                           /* release the interface at then end if it's idle */

    /* fall through into the command_exec handler to process the current command */

    case command_exec:                                      /* command is executing */
        switch (if_command [unit]) {                        /* dispatch the interface command */

            case disc_command:                              /* execute a disc command */
                result = dl_service_drive (cvptr, uptr);    /* service the disc unit */

                if (cvptr->opcode == Clear)                 /* is this a Clear command? */
                    if_dsj [unit] = 2;                      /* indicate that the self test is complete */

                if (cvptr->state != cntlr_busy) {           /* has the controller stopped? */
                    if_state [unit] = idle;                 /* idle the interface */

                    if (cvptr->status == normal_completion ||   /* do we have normal completion */
                        cvptr->status == drive_attention)       /*   or drive attention? */
                        break;                                  /* we're done */

                    else {                                      /* if the status is abnormal */
                        if_dsj [unit] = 1;                      /*   an error has occurred */

                        command_class = dl_classify (*cvptr);   /* classify the command */

                        if (command_class == class_write) {     /* did a write command fail? */
                            if_state [unit] = error_sink;       /* sink the remaining bytes */
                            uptr->wait = cvptr->cmd_time;       /* activate to complete processing */
                            }

                        else if (command_class != class_control) {  /* did a read or status command fail? */
                            if_state [unit] = error_source;         /* source an error byte */
                            uptr->wait = cvptr->cmd_time;           /* activate to complete processing */
                            }
                        }
                    }

                else if (uptr->PHASE == data_phase) {           /* are we starting the data phase? */
                    cvptr->length = cvptr->length * 2;          /* convert the buffer length to bytes */

                    if (dl_classify (*cvptr) == class_write)    /* is this a write command? */
                        if_state [unit] = write_xfer;           /* set for a write data transfer */
                    else                                        /* it is a read or status command */
                        if_state [unit] = read_xfer;            /* set for a read data transfer */
                    }

                break;


            case amigo_identify:                            /* Amigo Identify */
                buffer [0] = 0x0003;                        /* store the response in the buffer */
                cvptr->length = 2;                          /* return two bytes */

                if_state [unit] = read_xfer;                /* we are ready to transfer the data */
                uptr->wait = cvptr->cmd_time;               /* schedule the transfer */

                tprintf (da_dev, DEB_RWSC, "Unit %d Amigo identify response %04XH\n",
                         unit, buffer [0]);
                break;


            case initiate_self_test:                        /* Initiate a self test */
                sim_cancel (&da_unit [unit]);               /* cancel any operation in progress */
                dl_clear_controller (cvptr,                 /* hard-clear the controller */
                                     &da_unit [unit],
                                     hard_clear);
                if_dsj [unit] = 2;                          /* set DSJ for self test completion */
                if_state [unit] = idle;                     /* the command is complete */
                di_poll_response (da, unit, SET);           /*   with PPR enabled */
                break;


            case amigo_clear:                               /* Amigo clear */
                dl_idle_controller (cvptr);                 /* idle the controller */
                if_dsj [unit] = 0;                          /* clear the DSJ value */
                if_state [unit] = idle;                     /* the command is complete */
                di_poll_response (da, unit, SET);           /*   with PPR enabled */
                break;


            default:                                        /* no other commands are executed */
                result = SCPE_IERR;                         /* signal an internal error */
                break;
            }                                               /* end of command dispatch */
        break;


    case error_source:                                      /* send data after an error */
        if (! (di [da].bus_cntl & (BUS_ATN | BUS_NRFD))) {  /* is the card ready for data? */
            di [da].bus_cntl |= BUS_EOI;                    /* set EOI */
            di_bus_source (da, 0);                          /*   and send a dummy byte to the card */
            if_state [unit] = idle;                         /* the command is complete */
            }
        break;


    case read_xfer:                                         /* send read data */
        if (! (di [da].bus_cntl & (BUS_ATN | BUS_NRFD)))    /* is the card ready for data? */
            switch (if_command [unit]) {                    /* dispatch the interface command */

                case disc_command:                          /* disc read or status commands */
                    data = get_buffer_byte (cvptr);         /* get the next byte from the buffer */

                    if (di_bus_source (da, data) == FALSE)  /* send the byte to the card; is it listening? */
                        cvptr->eod = SET;                   /* no, so terminate the read */

                    if (cvptr->length == 0 || cvptr->eod == SET) {  /* is the data phase complete? */
                        uptr->PHASE = end_phase;                    /* set the end phase */

                        if (cvptr->opcode == Request_Status)    /* is it a Request Status command? */
                            if_dsj [unit] = 0;                  /* clear the DSJ value */

                        if_state [unit] = command_exec;         /* set to execute the command */
                        uptr->wait = cvptr->cmd_time;           /*   and reschedule the service */
                        }

                    else                                    /* the data phase continues */
                        uptr->wait = cvptr->data_time;      /* reschedule the next transfer */

                    break;


                case amigo_identify:
                case read_loopback:
                case return_self_test_result:
                    data = get_buffer_byte (cvptr);         /* get the next byte from the buffer */

                    if (cvptr->length == 0)                 /* is the transfer complete? */
                        di [da].bus_cntl |= BUS_EOI;        /* set EOI */

                    if (di_bus_source (da, data)            /* send the byte to the card; is it listening? */
                      && cvptr->length > 0)                 /*   and is there more to transfer? */
                        uptr->wait = cvptr->data_time;      /* reschedule the next transfer */

                    else {                                  /* the transfer is complete */
                        if_state [unit] = idle;             /* the command is complete */
                        di_poll_response (da, unit, SET);   /* enable the PPR */
                        }
                    break;


                case device_specified_jump:
                    di [da].bus_cntl |= BUS_EOI;            /* set EOI */
                    di_bus_source (da, if_dsj [unit]);      /* send the DSJ value to the card */
                    if_state [unit] = idle;                 /* the command is complete */
                    break;


                case crc_talk:
                    di [da].bus_cntl |= BUS_EOI;            /* set EOI */
                    di_bus_source (da, 0);                  /* send dummy bytes */
                    break;                                  /*   until the card untalks */


                default:                                    /* no other commands send data */
                    result = SCPE_IERR;                     /* signal an internal error */
                    break;
                }                                           /* end of read data transfer dispatch */
        break;


    case error_sink:                                        /* absorb data after an error */
        cvptr->index = 0;                                   /* absorb data until EOI asserts */

        if (cvptr->eod == SET)                              /* is the transfer complete? */
            if_state [unit] = idle;                         /* the command is complete */

        di_bus_control (da, unit, 0, BUS_NRFD);             /* deny NRFD to allow the card to resume */
        break;


    case write_xfer:                                        /* receive write data */
        switch (if_command [unit]) {                        /* dispatch the interface command */

            case disc_command:                                  /* disc write commands */
                if (cvptr->length == 0 || cvptr->eod == SET) {  /* is the data phase complete? */
                    uptr->PHASE = end_phase;                    /* set the end phase */

                    if_state [unit] = command_exec;         /* set to execute the command */
                    uptr->wait = cvptr->cmd_time;           /*   and schedule the service */

                    if (cvptr->eod == CLEAR)                /* is the transfer continuing? */
                        break;                              /* do not deny NRFD until next service! */
                    }

                di_bus_control (da, unit, 0, BUS_NRFD);     /* deny NRFD to allow the card to resume */
                break;


            case write_loopback:
                if (cvptr->eod == SET) {                    /* is the transfer complete? */
                    cvptr->length = 16 - cvptr->length;     /* set the count of bytes transferred */
                    if_state [unit] = idle;                 /* the command is complete */
                    }

                di_bus_control (da, unit, 0, BUS_NRFD);     /* deny NRFD to allow the card to resume */
                break;


            default:                                        /* no other commands receive data */
                result = SCPE_IERR;                         /* signal an internal error */
                break;
            }                                               /* end of write data transfer dispatch */
        break;


    default:                                                /* no other states schedule service */
        result = SCPE_IERR;                                 /* signal an internal error */
        break;
    }                                                       /* end of interface state dispatch */


if (uptr->wait)                                             /* is service requested? */
    activate_unit (uptr);                                   /* schedule the next event */

if (result == SCPE_IERR)                                    /* did an internal error occur? */
    if (if_state [unit] == command_exec
      && if_command [unit] == disc_command)
        tprintf (da_dev, DEB_RWSC, "Unit %d %s command %s phase service not handled\n",
                 unit,
                 dl_opcode_name (ICD, (CNTLR_OPCODE) uptr->OP),
                 dl_phase_name ((CNTLR_PHASE) uptr->PHASE));
    else
        tprintf (da_dev, DEB_RWSC, "Unit %d %s state %s service not handled\n",
                 unit,
                 if_command_name [if_command [unit]],
                 if_state_name [if_state [unit]]);

if (if_state [unit] == idle) {                              /* is the command now complete? */
    if (if_command [unit] == disc_command) {                /* did a disc command complete? */
        if (cvptr->opcode != End)                           /* yes; if the command was not End, */
            di_poll_response (da, unit, SET);               /*   then enable PPR */

        tprintf (da_dev, DEB_RWSC, "Unit %d %s disc command completed\n",
                 unit, dl_opcode_name (ICD, cvptr->opcode));
        }

    else                                                    /* an interface command completed */
        tprintf (da_dev, DEB_RWSC, "Unit %d %s command completed\n",
                 unit, if_command_name [if_command [unit]]);

    if (release_interface)                                  /* if the next command is already pending */
        di_bus_control (da, unit, 0, BUS_NRFD);             /*   deny NRFD to allow the card to resume */
    }

return result;                                              /* return the result of the service */
}


/* Reset or preset the simulator.

   In hardware, a self-test is performed by the controller at power-on.  When
   the self-test completes, the controller sets DSJ = 2 and enables the parallel
   poll response.

   A front panel PRESET or programmed CRS has no direct effect on the controller
   or drive.  However, the card reacts to CRS by clearing its talker and
   listener states, so an in-progress read or status command will abort when the
   next byte sourced to the bus finds no acceptors.
*/

static t_stat da_reset (DEVICE *dptr)
{
uint32 unit;
t_stat status;

status = di_reset (dptr);                               /* reset the card */

if (status == SCPE_OK && (sim_switches & SWMASK ('P'))) /* is the card OK and is this a power-on reset? */
    for (unit = 0; unit < dptr->numunits; unit++) {     /* loop through the units */
        sim_cancel (dptr->units + unit);                /* cancel any current activation */
        dptr->units [unit].CYL = 0;                     /* reset the head position */
        dptr->units [unit].pos = 0;                     /*   to cylinder 0 */

        dl_clear_controller (&icd_cntlr [unit],         /* hard-clear the controller */
                             dptr->units + unit,
                             hard_clear);

        if_state [unit] = idle;                         /* reset the interface state */
        if_command [unit] = invalid;                    /* reset the interface command */

        if_dsj [unit] = 2;                              /* set the DSJ for power up complete */
        }

return status;
}


/* Attach a unit to a disc image file.

   The simulator considers an attached unit to be connected to the bus and an
   unattached unit to be disconnected, so we set the card's acceptor bit for the
   selected unit if the attach is successful.  An attached unit is ready if the
   heads are loaded or not ready if not.

   This model is slightly different than the MAC (DS) simulation, where an
   unattached unit is considered "connected but not ready" -- the same
   indication returned by an attached unit whose heads are unloaded.  Therefore,
   the situation when the simulator is started is that all DS units are
   "connected to the controller but not ready," whereas all DA units are "not
   connected to the bus."  This eliminates the overhead of sending HP-IB
   messages to unused units.

   In tabular form, the simulator responses are:

      Enabled  Loaded  Attached    DS (MAC)      DA (ICD)
      -------  ------  --------  ------------  ------------
         N       N        N      disconnected  disconnected
         N       N        Y           --            --
         N       Y        N           --            --
         N       Y        Y           --            --
         Y       N        N        unloaded    disconnected
         Y       N        Y        unloaded      unloaded
         Y       Y        N           --            --
         Y       Y        Y         ready         ready

   The unspecified responses are illegal conditions; for example, the simulator
   does not allow an attached unit to be disabled.

   If a new file is specified, the file is initialized to its capacity by
   writing a zero to the last byte in the file.


   Implementation notes:

    1. To conform exactly to the MAC responses would have required intercepting
       the SET <unit> DISABLED/ENABLED commands in order to clear or set the unit
       accepting bits.  However, short of intercepting the all SET commands with
       a custom command table, there is no way to ensure that unit enables are
       observed.  Adding ENABLED and DISABLED to the modifiers table and
       specifying a validation routine works for the DISABLED case but not the
       ENABLED case -- set_unit_enbdis returns SCPE_UDIS before calling the
       validation routine.

    2. The C standard says, "A binary stream need not meaningfully support fseek
       calls with a whence value of SEEK_END," so instead we determine the
       offset from the start of the file to the last byte and seek there.
*/

static t_stat da_attach (UNIT *uptr, CONST char *cptr)
{
t_stat      result;
t_addr      offset;
const uint8 zero = 0;
const int32 unit = uptr - da_unit;                      /* calculate the unit number */

result = dl_attach (&icd_cntlr [unit], uptr, cptr);     /* attach the drive */

if (result == SCPE_OK) {                                /* if the attach was successful */
    di [da].acceptors |= (1 << unit);                   /*   then set the unit's accepting bit */

    if (sim_switches & SWMASK ('N')) {                  /* if this is a new disc image */
        offset = (t_addr)                               /*   then determine the offset of */
          (uptr->capac * sizeof (int16) - sizeof zero); /*     the last byte in a full-sized file */

        if (sim_fseek (uptr->fileref, offset, SEEK_SET) != 0    /* seek to the last byte */
          || fwrite (&zero, sizeof zero, 1, uptr->fileref) == 0 /*   and write a zero to fill */
          || fflush (uptr->fileref) != 0)                       /*     the file to its capacity */
            clearerr (uptr->fileref);                           /* clear and ignore any errors */
        }
    }

return result;                                          /* return the result of the attach */
}


/* Detach a disc image file from a unit.

   As explained above, detaching a unit is the hardware equivalent of
   disconnecting the drive from the bus, so we clear the unit's acceptor bit if
   the detach is successful.
*/

static t_stat da_detach (UNIT *uptr)
{
t_stat result;
const int32 unit = uptr - da_unit;                      /* calculate the unit number */

result = dl_detach (&icd_cntlr [unit], uptr);           /* detach the drive */

if (result == SCPE_OK) {                                /* was the detach successful? */
    di [da].acceptors &= ~(1 << unit);                  /* clear the unit's accepting bit */
    di_poll_response (da, unit, CLEAR);                 /*   and its PPR, as it's no longer present */
    }

return result;
}


/* 7906H/20H/25H disc bootstrap loader (12992H).

   The HP 1000 uses the 12992H boot loader ROM to bootstrap the ICD discs.  Bit
   12 of the S register determines whether an RPL or manual boot is performed.
   Bits 1-0 specify the head number to use.

   The loader reads 256 words from cylinder 0 sector 0 of the specified head
   into memory starting at location 2011 octal.  Loader execution ends with one
   of the following instructions:

     * HLT 11     - the drive aborted the transfer due to an unrecoverable error
     * JSB 2055,I - the disc read succeeded

   The ICD drives are not supported on the 2100/14/15/16 CPUs, so no 21xx loader
   is provided.
*/

static const LOADER_ARRAY da_loaders = {
    {                               /* HP 21xx Loader does not exist */
      IBL_NA,                       /*   loader starting index */
      IBL_NA,                       /*   DMA index */
      IBL_NA,                       /*   FWA index */
      { 0 } },

    {                               /* HP 1000 Loader ROM (12992H) */
      IBL_START,                    /*   loader starting index */
      IBL_DMA,                      /*   DMA index */
      IBL_FWA,                      /*   FWA index */
      { 0102501,                    /*   77700:  START LIA 1         GET SWITCH REGISTER SETTING */
        0100044,                    /*   77701:        LSL 4         SHIFT A LEFT 4 */
        0006111,                    /*   77702:        CLE,SLB,RSS   SR BIT 12 SET FOR MANUAL BOOT? */
        0100041,                    /*   77703:        LSL 1         NO, SHIFT HEAD # FOR RPL BOOT */
        0001424,                    /*   77704:        ALR,ALR       SHIFT HEAD 2, CLEAR SIGN */
        0033744,                    /*   77705:        IOR HDSEC     SET EOI BIT */
        0073744,                    /*   77706:        STA HDSEC     PLACE IN COMMAND BUFFER */
        0017756,                    /*   77707:        JSB BTCTL     SEND DUMMY,U-CLR,PP */
        0102510,                    /*   77710:        LIA IBI       READ INPUT REGISTER */
        0101027,                    /*   77711:        ASR 7         SHIFT DRIVE 0 RESPONSE TO LSB */
        0002011,                    /*   77712:        SLA,RSS       DID DRIVE 0 RESPOND? */
        0027710,                    /*   77713:        JMP *-3       NO, GO LOOK AGAIN */
        0107700,                    /*   77714:        CLC 0,C       */
        0017756,                    /*   77715:        JSB BTCTL     SEND TALK, CL-RD,BUS HOLDER */
        0002300,                    /*   77716:        CCE           */
        0017756,                    /*   77717:        JSB BTCTL     TELL CARD TO LISTEN */
        0063776,                    /*   77720:        LDA DMACW     LOAD DMA CONTROL WORD */
        0102606,                    /*   77721:        OTA 6         OUTPUT TO DCPC */
        0106702,                    /*   77722:        CLC 2         READY DCPC */
        0063735,                    /*   77723:        LDA ADDR1     LOAD DMA BUFFER ADDRESS */
        0102602,                    /*   77724:        OTA 2         OUTPUT TO DCPC */
        0063740,                    /*   77725:        LDA DMAWC     LOAD DMA WORD COUNT */
        0102702,                    /*   77726:        STC 2         READY DCPC */
        0102602,                    /*   77727:        OTA 2         OUTPUT TO DCPC */
        0103706,                    /*   77730:        STC 6,C       START DCPC */
        0102206,                    /*   77731:  TEST  SFC 6         SKIP IF DMA NOT DONE */
        0117750,                    /*   77732:        JSB ADDR2,I   SUCCESSFUL END OF TRANSFER */
        0102310,                    /*   77733:        SFS IBI       SKIP IF DISC ABORTED TRANSFER */
        0027731,                    /*   77734:        JMP TEST      RECHECK FOR TRANSFER END */
        0102011,                    /*   77735:  ADDR1 HLT 11B       ERROR HALT */
        0000677,                    /*   77736:  UNCLR OCT 677       UNLISTEN */
        0000737,                    /*   77737:        OCT 737       UNTALK */
        0176624,                    /*   77740:  DMAWC OCT 176624    UNIVERSAL CLEAR,LBO */
        0000440,                    /*   77741:  LIST  OCT 440       LISTEN BUS ADDRESS 0 */
        0000550,                    /*   77742:  CMSEC OCT 550       SECONDARY GET COMMAND */
        0000000,                    /*   77743:  BOOT  OCT 0         COLD LOAD READ COMMAND */
        0001000,                    /*   77744:  HDSEC OCT 1000      HEAD,SECTOR PLUS EOI */
        0000677,                    /*   77745:  UNLST OCT 677       ATN,PRIMARY UNLISTEN,PARITY */
        0000500,                    /*   77746:  TALK  OCT 500       SEND READ DATA */
        0100740,                    /*   77747:  RDSEC OCT 100740    SECONDARY READ DATA */
        0102055,                    /*   77750:  ADDR2 OCT 102055    BOOT EXTENSION STARTING ADDRESS */
        0004003,                    /*   77751:  CTLP  OCT 4003      INT=LBO,T,CIC */
        0000047,                    /*   77752:        OCT 47        PPE,L,T,CIC */
        0004003,                    /*   77753:        OCT 4003      INT=LBO,T,CIC */
        0000413,                    /*   77754:        OCT 413       ATN,P,L,CIC */
        0001015,                    /*   77755:        OCT 1015      INT=EOI,P,L,CIC */
        0000000,                    /*   77756:  BTCTL NOP           */
        0107710,                    /*   77757:        CLC IBI,C     RESET IBI */
        0063751,                    /*   77760:  BM    LDA CTLP      LOAD CONTROL WORD */
        0102610,                    /*   77761:        OTA IBI       OUTPUT TO CONTROL REGISTER */
        0102710,                    /*   77762:        STC IBI       RETURN IBI TO DATA MODE */
        0037760,                    /*   77763:        ISZ BM        INCREMENT CONTROL WORD POINTER */
        0002240,                    /*   77764:        SEZ,CME       */
        0127756,                    /*   77765:        JMP BTCTL,I   RETURN */
        0063736,                    /*   77766:  LABL  LDA UNCLR     LOAD DATA WORD */
        0037766,                    /*   77767:        ISZ LABL      INCREMENT WORD POINTER */
        0102610,                    /*   77770:        OTA IBI       OUTPUT TO HPIB */
        0002021,                    /*   77771:        SSA,RSS       SKIP IF LAST WORD */
        0027766,                    /*   77772:        JMP LABL      GO BACK FOR NEXT WORD */
        0102310,                    /*   77773:        SFS IBI       SKIP IF LAST WORD SENT TO BUS */
        0027773,                    /*   77774:        JMP *-1       RECHECK ACCEPTANCE */
        0027757,                    /*   77775:        JMP BTCTL+1   */
        0000010,                    /*   77776:  DMACW ABS IBI       */
        0170100 } }                 /*   77777:        ABS -START    */
    };

/* Device boot routine.

   This routine is called directly by the BOOT DA and LOAD DA commands to copy
   the device bootstrap into the upper 64 words of the logical address space.
   It is also called indirectly by a BOOT CPU or LOAD CPU command when the
   specified HP 1000 loader ROM socket contains a 12992H ROM.

   When called in response to a BOOT DA or LOAD DA command, the "unitno"
   parameter indicates the unit number specified in the BOOT command or is zero
   for the LOAD command, and "dptr" points at the DA device structure.  The
   bootstrap supports loading only from the disc at bus address 0 only.  The
   12992F loader ROM will be copied into memory and configured for the DA select
   code.  The S register will be set as it would be by the front-panel
   microcode.

   When called for a BOOT/LOAD CPU command, the "unitno" parameter indicates the
   select code to be used for configuration, and "dptr" will be NULL.  As above,
   the 12992H loader ROM will be copied into memory and configured for the
   specified select code.  The S register is assumed to be set correctly on
   entry and is not modified.

   The loader expects the S register to be is set as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   1 |      select code      |   reserved    | head  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Bit 12 must be 1 for a manual boot.  Bits 5-2 are nominally zero but are
   reserved for the target operating system.  For example, RTE uses bit 5 to
   indicate whether a standard (0) or reconfiguration (1) boot is desired.

   The boot routine sets bits 15-6 of the S register to appropriate values.
   Bits 5-3 and 1-0 retain their original values, so S should be set before
   booting.  These bits are typically set to 0, although bit 5 is set for an RTE
   reconfiguration boot, and bits 1-0 may be set if booting from a head other
   than 0 is desired.
*/

static t_stat da_boot (int32 unitno, DEVICE *dptr)
{
static const HP_WORD da_preserved   = 0000073u;             /* S-register bits 5-3 and 1-0 are preserved */
static const HP_WORD da_manual_boot = 0010000u;             /* S-register bit 12 set for a manual boot */
uint32 status;

if (dptr == NULL)                                           /* if we are being called for a BOOT/LOAD CPU */
    status = cpu_copy_loader (da_loaders, unitno,           /*   then copy the boot loader to memory */
                              IBL_S_NOCLEAR, IBL_S_NOSET);  /*     but do not alter the S register */

else if (GET_BUSADR (da_unit [unitno].flags) != 0)          /* otherwise BOOT DA is supported on bus address 0 only */
    return SCPE_NOFNC;                                      /*   so reject other addresses as unsupported */

else                                                            /* otherwise this is a BOOT/LOAD DA */
    status = cpu_copy_loader (da_loaders, da_dib.select_code,   /*   so copy the boot loader to memory */
                              da_preserved, da_manual_boot);    /*     and configure the S register if 1000 CPU */

if (status == 0)                                        /* if the copy failed */
    return SCPE_NOFNC;                                  /*   then reject the command */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   the boot loader was successfully copied */
}



/* Amigo disc global SCP routines */


/* Load or unload a unit's heads.

   The heads are automatically loaded when a unit is attached and unloaded when
   a unit is detached.  While a unit is attached, the heads may be manually
   unloaded; this yields a "not ready" status if the unit is accessed.  An
   unloaded drive may be manually loaded, returning the unit to "ready" status.

   The ICD controller sets Drive Attention status when the heads unload and also
   asserts a parallel poll response if the heads unload while in idle state 2
   (i.e., after an End command).


   Implementation notes:

    1. The 13365 manual says on page 28 that Drive Attention status is
       "Generated whenever...the drive unloads and the controller is in Idle
       State 2 or 3."  However, the ICD diagnostic tests for Drive Attention
       status on head unload immediately after the Request Status command that
       completes the previous step, which leaves the controller in idle state 1.

       Moreover, the diagnostic does NOT check for Drive Attention status if the
       Amigo ID is 2 (MAC controller).  But the 12745 manual says on page 3-7
       that the status is returned if "...Drive becomes not ready (heads
       unload)" with no mention of controller state.

       It appears as though the diagnostic test is exactly backward.  However,
       we match the diagnostic expectation below.
*/

static t_stat da_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const int32 unit = uptr - da_unit;                          /* calculate the unit number */
const t_bool load = (value != UNIT_UNLOAD);                 /* true if the heads are loading */
t_stat result;

result = dl_load_unload (&icd_cntlr [unit], uptr, load);    /* load or unload the heads */

if (result == SCPE_OK && ! load) {                          /* was the unload successful? */
    icd_cntlr [unit].status = drive_attention;              /* set Drive Attention status */

    if (uptr->OP == End)                                    /* is the controller in idle state 2? */
        di_poll_response (da, unit, SET);                   /* enable PPR */
    }

return result;
}



/* Amigo disc global bus routines */


/* Accept a data byte from the bus.

   The indicated unit is offered a byte that has been sourced to the bus.  The
   routine returns TRUE or FALSE to indicate whether or not it accepted the
   byte.

   Commands from the bus may be universal (applying to all acceptors) or
   addressed (applying only to those acceptors that have been addressed to
   listen).  Data bytes are accepted only if the unit has been addressed to
   listen.  As we are called for a data transfer or an addressed command only if
   we are currently listening, the only bytes that we do not accept are primary
   talk or listen commands directed to another address, or secondary commands
   when we are not addressed to listen.

   This routine handles the HP-IB protocol.  The type of byte passed is
   determined by the state of the ATN signal and, if ATN is asserted, by the
   high-order bits of the value.  Most of the work involves decoding secondary
   commands and their associated data parameters.  The interface state is
   changed as needed to track the command protocol.  The states processed in
   this routine are:

   opcode_wait
   ===========

     A Receive Disc Command secondary has been received, and the interface is
     waiting for the opcode that should follow.

   parameter_wait
   ==============

     A disc opcode or interface command has been received, and the interface is
     waiting for a parameter byte that should follow.

   write_wait
   ==========

     A disc write command has been received, and the interface is waiting for
     the Receive Write Data secondary that should follow.

   read_wait
   =========

     A disc read command has been received, and the interface is waiting for the
     Send Read Data secondary that should follow.

   status_wait
   ===========

     A disc status command has been received, and the interface is waiting for
     the Send Disc Status secondary that should follow.

   write_xfer
   ==========

     A disc write is in progress, and the interface is waiting for a data byte
     that should follow.

   error_sink
   ==========

     A disc write has terminated with an error, and the interface is waiting to
     absorb all of the remaining data bytes of the transfer.


   Disc commands and parameters are assembled in the sector buffer before being
   passed to the disc library to start the command.  Once the command is
   started, the interface state is set either to execute the command or to wait
   for the receipt of a data transfer secondary before executing, depending on
   the command.

   Two disc command protocol errors are detected.  First, an Illegal Opcode is
   identified during the check for the expected number of disc command
   parameters.  This allows us to sink an arbitrary number of parameter bytes.
   Second, an I/O Program Error occurs if an unsupported secondary is received
   or the HP-IB sequence is incorrect.  The latter occurs if a command has the
   wrong number of parameters or a secondary data transfer sequence is invalid.

   Disc commands that require data transfers (e.g., Read, Write, Request Status)
   involve a pair of secondaries.  The first transmits the command, and the
   second transmits or receives the data.  If one occurs without the other, an
   I/O Program Error occurs.

   A secondary or command that generates an I/O Program Error is always ignored.
   Error recovery is as follows:

    - An unsupported talk secondary sends a single data byte tagged with EOI.

    - An unsupported listen secondary accepts and discards any accompanying data
      bytes until EOI is asserted or an Unlisten is received.

    - A supported command with too few parameter bytes or for which the last
      parameter byte is not tagged with EOI (before unlisten) does nothing.

    - A supported command with too many parameter bytes accepts and discards
      excess parameter bytes until EOI is asserted or an Unlisten is received.

    - A read or status command that is not followed by a Send Read Data or a
      Send Disc Status secondary does nothing.  The unexpected secondary is
      executed normally.

    - A write command that is not followed by a Receive Write Data secondary
      does nothing.  The unexpected secondary is executed normally.

    - A Send Read Data or a Send Disc Status secondary that is not preceded by a
      read or status command sends a single data byte tagged with EOI.

    - A Receive Write Data secondary that is not preceded by a write command
      accepts and discards data bytes until EOI is asserted or an Unlisten is
      received.

   The Amigo command sequence does not provide a byte count for disc read and
   write commands, so the controller continues to source or accept data bytes
   until the device is unaddressed.  Normally, this is done by an Unlisten or
   Untalk.  However, per IEEE-488, a listening device may be unaddressed by IFC,
   by an Unlisten, or by addressing the device to talk, and a talking device may
   be unaddressed by IFC, by addressing another device to talk (or no device via
   Untalk), or by addressing the device to listen.  Therefore, we must keep
   track of whether the unit stopped talking or listening, and if it has, we
   check for command termination.

   If the controller is unaddressed in the middle of a sector transfer, the read
   or write must be terminated cleanly to ensure that the disc image is
   coherent.  It is also permissible to untalk the controller before all of the
   requested status bytes are returned.

   In addition, the controller has no way to inform the host that an error has
   occurred that prevents the command from continuing.  For example, if a data
   error is encountered while reading or a protected track is encountered while
   writing, the controller must still source or sink data bytes until the
   command is terminated by the host.  The controller handles read errors by
   sourcing a single data byte tagged with EOI and write errors by sinking data
   bytes until EOI is seen or the unit is unaddressed.

   Therefore, if the unit is unaddressed while a read, write, or status command
   is transferring data, the unit service must be scheduled to end the current
   command.  Unaddressing while an error condition is present merely terminates
   the source or sink operation.


   Implementation notes:

    1. The 13365 manual does not indicate that the controller responds to
       Universal Clear, but the 12992H loader ROM issues this primary and
       expects the controller to initialize.

    2. It is not necessary to check for listening when processing addressed
       commands, as only listeners are called by the bus source.
*/

t_bool da_bus_accept (uint32 unit, uint8 data)
{
const uint8 message_address = data & BUS_ADDRESS;
t_bool accepted = TRUE;
t_bool initiated = FALSE;
t_bool addressed = FALSE;
t_bool stopped_listening = FALSE;
t_bool stopped_talking = FALSE;
char action [40] = "";
uint32 my_address;

if (di [da].bus_cntl & BUS_ATN) {                           /* is it a bus command (ATN asserted)? */
    switch (data & BUS_GROUP) {                             /* dispatch the bus group */

        case BUS_PCG:                                       /* primary command group */
            switch (message_address) {

                case 0x04:                                  /* selected device clear */
                case 0x05:                                  /* SDC with parity freeze */
                case 0x14:                                  /* universal clear */
                    tprintf (da_dev, DEB_RWSC, "Unit %d device cleared\n", unit);

                    sim_cancel (&da_unit [unit]);           /* cancel any in-progress command */
                    dl_idle_controller (&icd_cntlr [unit]); /* idle the controller */
                    if_dsj [unit] = 0;                      /* clear DSJ */
                    if_state [unit] = idle;                 /* idle the interface */
                    di_poll_response (da, unit, SET);       /* enable PPR */

                    if (TRACING (da_dev, DEB_XFER))
                        strcpy (action, "device clear");
                    break;


                default:                                    /* unsupported universal command */
                    break;                                  /* universals are always accepted */
                }

            break;


        case BUS_LAG:                                       /* listen address group */
            my_address = GET_BUSADR (da_unit [unit].flags); /* get my bus address */

            if (message_address == my_address) {            /* is it my listen address? */
                di [da].listeners |= (1 << unit);           /* set my listener bit */
                di [da].talker &= ~(1 << unit);             /* clear my talker bit */

                addressed = TRUE;                           /* unit is now addressed */
                stopped_talking = TRUE;                     /* MLA stops the unit from talking */

                if (TRACING (da_dev, DEB_XFER))
                    sprintf (action, "listen %d", message_address);
                }

            else if (message_address == BUS_UNADDRESS) {    /* is it an Unlisten? */
                di [da].listeners = 0;                      /* clear all of the listeners */

                stopped_listening = TRUE;                   /* UNL stops the unit from listening */

                if (TRACING (da_dev, DEB_XFER))
                    strcpy (action, "unlisten");
                }

            else                                            /* other listen addresses */
                accepted = FALSE;                           /*   are not accepted */

            break;


        case BUS_TAG:                                       /* talk address group */
            my_address = GET_BUSADR (da_unit [unit].flags); /* get my bus address */

            if (message_address == my_address) {            /* is it my talk address? */
                di [da].talker = (1 << unit);               /* set my talker bit and clear the others */
                di [da].listeners &= ~(1 << unit);          /* clear my listener bit */

                addressed = TRUE;                           /* the unit is now addressed */
                stopped_listening = TRUE;                   /* MTA stops the unit from listening */

                if (TRACING (da_dev, DEB_XFER))
                    sprintf (action, "talk %d", message_address);
                }

            else {                                          /* it is some other talker (or Untalk) */
                di [da].talker &= ~(1 << unit);             /* clear my talker bit */

                stopped_talking = TRUE;                     /* UNT or OTA stops the unit from talking */

                if (message_address != BUS_UNADDRESS)       /* other talk addresses */
                    accepted = FALSE;                       /*   are not accepted */

                else                                        /* it's an Untalk */
                    if (TRACING (da_dev, DEB_XFER))
                        strcpy (action, "untalk");
                }

            break;


        case BUS_SCG:                                       /* secondary command group */
            icd_cntlr [unit].index = 0;                     /* reset the buffer index */

            if (di [da].listeners & (1 << unit)) {          /* is it a listen secondary? */
                if (if_state [unit] == write_wait           /* if we're waiting for a write data secondary */
                  && message_address != 0x00)               /*   but it's not there, */
                    abort_command (unit, io_program_error,  /*   then abort the pending command */
                                   idle);                   /*   and process the new command */

                switch (message_address) {                  /* dispatch the listen secondary */

                    case 0x00:                                                  /* Receive Write Data */
                        if (if_state [unit] != write_wait)                      /* if we're not expecting it */
                            abort_command (unit, io_program_error,              /*   abort and sink any data */
                                           error_sink);
                        else {                                                  /* the sequence is correct */
                            if_state [unit] = command_exec;                     /* the command is ready to execute */
                            da_unit [unit].wait = icd_cntlr [unit].cmd_time;    /* schedule the unit */
                            di_bus_control (da, unit, BUS_NRFD, 0);             /* assert NRFD to hold off the card */
                            }

                        initiated = TRUE;                   /* log the command or abort initiation */
                        break;

                    case 0x08:                                  /* disc commands */
                        if_command [unit] = disc_command;       /* set the command and wait */
                        if_state [unit] = opcode_wait;          /*  for the opcode that must follow */
                        break;

                    case 0x09:                                  /* CRC (Listen) */
                        if_command [unit] = crc_listen;         /* set up the command */
                        if_state [unit] = error_sink;           /* sink any data that will be coming */
                        initiated = TRUE;                       /* log the command initiation */
                        break;

                    case 0x10:                                  /* Amigo Clear */
                        if_command [unit] = amigo_clear;        /* set up the command */
                        if_state [unit] = parameter_wait;       /* a parameter must follow */
                        icd_cntlr [unit].length = 1;            /* set to expect one (unused) byte */
                        break;

                    case 0x1E:                                  /* Write Loopback */
                        if_command [unit] = write_loopback;     /* set up the command */
                        if_state [unit] = write_xfer;           /* data will be coming */
                        icd_cntlr [unit].length = 16;           /* accept only the first 16 bytes */
                        initiated = TRUE;                       /* log the command initiation */
                        break;

                    case 0x1F:                                  /* Initiate Self-Test */
                        if_command [unit] = initiate_self_test; /* set up the command */
                        if_state [unit] = parameter_wait;       /* a parameter must follow */
                        icd_cntlr [unit].length = 1;            /* set to expect the test ID byte */
                        break;

                    default:                                    /* an unsupported listen secondary was received */
                        abort_command (unit, io_program_error,  /* abort and sink any data */
                                       error_sink);             /*   that might accompany the command */
                        initiated = TRUE;                       /* log the abort initiation */
                        break;
                    }
                }


            else if (di [da].talker & (1 << unit)) {                /* is it a talk secondary? */
                da_unit [unit].wait = icd_cntlr [unit].cmd_time;    /* these are always scheduled and */
                initiated = TRUE;                                   /*   logged as initiated */

                if (if_state [unit] == read_wait                    /* if we're waiting for a send data secondary */
                  && message_address != 0x00                        /*   but it's not there */
                  || if_state [unit] == status_wait                 /* or a send status secondary, */
                  && message_address != 0x08)                       /*   but it's not there */
                    abort_command (unit, io_program_error,          /*   then abort the pending command */
                                   idle);                           /*   and process the new command */

                switch (message_address) {                          /* dispatch the talk secondary */

                    case 0x00:                                      /* Send Read Data */
                        if (if_state [unit] != read_wait)           /* if we're not expecting it */
                            abort_command (unit, io_program_error,  /*   abort and source a data byte */
                                           error_source);           /*     tagged with EOI */
                        else
                            if_state [unit] = command_exec;         /* the command is ready to execute */
                        break;

                    case 0x08:                                      /* Read Status */
                        if (if_state [unit] != status_wait)         /* if we're not expecting it, */
                            abort_command (unit, io_program_error,  /*   abort and source a data byte */
                                           error_source);           /*     tagged with EOI */
                        else                                        /* all status commands */
                            if_state [unit] = read_xfer;            /*   are ready to transfer data */
                        break;

                    case 0x09:                                      /* CRC (Talk) */
                        if_command [unit] = crc_talk;               /* set up the command */
                        if_state [unit] = read_xfer;                /* data will be going */
                        break;

                    case 0x10:                                      /* Device-Specified Jump */
                        if_command [unit] = device_specified_jump;  /* set up the command */
                        if_state [unit] = read_xfer;                /* data will be going */
                        break;

                    case 0x1E:                                      /* Read Loopback */
                        if_command [unit] = read_loopback;          /* set up the command */
                        if_state [unit] = read_xfer;                /* data will be going */
                        break;

                    case 0x1F:                                          /* Return Self-Test Result */
                        if_command [unit] = return_self_test_result;    /* set up the command */
                        if_state [unit] = read_xfer;                    /* data will be going */
                        icd_cntlr [unit].length = 1;                    /* return one byte that indicates */
                        buffer [0] = 0;                                 /*   that the self-test passed */
                        break;

                    default:                                        /* an unsupported talk secondary was received */
                        abort_command (unit, io_program_error,      /* abort and source a data byte */
                                       error_source);               /*   tagged with EOI */
                        break;
                    }
                }


            else {                                                  /* the unit is not addressed */
                my_address = GET_BUSADR (da_unit [unit].flags);     /* get my bus address */

                if (di [da].talker == 0 && di [da].listeners == 0       /* if there are no talkers or listeners */
                  && message_address == my_address) {                   /*   and this is my secondary address, */
                    if_command [unit] = amigo_identify;                 /*     then this is an Amigo ID sequence */
                    if_state [unit] = command_exec;                     /* set up for execution */
                    da_unit [unit].wait = icd_cntlr [unit].cmd_time;    /* schedule the unit */
                    initiated = TRUE;                                   /* log the command initiation */
                    }

                else                                                /* unaddressed secondaries */
                    accepted = FALSE;                               /*   are not accepted */
                }


            if (accepted) {                                 /* was the command accepted? */
                if (TRACING (da_dev, DEB_XFER))
                    sprintf (action, "secondary %02XH", message_address);

                if (if_command [unit] != amigo_identify)    /* disable PPR for all commands */
                    di_poll_response (da, unit, CLEAR);     /*   except Amigo ID */
                }

            break;                                          /* end of secondary processing */
        }


    if (addressed && sim_is_active (&da_unit [unit])) {     /* is the unit being addressed while it is busy? */
        if_state [unit] = command_wait;                     /* change the interface state to wait */
        di_bus_control (da, unit, BUS_NRFD, 0);             /*   and assert NRFD to hold off the card */

        tprintf (da_dev, DEB_RWSC, "Unit %d addressed while controller is busy\n", unit);
        }

    if (stopped_listening) {                                /* was the unit Unlistened? */
        if (icd_cntlr [unit].state == cntlr_busy)           /* if the controller is busy, */
            complete_write (unit);                          /*   then check for write completion */

        else if (if_command [unit] == invalid)              /* if a command was aborting, */
            complete_abort (unit);                          /*   then complete it */

        else if (if_state [unit] == opcode_wait             /* if waiting for an opcode */
          || if_state [unit] == parameter_wait)             /*   or a parameter, */
            abort_command (unit, io_program_error, idle);   /*   then abort the pending command */
        }

    else if (stopped_talking) {                             /* was the unit Untalked? */
        if (icd_cntlr [unit].state == cntlr_busy)           /* if the controller is busy, */
            complete_read (unit);                           /*   then check for read completion */

        else if (if_command [unit] == invalid)              /* if a command was aborting, */
            complete_abort (unit);                          /*   then complete it */
        }
    }                                                       /* end of bus command processing */


else {                                                      /* it is bus data (ATN is denied) */
    switch (if_state [unit]) {                              /* dispatch the interface state */

        case opcode_wait:                                   /* waiting for an opcode */
            if (TRACING (da_dev, DEB_XFER))
                sprintf (action, "opcode %02XH", data & DL_OPCODE_MASK);

            buffer [0] = TO_WORD (data, 0);                 /* set the opcode into the buffer */

            if (dl_prepare_command (&icd_cntlr [unit],      /* is the command valid? */
                                    da_unit, unit)) {
                if_state [unit] = parameter_wait;           /* set up to get the pad byte */
                icd_cntlr [unit].index = 0;                 /* reset the word index for the next byte */
                icd_cntlr [unit].length =                   /* convert the parameter count to bytes */
                  icd_cntlr [unit].length * 2 + 1;          /*   and include the pad byte */
                }

            else {                                          /* the disc command is invalid */
                abort_command (unit, illegal_opcode,        /* abort the command */
                               error_sink);                 /*   and sink any parameter bytes */
                initiated = TRUE;                           /* log the abort initiation */
                }                                           /* (the unit cannot be busy) */
            break;


        case parameter_wait:                                /* waiting for a parameter */
            if (TRACING (da_dev, DEB_XFER))
                sprintf (action, "parameter %02XH", data);

            put_buffer_byte (&icd_cntlr [unit], data);      /* add the byte to the buffer */

            if (icd_cntlr [unit].length == 0)               /* is this the last parameter? */
                if (di [da].bus_cntl & BUS_EOI)             /* does the host agree? */
                    initiated = start_command (unit);       /* start the command and log the initiation */

                else {                                      /* the parameter count is wrong */
                    abort_command (unit, io_program_error,  /* abort the command and sink */
                                   error_sink);             /*   any additional parameter bytes */
                    initiated = TRUE;                       /* log the abort initiation */
                    }
            break;


        case write_xfer:                                    /* transferring write data */
            if (icd_cntlr [unit].length > 0)                /* if there is more to transfer */
                put_buffer_byte (&icd_cntlr [unit], data);  /*   then add the byte to the buffer */

        /* fall through into the error_sink handler */

        case error_sink:                                        /* sinking data after an error */
            if (TRACING (da_dev, DEB_XFER))
                sprintf (action, "data %03o", data);

            if (di [da].bus_cntl & BUS_EOI)                     /* is this the last byte from the bus? */
                icd_cntlr [unit].eod = SET;                     /* indicate EOD to the controller */

            di_bus_control (da, unit, BUS_NRFD, 0);             /* assert NRFD to hold off the card */

            da_unit [unit].wait = icd_cntlr [unit].data_time;   /* schedule the unit */
            break;


        default:                                            /* data was received in the wrong state */
            abort_command (unit, io_program_error,          /* report the error */
                           error_sink);                     /*   and sink any data that follows */

            if (TRACING (da_dev, DEB_XFER))
                sprintf (action, "unhandled data %03o", data);
            break;
        }
    }


if (accepted)
    tprintf (da_dev, DEB_XFER, "HP-IB address %d accepted %s\n",
             GET_BUSADR (da_unit [unit].flags), action);

if (da_unit [unit].wait > 0)                            /* was service requested? */
    activate_unit (&da_unit [unit]);                    /* schedule the unit */

if (initiated)
    if (if_command [unit] == disc_command)
        tprintf (da_dev, DEB_RWSC, "Unit %d position %" T_ADDR_FMT "d %s disc command initiated\n",
                 unit, da_unit [unit].pos, dl_opcode_name (ICD, icd_cntlr [unit].opcode));
    else
        tprintf (da_dev, DEB_RWSC, "Unit %d %s command initiated\n",
                 unit, if_command_name [if_command [unit]]);

return accepted;                                        /* indicate the acceptance condition */
}


/* Respond to the bus control lines.

   The indicated unit is notified of the new control state on the bus.  There
   are two conditions to which we must respond:

    1. An Interface Clear is initiated.  IFC unaddresses all units, so any
       in-progress disc command must be terminated as if an Untalk and Unlisten
       were accepted from the data bus.

    2. Attention and Not Ready for Data are denied.  A device addressed to talk
       must wait for ATN to deny before data may be sent.  Also, a listener that
       has asserted NRFD must deny it before a talker may send data.  If the
       interface is sending data and both ATN and NRFD are denied, then we
       reschedule the service routine to send the next byte.
*/

void da_bus_respond (CARD_ID card, uint32 unit, uint8 new_cntl)
{
if (new_cntl & BUS_IFC) {                               /* is interface clear asserted? */
    di [da].listeners = 0;                              /* perform an Unlisten */
    di [da].talker = 0;                                 /*   and an Untalk */

    if (icd_cntlr [unit].state == cntlr_busy) {         /* is the controller busy? */
        complete_write (unit);                          /* check for write completion */
        complete_read (unit);                           /*   or read completion */

        if (da_unit [unit].wait > 0)                    /* is service needed? */
            activate_unit (&da_unit [unit]);            /* activate the unit */
        }

    else if (if_command [unit] == invalid)              /* if a command was aborting, */
        complete_abort (unit);                          /*   then complete it */

    else if (if_state [unit] == opcode_wait             /* if we're waiting for an opcode */
      || if_state [unit] == parameter_wait)             /*   or a parameter, */
        abort_command (unit, io_program_error, idle);   /*   then abort the pending command */
    }

if (!(new_cntl & (BUS_ATN | BUS_NRFD))                  /* is the card in data mode and ready for data? */
  && (if_state [unit] == read_xfer                      /* is the interface waiting to send data */
  || if_state [unit] == error_source))                  /*   or source error bytes? */
    da_service (&da_unit [unit]);                       /* start or resume the transfer */
}



/* Amigo disc local utility routines */


/* Start a command with parameters.

   A command that has been waiting for all of its parameters to be received is
   now ready to start.  If this is a disc command, call the disc library to
   validate the parameters and, if they are OK, to start the command.  Status
   commands return the status values in the sector buffer and the number of
   words that were returned in the buffer length, which we convert to a byte
   count.

   If the disc command was accepted, the library returns a pointer to the unit
   to be scheduled.  For an ICD controller, the unit is always the one currently
   addressed, so we simply test if the return is not NULL.  If it isn't, then we
   set the next interface state as determined by the command that is executing.
   For example, a Read command sets the interface to read_wait status in order
   to wait until the accompanying Send Read Data secondary is received.

   If the return is NULL, then the command was rejected, so we set DSJ = 1 and
   leave the interface state in parameter_wait; the controller status will have
   been set to the reason for the rejection.

   If the next interface state is command_exec, then the disc command is ready
   for execution, and we return TRUE to schedule the unit service.  Otherwise,
   we return FALSE, and the appropriate action will be taken by the caller.

   For all other commands, execution begins as soon as the correct parameters
   are received, so we set command_exec state and return TRUE.  (Only Amigo
   Clear and Initiate Self Test require parameters, so they will be the only
   other commands that must be started here.)


   Implementation notes:

    1. As the ICD implementation does not need to differentiate between unit and
       controller commands, the return value from the dl_start_command routine
       is not used other than as an indication of success or failure.
*/

static t_bool start_command (uint32 unit)
{
if (if_command [unit] == disc_command) {                        /* are we starting a disc command? */
    if (dl_start_command (&icd_cntlr [unit], da_unit, unit)) {  /* start the command; was it successful? */
        icd_cntlr [unit].length = icd_cntlr [unit].length * 2;  /* convert the return length from words to bytes */
        if_state [unit] = next_state [icd_cntlr [unit].opcode]; /* set the next interface state */
        }

    else                                                        /* the command was rejected */
        if_dsj [unit] = 1;                                      /*   so indicate an error */

    if (if_state [unit] == command_exec)                        /* if the command is executing */
        return TRUE;                                            /*   activate the unit */

    else {                                                      /* if we must wait */
        da_unit [unit].wait = 0;                                /*   for another secondary, */
        return FALSE;                                           /*   then skip the activation */
        }
    }

else {                                                          /* all other commands */
    if_state [unit] = command_exec;                             /*   execute as soon */
    da_unit [unit].wait = icd_cntlr [unit].cmd_time;            /*     as they */
    return TRUE;                                                /*       are received */
    }
}


/* Abort an in-process command.

   A command sequence partially received via the bus must be aborted.  The cause
   might be an unknown secondary, an illegal disc command opcode, an improper
   secondary sequence (e.g., a Read not followed by Send Read Data), an
   incorrect number of parameters, or unaddressing before the sequence was
   complete.  In any event, the controller and interface are set to an abort
   state, and the DSJ value is set to 1 to indicate an error.
*/

static void abort_command (uint32 unit, CNTLR_STATUS status, IF_STATE state)
{
if_command [unit] = invalid;                            /* indicate an invalid command */
if_state [unit] = state;                                /* set the interface state as directed */
if_dsj [unit] = 1;                                      /* set DSJ to indicate an error condition */
dl_end_command (&icd_cntlr [unit], status);             /* place the disc controller into the wait state */
return;
}


/* Complete an in-process read command.

   An Untalk terminates a Read, Read Full Sector, Read Without Verify, Read With
   Offset, or Cold Load Read command, which must be tied off cleanly by setting
   the end-of-data condition and calling the service routine.  This is required
   only if the read has not already aborted (e.g., for an auto-seek error).

   If a read is in progress, the controller will be busy, and the interface
   state will be either command_exec (if between sectors) or read_xfer (if
   within a sector).  We set up the end phase for the command and schedule the
   disc service to tidy up.

   If a read has aborted, the controller will be waiting, and the interface
   state will be error_source.  In this latter case, we no nothing, as the
   controller has already set the required error status.

   We must be careful NOT to trigger on an Untalk that may follow the opcode and
   precede the Send Read Data sequence.  In this case, the controller will be
   busy, but the interface state will be either read_wait or status_wait.


   Implementation notes:

    1. The test for controller busy is made before calling this routine.  This
       saves the call overhead for the most common case, which is the card is
       being unaddressed after command completion.

    2. There is no need to test if we are processing a disc command, as the
       controller would not be busy otherwise.

    3. If an auto-seek will be needed to continue the read, but the seek will
       fail, then an extra delay is inserted before the service call to start
       the next sector.  Once an Untalk is received, this delay is no longer
       needed, so it is cancelled before rescheduling the service routine.
*/

static void complete_read (uint32 unit)
{
if ((if_state [unit] == command_exec                        /* is a command executing */
  || if_state [unit] == read_xfer)                          /*   or is data transferring */
  && (dl_classify (icd_cntlr [unit]) == class_read          /* and the controller is executing */
  ||  dl_classify (icd_cntlr [unit]) == class_status)) {    /*    a read or status command? */
    icd_cntlr [unit].eod = SET;                             /* set the end of data flag */

    if_state [unit] = command_exec;                         /* set to execute */
    da_unit [unit].PHASE = end_phase;                       /*   the completion phase */

    sim_cancel (&da_unit [unit]);                           /* cancel the EOT delay */
    da_unit [unit].wait = icd_cntlr [unit].data_time;       /* reschedule for completion */
    }

return;
}


/* Complete an in-process write command.

   Normally, the host sends a byte tagged with EOI to end a Write, Write Full
   Sector, or Initialize command.  However, an Unlisten may terminate a write,
   which must be tied off cleanly by setting the end-of-data condition and
   calling the service routine.  This is required only if the write has not
   already aborted (e.g., for a write-protected disc).

   If a write is in progress, the controller will be busy, and the interface
   state will be either command_exec (if between sectors) or write_xfer (if
   within a sector).  We set up the end phase for the command and schedule the
   disc service to tidy up.

   If a write has aborted, the controller will be waiting, and the interface
   state will be error_sink.  In this latter case, we do nothing, as the
   controller has already set the required error status.

   We must be careful NOT to trigger on the Unlisten that may follow the opcode
   and precede the Receive Write Data sequence.  In this case, the controller
   will be busy, but the interface state will be write_wait.


   Implementation notes:

    1. The test for controller busy is made before calling this routine.  This
       saves the call overhead for the most common case, which is the card is
       being unaddressed after command completion.

    2. There is no need to test if we are processing a disc command, as the
       controller would not be busy otherwise.
*/

static void complete_write (uint32 unit)
{
if ((if_state [unit] == command_exec                    /* is a command executing */
  || if_state [unit] == write_xfer)                     /*   or is data transferring */
  && dl_classify (icd_cntlr [unit]) == class_write) {   /* and the controller is executing a write? */
    icd_cntlr [unit].eod = SET;                         /* set the end of data flag */

    if_state [unit] = command_exec;                     /* set to execute */
    da_unit [unit].PHASE = end_phase;                   /*   the completion phase */
    da_unit [unit].wait = icd_cntlr [unit].data_time;   /* ensure that the controller will finish */
    }

return;
}


/* Complete an in-process command abort.

   Errors in the command protocol begin an abort sequence that may involve
   sourcing or sinking bytes to allow the sequence to complete as expected by
   the CPU.  Unaddressing the unit terminates the aborted command.

   If an abort is in progress, and the interface is not idle, the end-of-data
   indication is set, and the disc service routine is called directly to process
   the completion of the abort.  The service routine will terminate the
   error_source or error_sink state cleanly and then idle the interface.


   Implementation notes:

    1. The test for an abort-in-progress is made before calling this routine.
       This saves the call overhead for the most common case, which is the card
       is being unaddressed after normal command completion.
*/

static void complete_abort (uint32 unit)
{
if (if_state [unit] != idle) {                          /* is the interface busy? */
    icd_cntlr [unit].eod = SET;                         /* set the end of data flag */
    da_service (&da_unit [unit]);                       /*   and process the abort completion */
    }

return;
}


/* Get a byte from the sector buffer.

   The next available byte in the sector buffer is returned to the caller.  The
   determination of which byte of the 16-bit buffer word to return is made by
   the polarity of the buffer byte count.  The count always begins with an even
   number, as it is set by doubling the word count returned from the disc
   library.  Therefore, because we decrement the count first, the upper byte is
   indicated by an odd count, and the lower byte is indicated by an even count.
   The buffer index is incremented only after the lower byte is returned.
*/

static uint8 get_buffer_byte (CVPTR cvptr)
{
cvptr->length = cvptr->length - 1;                      /* count the byte */

if (cvptr->length & 1)                                  /* is the upper byte next? */
    return UPPER_BYTE (buffer [cvptr->index]);          /* return the byte */
else                                                    /* the lower byte is next */
    return LOWER_BYTE (buffer [cvptr->index++]);        /* return the byte and bump the word index */
}


/* Put a byte into the sector buffer.

   The supplied byte is stored in the sector buffer.  The determination of which
   byte of the 16-bit buffer word to store is made by the polarity of the buffer
   byte count.  The count always begins with an even number, as it is set by
   doubling the word count returned from the disc library.  Therefore, because
   we decrement the count first, the upper byte is indicated by an odd count,
   and the lower byte is indicated by an even count.  The buffer index is
   incremented only after the lower byte is stored.
*/

static void put_buffer_byte (CVPTR cvptr, uint8 data)
{
cvptr->length = cvptr->length - 1;                      /* count the byte */

if (cvptr->length & 1)                                  /* is the upper byte next? */
    buffer [cvptr->index] = TO_WORD (data, 0);          /* save the byte */
else                                                    /* the lower byte is next */
    buffer [cvptr->index++] |= TO_WORD (0, data);       /* merge the byte and bump the word index */
return;
}


/* Activate the unit.

   The specified unit is activated using the unit's "wait" time.  If debugging
   is enabled, the activation is logged to the debug file.
*/

static t_stat activate_unit (UNIT *uptr)
{
t_stat result;

tprintf (da_dev, DEB_SERV, "Unit %d state %s delay %d service scheduled\n",
         uptr - da_unit, if_state_name [if_state [uptr - da_unit]], uptr->wait);

result = sim_activate (uptr, uptr->wait);               /* activate the unit */
uptr->wait = 0;                                         /* reset the activation time */

return result;                                          /* return the activation status */
}
