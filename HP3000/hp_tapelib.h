/* hp_tapelib.h: HP magnetic tape controller simulator library definitions

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

   Except as contained in this notice, the name of the authors shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the authors.

   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Mar-16    JDB     Added the TL_BUFFER type to define the tape buffer array
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   11-Nov-15    JDB     First release version
   24-Mar-13    JDB     Created tape controller common library from MS simulator


   This file defines the interface between machine-specific tape drive
   simulators and the common HP tape controller simulation library.  It must be
   included by the controller-specific modules.
*/



#include "hp3000_defs.h"                        /* this must reflect the machine used */

#include "sim_tape.h"



/* Architectural constants.

   The type of the tape buffer element is defined.  This must be an 8-bit array
   for compatibility with the simulator tape support library (sim_tape).
*/

typedef uint8               TL_BUFFER;          /* a buffer containing 8-bit tape data words */


/* Program limits */

#define TL_MAXDRIVE         3                   /* last valid drive number */
#define TL_AUXUNITS         1                   /* number of auxiliary units required */
#define TL_CNTLR_UNIT       (TL_MAXDRIVE + 1)   /* controller unit number */

#define TL_MAXREC           (DV_UMAX + 1)       /* maximum supported tape record size in bytes */
#define TL_BUFSIZE          (TL_MAXREC + 2)     /* buffer size in bytes (including space for CRCC/LRCC) */


/* Debug flags */

#define TL_DEB_CMD          (1u << 0)           /* trace controller commands */
#define TL_DEB_INCO         (1u << 1)           /* trace command initiations and completions */
#define TL_DEB_STATE        (1u << 2)           /* trace command execution state changes */
#define TL_DEB_SERV         (1u << 3)           /* trace unit service scheduling calls */
#define TL_DEB_XFER         (1u << 4)           /* trace data reads and writes */
#define TL_DEB_IOB          (1u << 5)           /* trace I/O bus signals and data words */
#define TL_DEB_V_UF         6                   /* first free debug flag bit */


/* Common per-unit tape drive state variables */

#define PROP                u3                  /* drive properties */
#define STATUS              u4                  /* drive status */
#define OPCODE              u5                  /* drive current operation */
#define PHASE               u6                  /* drive current operation phase */


/* Device flags and accessors */

#define DEV_REALTIME_SHIFT  (DEV_V_UF + 0)              /* bits 0-0: timing mode is realistic */

#define DEV_REALTIME        (1 << DEV_REALTIME_SHIFT)   /* realistic timing flag */


/* Unit flags and accessors.

   The user-defined unit flags are used to store tape unit status that may be
   modified by the user, as follows:

           +---+---+---+---+---+---+---+---+
     [...]   -   -   - | R | O |   model   |
           +---+---+---+---+---+---+---+---+

   Where:

     R     = the unit is rewinding
     O     = the unit is offline
     model = the DRIVE_TYPE enumeration constant for the simulated drive


 Implementation notes:

    1. The hardware REWIND STATUS (SRW) signal is implemented as a unit flag,
       although it may be inferred from unit.OPCODE = Rewind or Rewind_Offline
       and unit.PHASE = Traverse_Phase.  This is for the convenience of testing
       the "unit busy" condition, which exists when a unit is offline or
       rewinding.
*/

#define UNIT_MODEL_SHIFT    (MTUF_V_UF + 0)             /* bits 0-2: drive model ID */
#define UNIT_OFFLINE_SHIFT  (MTUF_V_UF + 3)             /* bits 3-3: unit is offline */
#define UNIT_REW_SHIFT      (MTUF_V_UF + 4)             /* bits 4-4: unit is rewinding */

#define TL_UNIT_V_UF        (MTUF_V_UF + 5)             /* first free unit flag bit */

#define UNIT_MODEL_MASK     0000007u                    /* model ID mask */

#define UNIT_MODEL          (UNIT_MODEL_MASK << UNIT_MODEL_SHIFT)
#define UNIT_OFFLINE        (1u << UNIT_OFFLINE_SHIFT)
#define UNIT_REWINDING      (1u << UNIT_REW_SHIFT)

#define UNIT_7970B          (HP_7970B << UNIT_MODEL_SHIFT)
#define UNIT_7970E          (HP_7970E << UNIT_MODEL_SHIFT)
#define UNIT_7974           (HP_7974  << UNIT_MODEL_SHIFT)
#define UNIT_7978           (HP_7978  << UNIT_MODEL_SHIFT)


/* Controller flag and function accessors */

#define TLIFN(C)            ((CNTLR_IFN_SET) ((C) & ~D16_MASK))
#define TLIBUS(C)           ((CNTLR_IBUS)    ((C) &  D16_MASK))

#define TLNEXTIFN(S)        ((CNTLR_IFN) IOPRIORITY (S))


/* Tape drive types */

typedef enum {
    HP_7970B,                               /* HP 7970B 800 bpi NRZI */
    HP_7970E,                               /* HP 7970E 1600 bpi PE */
    HP_7974,                                /* HP 7974A 800/1600 bpi NRZI/PE */
    HP_7978                                 /* HP 7978A 1600/6250 bpi PE/GCR */
    } DRIVE_TYPE;


/* Controller types */

typedef enum {
    HP_13181,                               /* HP 1000 NRZI controller */
    HP_13183,                               /* HP 1000 PE controller */
    HP_30215,                               /* HP 3000 NRZI/PE controller */
    HP_IB                                   /* HP-IB controller */
    } CNTLR_TYPE;

#define LAST_CNTLR          HP_IB
#define CNTLR_COUNT         (LAST_CNTLR + 1)


/* Interface flags and function bus orders.

   The CNTLR_FLAG and CNTLR_IFN declarations simulate hardware signals that are
   received and asserted, respectively, by the abstract tape controller.  In
   simulation, the interface sends a set of one or more flags that indicates the
   state of the interface to the controller.  The controller then returns a set
   of one or more functions that requests the interface to perform certain
   actions.

   The function set is decoded into a set of function identifiers that are
   returned to, and then processed sequentially by, the interface in order of
   ascending numerical value.


   Implementation notes:

    1. The enumerations describe signals.  A set of signals normally would be
       modeled as an unsigned integer, as a set may contain more than one
       signal.  However, we define a set as the enumeration, as the "gdb"
       debugger has special provisions for an enumeration of discrete bit values
       and will display the set in "ORed" form.

    2. The null sets -- NO_FLAGS and NO_FUNCTIONS -- cannot be defined as
       enumeration constants, as including them would require handlers for them
       in "switch" statements, which is undesirable.  Therefore, we define them
       as an explicit integer zero values compatible with the enumerations.

    3. Function bus values are restricted to the upper 16 bits to allow the
       combined function and data value to fit in 32 bits.
*/

typedef enum {                                  /* interface flags */
    CMRDY   = 0000001,                          /*   Command Ready */
    CMXEQ   = 0000002,                          /*   Command Execute */
    DTRDY   = 0000004,                          /*   Data Ready */
    EOD     = 0000010,                          /*   End of Data */
    INTOK   = 0000020,                          /*   Interrupt OK */
    OVRUN   = 0000040,                          /*   Data Overrun */
    XFRNG   = 0000100                           /*   Data Transfer No Good */
    } CNTLR_FLAG;

#define NO_FLAGS        (CNTLR_FLAG) 0          /* no flags are asserted */

typedef CNTLR_FLAG      CNTLR_FLAG_SET;         /* a set of CNTLR_FLAGs */


typedef enum {                                  /* interface function bus orders */
    IFIN        = 000000200000,                 /*   Interface In */
    IFOUT       = 000000400000,                 /*   Interface Out */
    IFGTC       = 000001000000,                 /*   Interface Get Command */
    SCPE        = 000002000000,                 /*   SCP Error Status */
    RQSRV       = 000004000000,                 /*   Request Service */
    DVEND       = 000010000000,                 /*   Device End */
    STCFL       = 000020000000,                 /*   Set Control Flag */
    STDFL       = 000040000000,                 /*   Set Data Flag */
    STINT       = 000100000000,                 /*   Set Interrupt */
    DATTN       = 000200000000                  /*   Drive Attention */
    } CNTLR_IFN;

#define NO_FUNCTIONS    (CNTLR_IFN) 0           /* no functions are asserted */

typedef CNTLR_IFN       CNTLR_IFN_SET;          /* a set of CNTLR_IFNs */


typedef HP_WORD CNTLR_IBUS;                     /* the interface data bus */

#undef  NO_DATA                                 /* remove winsock definition */
#define NO_DATA         (CNTLR_IBUS) 0          /* no data asserted */


typedef uint32 CNTLR_IFN_IBUS;                  /* a combined interface function set and data bus value */


/* Controller opcodes.

   Each tape interface uses its own encoding for tape commands that must be
   translated to the appropriate CNTLR_OPCODEs of the common set, typically via
   a lookup table.


   Implementation notes:

    1. The Select Unit 0-3 opcodes must have contiguous values, so that the
       value of the selected unit may be added to the Select_Unit_0 value to
       obtain the correct opcode.

    2. The Invalid Opcode must be the last enumeration value.
*/

typedef enum {
    Select_Unit_0,
    Select_Unit_1,
    Select_Unit_2,
    Select_Unit_3,
    Clear_Controller,
    Read_Record,
    Read_Record_with_CRCC,
    Read_Record_Backward,
    Read_File_Forward,
    Write_Record,
    Write_Record_without_Parity,
    Write_File_Mark,
    Write_Gap,
    Write_Gap_and_File_Mark,
    Forward_Space_Record,
    Forward_Space_File,
    Backspace_Record,
    Backspace_File,
    Rewind,
    Rewind_Offline,
    Invalid_Opcode
    } CNTLR_OPCODE;


/* Controller opcode classifications */

typedef enum {
    Class_Invalid,                              /* invalid classification */
    Class_Read,                                 /* read classification */
    Class_Write,                                /* write classification */
    Class_Rewind,                               /* rewind classification */
    Class_Control                               /* control classification */
    } CNTLR_CLASS;


/* Controller execution states.

   The controller is in the idle state while it is awaiting a command.  It is in
   the busy state while it is executing a command.  The end and error states are
   busy states in which a device end or a device error, respectively, has
   occurred while executing the command.  A device end occurs when a record
   shorter than the requested length is read.  A device error occurs when a
   simulator tape support library routine returns an error (e.g., a read of a
   tape mark or of a record that is marked bad), or a data overrun is detected
   by the interface.  In these cases, command execution completes normally, but
   notification is given that the channel order or program should be aborted.


   Implementation notes:

    1. The error states (End_State and Error_State) must be numerically greater
       than the non-error states (Idle_State and Busy_State).
*/

typedef enum {
    Idle_State,                                 /* idle */
    Busy_State,                                 /* busy */
    End_State,                                  /* device end */
    Error_State                                 /* device error */
    } CNTLR_STATE;


/* Tape activation delays structure.

   The simulation models the mechanical delays of the tape drive as timed events
   that are scheduled by unit command phase transitions.  For example, a tape
   record read is modeled as a start phase, a data transfer phase, and a stop
   phase.  These correspond to the motion of the physical tape as it starts and
   ramps up to speed while crossing the interrecord gap, passes the data record
   over the read head, and then slows the tape to a stop in the next interrecord
   gap.  Separate structures contain the delays for realistic and optimized
   timing for the various tape drive types.

   The structure contains these fields:

     rewind_start -- the time from rewind initiation to controller idle
     rewind_rate  -- the travel time per inch during rewinding
     rewind_stop  -- the time from BOT detection to load point search completion
     bot_start    -- the time starting from the BOT marker to the data block
     ir_start     -- the time starting from the IR gap to the data block
     data_xfer    -- the travel time from one data byte to the next
     overhead     -- the controller execution time from command to first motion

   The bot_start and ir_start values include the drive start/stop time and the
   traverse time across the initial gap and one-half of the interrecord gap,
   respectively.  The ir_start value doubles as the stop time.
*/

typedef struct {
    int32 rewind_start;                         /* rewind initiation time */
    int32 rewind_rate;                          /* rewind time per inch */
    int32 rewind_stop;                          /* rewind completion time */
    int32 bot_start;                            /* beginning of tape gap traverse time */
    int32 ir_start;                             /* interrecord traverse time */
    int32 data_xfer;                            /* per-byte data transfer time */
    int32 overhead;                             /* controller execution overhead */
    } DELAY_PROPS;

#define DELAY_INIT(rstart,rrate,rstop,bot,ir,dxfr,ovhd) \
          (rstart), (rrate), (rstop), (bot), (ir), (dxfr), (ovhd)


/* Tape controller state */

typedef struct {
    CNTLR_TYPE        type;                     /* controller type */
    DEVICE            *device;                  /* controlling device pointer */
    CNTLR_STATE       state;                    /* controller state */
    uint32            status;                   /* controller status */
    uint32            unit_selected;            /* unit number currently selected */
    uint32            unit_attention;           /* bitmap of units needing attention */
    TL_BUFFER         *buffer;                  /* data buffer pointer */
    t_stat            call_status;              /* simulator tape support library call status */
    t_mtrlnt          length;                   /* data buffer valid length */
    t_mtrlnt          index;                    /* data buffer current index */
    t_mtrlnt          gaplen;                   /* current record erase gap length */
    t_addr            initial_position;         /* tape motion initial position */
    DELAY_PROPS       *fastptr;                 /* pointer to the FASTTIME delays */
    const DELAY_PROPS *dlyptr;                  /* current delay property pointer */
    } CNTLR_VARS;

typedef CNTLR_VARS *CVPTR;                      /* a pointer to a controller state variable structure */


/* Controller state variable structure initialization.

   The supplied parameters are:

     ctype  - the type of the controller (CNTLR_TYPE)
     dev    - the device on which the controller operates (DEVICE)
     bufptr - a pointer to the data buffer (array of TL_BUFFER)
     fast   - a pointer to the fast timing values (DELAY_PROPS)
*/

#define CNTLR_INIT(ctype,dev,bufptr,fast) \
          (ctype), &(dev), Idle_State, 0, 0, 0, \
          (bufptr), MTSE_OK, 0, 0, 0, 0, \
          &(fast), &(fast)


/* Tape controller device register definitions.

   These definitions should be included AFTER any interface-specific registers.

   The supplied parameters are:

     cntlr    -- the controller state variable structure (CNTLR_VARS)
     units    -- the unit array (array of UNIT)
     numunits -- the number of tape drive units
     buffer   -- the buffer array (array of TL_BUFFER)
     times    -- the structure containing the fast delay time values (DELAY_PROPS)


   Implementation notes:

    1. The CNTLR_VARS fields "type", "device", "buffer", "fastptr", and "dlyptr"
       do not need to appear in the REG array, as "dlyptr" is reset by the
       tl_attach routine during a RESTORE, and the others are static.
*/

#define TL_REGS(cntlr,units,numunits,buffer,times)  \
/*    Macro   Name    Location                  Radix   Width        Depth             Flags         */ \
/*    ------  ------  ------------------------  -----  --------  ---------------  -----------------  */ \
    { DRDATA (CSTATE, (cntlr).state,                      4),                     PV_LEFT | REG_RO   }, \
    { ORDATA (STATUS, (cntlr).status,                    16),                               REG_RO   }, \
    { DRDATA (USEL,   (cntlr).unit_selected,              4),                     PV_LEFT | REG_RO   }, \
    { YRDATA (UATTN,  (cntlr).unit_attention,             4,                      PV_RZRO)           }, \
    { BRDATA (RECBUF, (buffer),                   8,      8,       TL_BUFSIZE),   REG_A              }, \
    { DRDATA (LIBSTA, (cntlr).call_status,               16),                     PV_LEFT            }, \
    { DRDATA (LENGTH, (cntlr).length,                    24),                     PV_LEFT            }, \
    { DRDATA (INDEX,  (cntlr).index,                     24),                     PV_LEFT            }, \
    { DRDATA (GAPLEN, (cntlr).gaplen,                    32),                     PV_LEFT            }, \
    { DRDATA (INPOS,  (cntlr).initial_position,        T_ADDR_W),                 PV_LEFT            }, \
                                                                                                        \
/*    Macro   Name     Location              Width       Flags       */ \
/*    ------  -------  --------------------  -----  ---------------- */ \
    { DRDATA (RSTART,  (times).rewind_start,  24),  PV_LEFT | REG_NZ }, \
    { DRDATA (RRATE,   (times).rewind_rate,   24),  PV_LEFT | REG_NZ }, \
    { DRDATA (RSTOP,   (times).rewind_stop,   24),  PV_LEFT | REG_NZ }, \
    { DRDATA (BTIME,   (times).bot_start,     24),  PV_LEFT | REG_NZ }, \
    { DRDATA (ITIME,   (times).ir_start,      24),  PV_LEFT | REG_NZ }, \
    { DRDATA (DTIME,   (times).data_xfer,     24),  PV_LEFT | REG_NZ }, \
    { DRDATA (OTIME,   (times).overhead,      24),  PV_LEFT | REG_NZ }, \
                                                                        \
/*    Macro   Name     Location           Radix    Width   Offset     Depth          Flags        */ \
/*    ------  -------  -----------------  -----  --------  ------  ----------  ------------------ */ \
    { URDATA (UPROP,   (units)[0].PROP,     8,      16,      0,    (numunits), PV_RZRO)           }, \
    { URDATA (USTATUS, (units)[0].STATUS,   2,      16,      0,    (numunits), PV_RZRO)           }, \
    { URDATA (UOPCODE, (units)[0].OPCODE,  10,       6,      0,    (numunits), PV_LEFT | REG_RO)  }, \
    { URDATA (USTATE,  (units)[0].PHASE,   10,       4,      0,    (numunits), PV_LEFT | REG_RO)  }, \
    { URDATA (UPOS,    (units)[0].pos,     10,   T_ADDR_W,   0,    (numunits), PV_LEFT | REG_RO)  }, \
    { URDATA (UWAIT,   (units)[0].wait,    10,      32,      0,    (numunits), PV_LEFT | REG_HRO) }


/* Tape controller device modifier structure initialization.

   This initialization should be included BEFORE any device-specific modifiers.

   The supplied parameters are:

     cntlr    -- the controller state variable structure (CNTLR_VARS)
     typeset  -- the set of drive type flags supported by the interface
     densset  -- the set of drive density flags supported by the interface
     offvalid -- the interface SET ONLINE/OFFLINE validation function


   Implementation notes:

    1. The IFTYPE and IFDENSITY macros test their respective flag sets and
       enable or disable the associated modifier entries as indicated.  An entry
       is disabled by setting the print and match strings to NULL; this ensures
       that the modifier is neither printed nor matched against any user input.

    2. The UNIT_RO modifier displays "write ring" if the flag is not set.  There
       is no corresponding entry for the opposite condition because "read only"
       is automatically printed after the attached filename.
*/

/* Selectable drive type flags */

#define TL_7970B        (1u << HP_7970B)
#define TL_7970E        (1u << HP_7970E)
#define TL_7974         (1u << HP_7974)
#define TL_7978         (1u << HP_7978)

/* Selectable drive density flags */

#define TL_FIXED        0
#define TL_800          (1u << MT_DENS_800)
#define TL_1600         (1u << MT_DENS_1600)
#define TL_6250         (1u << MT_DENS_6250)

#define IFTYPE(t,s)     (TL_##t & (s) ? #t : NULL), \
                        (TL_##t & (s) ? #t : NULL)

#define IFDENSITY(s)    ((s) ? "DENSITY" : NULL), \
                        ((s) ? "DENSITY" : NULL)

#define TL_MODS(cntlr,typeset,densset,offvalid)  \
/*    Mask Value   Match Value  Print String  Match String  Validation     Display  Descriptor        */ \
/*    -----------  -----------  ------------  ------------  -------------  -------  ----------------- */ \
    { UNIT_MODEL,  UNIT_7970B,  IFTYPE (7970B, typeset),    &tl_set_model, NULL,    (void *) &(cntlr) }, \
    { UNIT_MODEL,  UNIT_7970E,  IFTYPE (7970E, typeset),    &tl_set_model, NULL,    (void *) &(cntlr) }, \
    { UNIT_MODEL,  UNIT_7974,   IFTYPE (7974,  typeset),    &tl_set_model, NULL,    (void *) &(cntlr) }, \
    { UNIT_MODEL,  UNIT_7978,   IFTYPE (7978,  typeset),    &tl_set_model, NULL,    (void *) &(cntlr) }, \
                                                                                                         \
/*    Entry Flags          Value  Print String  Match String  Validation        Display            Descriptor        */ \
/*    -------------------  -----  ------------  ------------  ----------------  -----------------  ----------------- */ \
    { MTAB_XUN,              0,   IFDENSITY (densset),        &tl_set_density,  &tl_show_density,  (void *) &(cntlr) }, \
    { MTAB_XUN,              0,   "CAPACITY",   "CAPACITY",   &tl_set_reelsize, &tl_show_reelsize, NULL              }, \
    { MTAB_XUN | MTAB_NMO,   1,   "REEL",       "REEL",       &tl_set_reelsize, &tl_show_reelsize, NULL              }, \
                                                                                                                        \
/*    Mask Value    Match Value   Print String  Match String  Validation   Display  Descriptor        */ \
/*    ------------  ------------  ------------  ------------  -----------  -------  ----------------- */ \
    { UNIT_OFFLINE, UNIT_OFFLINE, "offline",    "OFFLINE",    &(offvalid), NULL,    NULL              }, \
    { UNIT_OFFLINE, 0,            "online",     "ONLINE",     &(offvalid), NULL,    (void *) &(cntlr) }, \
                                                                                                         \
    { UNIT_RO,      0,            "write ring", NULL,         NULL,        NULL,    NULL              }, \
                                                                                                         \
/*    Entry Flags   Value  Print String  Match String  Validation         Display             Descriptor        */ \
/*    ------------  -----  ------------  ------------  -----------------  ------------------  ----------------- */ \
    { MTAB_XDV,       0,   "TIMING",     "FASTTIME",   &tl_set_timing,    &tl_show_timing,    (void *) &(cntlr) }, \
    { MTAB_XDV,       1,   NULL,         "REALTIME",   &tl_set_timing,    NULL,               (void *) &(cntlr) }, \
                                                                                                                   \
    { MTAB_XUN,       0,   "FORMAT",     "FORMAT",     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL              }


/* Tape library global controller routines */

extern CNTLR_IFN_IBUS tl_controller (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data);
extern t_stat         tl_onoffline  (CVPTR cvptr, UNIT *uptr, t_bool online);

extern HP_WORD tl_status (CVPTR cvptr);
extern t_stat  tl_reset  (CVPTR cvptr);
extern void    tl_clear  (CVPTR cvptr);

/* Tape library global utility routines */

extern const char *tl_opcode_name (CNTLR_OPCODE opcode);
extern const char *tl_unit_name   (int32        unit);

/* Tape library global SCP support routines */

extern t_stat tl_attach (CVPTR cvptr, UNIT *uptr, CONST char *cptr);
extern t_stat tl_detach (UNIT  *uptr);

extern t_stat tl_set_timing   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat tl_set_model    (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat tl_set_density  (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat tl_set_reelsize (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

extern t_stat tl_show_timing   (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
extern t_stat tl_show_density  (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
extern t_stat tl_show_reelsize (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
