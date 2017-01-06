/* hp_disclib.h: HP MAC/ICD disc controller simulator library definitions

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

   Except as contained in this notice, the name of the authors shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the authors.

   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Mar-16    JDB     Added the DL_BUFFER type to define the disc buffer array
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   27-Jul-15    JDB     First revised release version
   21-Feb-15    JDB     Revised for new controller interface model
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   07-May-12    JDB     Added end-of-track delay time as a controller variable
   02-May-12    JDB     First release
   09-Nov-11    JDB     Created disc controller common library from DS simulator


   This file provides the declarations for interoperation between interface
   simulators and the simulation library for the HP 13037 and 13365 disc
   controllers.  It must be included by the interface-specific modules.
*/



#include "hp3000_defs.h"                        /* this must reflect the machine used */



/* Architectural constants.

   The type of the disc buffer element is defined.  This must be a 16-bit array
   for the file representation to be packed.
*/

typedef uint16              DL_BUFFER;          /* a buffer containing 16-bit disc data words */


/* Program limits */

#define DL_MAXDRIVE         7                   /* last valid drive number */
#define DL_AUXUNITS         1                   /* number of MAC auxiliary units required */
#define DL_BUFSIZE          138                 /* required buffer size in words (full sector) */


/* Program constants (cylinders * heads * sectors * words per sector) */

#define WORDS_7905          (411 * 3 * 48 * 128)        /* 7905 capacity =  15 MB */
#define WORDS_7906          (411 * 4 * 48 * 128)        /* 7906 capacity =  20 MB */
#define WORDS_7920          (823 * 5 * 48 * 128)        /* 7920 capacity =  50 MB */
#define WORDS_7925          (823 * 9 * 64 * 128)        /* 7925 capacity = 120 MB */


/* Debug flags */

#define DL_DEB_CMD          (1u << 0)           /* trace controller commands */
#define DL_DEB_INCO         (1u << 1)           /* trace command initiations and completions */
#define DL_DEB_STATE        (1u << 2)           /* trace command execution state changes */
#define DL_DEB_SERV         (1u << 3)           /* trace unit service scheduling calls */
#define DL_DEB_XFER         (1u << 4)           /* trace data reads and writes */
#define DL_DEB_IOB          (1u << 5)           /* trace I/O bus signals and data words */
#define DL_DEB_V_UF         6                   /* first free debug flag bit */


/* Common per-unit disc drive state variables */

#define CYL                 u3                  /* drive cylinder */
#define STATUS              u4                  /* drive status (Status 2) */
#define OPCODE              u5                  /* drive current operation */
#define PHASE               u6                  /* drive current operation phase */


/* Device flags and accessors */

#define DEV_REALTIME_SHIFT  (DEV_V_UF + 0)              /* bits 0-0: timing mode is realistic */

#define DEV_REALTIME        (1u << DEV_REALTIME_SHIFT)  /* realistic timing flag */


/* Unit flags and accessors */

#define UNIT_MODEL_SHIFT    (UNIT_V_UF + 0)             /* bits 0-1: drive model ID */
#define UNIT_PROT_SHIFT     (UNIT_V_UF + 2)             /* bits 2-3  write protection */
#define UNIT_UNLOAD_SHIFT   (UNIT_V_UF + 4)             /* bits 4-4: heads unloaded */
#define UNIT_FMT_SHIFT      (UNIT_V_UF + 5)             /* bits 5-5: format enabled */
#define DL_V_UF             (UNIT_V_UF + 6)             /* first free unit flag bit */

#define UNIT_MODEL_MASK     0000003u                    /* model ID mask */
#define UNIT_PROT_MASK      0000003u                    /* head protection mask */

#define UNIT_MODEL          (UNIT_MODEL_MASK << UNIT_MODEL_SHIFT)
#define UNIT_PROT           (UNIT_PROT_MASK  << UNIT_PROT_SHIFT)
#define UNIT_PROT_L         (1u << UNIT_PROT_SHIFT + 0)
#define UNIT_PROT_U         (1u << UNIT_PROT_SHIFT + 1)
#define UNIT_UNLOAD         (1u << UNIT_UNLOAD_SHIFT)
#define UNIT_FMT            (1u << UNIT_FMT_SHIFT)

#define UNIT_7905           (HP_7905 << UNIT_MODEL_SHIFT)
#define UNIT_7906           (HP_7906 << UNIT_MODEL_SHIFT)
#define UNIT_7920           (HP_7920 << UNIT_MODEL_SHIFT)
#define UNIT_7925           (HP_7925 << UNIT_MODEL_SHIFT)


/* Controller flag and function accessors */

#define DLIFN(C)            ((CNTLR_IFN_SET) ((C) & ~D16_MASK))
#define DLIBUS(C)           ((CNTLR_IBUS) ((C) & D16_MASK))

#define DLNEXTIFN(S)        ((CNTLR_IFN) IOPRIORITY (S))


/* Disc drive types */

typedef enum {
    HP_All  = -1,
    HP_7906 =  0,                               /* these values */
    HP_7920 =  1,                               /*   are hard-coded */
    HP_7905 =  2,                               /*     in the 13037 */
    HP_7925 =  3                                /*       controller microcode */
    } DRIVE_TYPE;


/* Controller types */

typedef enum {
    MAC = 0,
    ICD,
    CS80
    } CNTLR_TYPE;

#define LAST_CNTLR          CS80
#define CNTLR_COUNT         (LAST_CNTLR + 1)


/* Interface flags and function bus orders.

   The CNTLR_FLAG and CNTLR_IFN declarations mirror the hardware signals that
   are received and asserted, respectively, by the 13037 disc controller.  In
   simulation, the interface to which the controller is connected sends a set of
   one or more flags that indicate the state of the interface to the controller.
   The controller then returns a set of one or more functions that request the
   interface to perform certain actions.

   In hardware, a series of functions are sent sequentially to the interface as
   encoded values accompanied by IFVLD (interface function is valid) or IFCLK
   (interface function clock) signals.  In simulation, the functions are decoded
   into a set of function identifiers that are returned to, and then processed
   sequentially by, the interface in order of ascending numerical value.


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

    4. The hardware signal CLEAR is renamed CLEARF in simulation to avoid the
       name clash with the flip-flop CLEAR enumeration value.  (A sensible
       language would allow a definition of CLEAR in each of the enumerations.
       However, C has a single name space for all enumeration constants.)

    5. In hardware, the BUSY command is used in combination with the LSB of the
       data bus to set or clear the interface busy flip-flop.  In simulation,
       separate BUSY (set busy) and FREE (clear busy) commands are used to allow
       the data bus to carry a full 16-bit value.

    6. The DVEND function must come numerically after RQSRV, as DVEND will abort
       the transfer if no retries are left.
*/

typedef enum {                                  /* interface flags */
    CLEARF  = 0000001,                          /*   Clear Controller */
    CMRDY   = 0000002,                          /*   Command Ready */
    DTRDY   = 0000004,                          /*   Data Ready */
    EOD     = 0000010,                          /*   End of Data */
    INTOK   = 0000020,                          /*   Interrupt OK */
    OVRUN   = 0000040,                          /*   Data Overrun */
    XFRNG   = 0000100                           /*   Data Transfer No Good */
    } CNTLR_FLAG;

#define NO_FLAGS            ((CNTLR_FLAG) 0)    /* no flags are asserted */

typedef CNTLR_FLAG          CNTLR_FLAG_SET;     /* a set of CNTLR_FLAGs */


typedef enum {                                  /* interface function bus orders */
    BUSY        = 000000200000,                 /*   Set Interface Busy */
    DSCIF       = 000000400000,                 /*   Disconnect Interface */
    SELIF       = 000001000000,                 /*   Select Interface */
    IFIN        = 000002000000,                 /*   Interface In */
    IFOUT       = 000004000000,                 /*   Interface Out */
    IFGTC       = 000010000000,                 /*   Interface Get Command */
    IFPRF       = 000020000000,                 /*   Interface Prefetch Command */
    RQSRV       = 000040000000,                 /*   Request Service */
    DVEND       = 000100000000,                 /*   Device End */
    SRTRY       = 000200000000,                 /*   Set Retry Counter */
    STDFL       = 000400000000,                 /*   Set Data Flag */
    STINT       = 001000000000,                 /*   Set Interrupt */
    WRTIO       = 002000000000,                 /*   Write TIO Register */
    FREE        = 004000000000                  /*   Set Interface Free */
    } CNTLR_IFN;

#define NO_FUNCTIONS    ((CNTLR_IFN) 0)         /* no functions are asserted */

typedef CNTLR_IFN       CNTLR_IFN_SET;          /* a set of CNTLR_IFNs */


typedef HP_WORD         CNTLR_IBUS;             /* the interface data bus */

#undef  NO_DATA                                 /* remove winsock definition */
#define NO_DATA         ((CNTLR_IBUS) 0)        /* no data asserted */


typedef uint32          CNTLR_IFN_IBUS;         /* a combined interface function set and data bus value */


/* Controller opcodes */

typedef enum {
    Cold_Load_Read         = 000,
    Recalibrate            = 001,
    Seek                   = 002,
    Request_Status         = 003,
    Request_Sector_Address = 004,
    Read                   = 005,
    Read_Full_Sector       = 006,
    Verify                 = 007,
    Write                  = 010,
    Write_Full_Sector      = 011,
    Clear                  = 012,
    Initialize             = 013,
    Address_Record         = 014,
    Request_Syndrome       = 015,
    Read_With_Offset       = 016,
    Set_File_Mask          = 017,
    Invalid_Opcode         = 020,
    Read_Without_Verify    = 022,
    Load_TIO_Register      = 023,
    Request_Disc_Address   = 024,
    End                    = 025,
    Wakeup                 = 026
    } CNTLR_OPCODE;

#define LAST_OPCODE         Wakeup


/* Controller command classifications */

typedef enum {
    Class_Invalid,                                      /* invalid classification */
    Class_Read,                                         /* read classification */
    Class_Write,                                        /* write classification */
    Class_Control,                                      /* control classification */
    Class_Status                                        /* status classification */
    } CNTLR_CLASS;


/* Controller status.

   Not all status values are returned by the library.  The values not currently
   returned are:

    - Illegal_Drive_Type
    - Cylinder_Miscompare
    - Head_Sector_Miscompare
    - Sync_Timeout
    - Correctable_Data_Error
    - Illegal_Spare_Access
    - Defective_Track

   In hardware, all of the above errors, except for Illegal_Drive_Type and
   Correctable_Data_Error, potentially could occur during the address
   verification that precedes normal reads and writes.  In simulation, these
   cannot occur, as sector address headers are not simulated.  However, any
   error may be returned if diagnostic overrides are used.

   In simulation, Uncorrectable_Data_Error is returned by default by the Request
   Syndrome command.  Uncorrectable_Data_Error is also returned if a host I/O
   error occurs on reading or writing.
*/

typedef enum {
    Normal_Completion        = 000,
    Illegal_Opcode           = 001,
    Unit_Available           = 002,
    Illegal_Drive_Type       = 003,
    Cylinder_Miscompare      = 007,
    Uncorrectable_Data_Error = 010,
    Head_Sector_Miscompare   = 011,
    IO_Program_Error         = 012,
    Sync_Timeout             = 013,
    End_of_Cylinder          = 014,
    Data_Overrun             = 016,
    Correctable_Data_Error   = 017,
    Illegal_Spare_Access     = 020,
    Defective_Track          = 021,
    Access_Not_Ready         = 022,
    Status_2_Error           = 023,
    Protected_Track          = 026,
    Unit_Unavailable         = 027,
    Drive_Attention          = 037
    } CNTLR_STATUS;

#define LAST_STATUS         Drive_Attention


/* Controller execution states */

typedef enum {
    Idle_State,                                 /* idle */
    Wait_State,                                 /* command wait */
    Busy_State                                  /* busy */
    } CNTLR_STATE;


/* Unit command phases */

typedef enum {
    Idle_Phase = 0,
    Parameter_Phase,
    Seek_Phase,
    Rotate_Phase,
    Data_Phase,
    Intersector_Phase,
    End_Phase
    } CNTLR_PHASE;

#define LAST_PHASE          End_Phase


/* Diagnostic override entries.

   Diagnostic overrides are used to return controller status values that
   otherwise are not simulated to a diagnostic program.  A table of override
   entries may be set up by the interface simulator if this facility is desired.
*/

typedef struct {
    uint32       cylinder;                      /* matching cylinder address */
    uint32       head;                          /* matching head address */
    uint32       sector;                        /* matching sector address */
    CNTLR_OPCODE opcode;                        /* matching controller opcode */
    uint32       spd;                           /* returned S/P/D flags */
    CNTLR_STATUS status;                        /* returned controller status */
    } DIAG_ENTRY;

#define DL_OVEND            D32_UMAX            /* marker for the end of the current override set */


/* Disc access delays */

typedef struct {
    CNTLR_TYPE   type;                          /* controller type */
    DRIVE_TYPE   drive;                         /* drive type */
    int32        seek_one;                      /* track-to-track seek time */
    int32        seek_full;                     /* full-stroke seek time */
    int32        sector_full;                   /* full sector rotation time */
    int32        data_xfer;                     /* per-word data transfer time */
    int32        intersector_gap;               /* intersector gap time */
    int32        overhead;                      /* controller execution overhead */
    } DELAY_PROPS;

#define DELAY_INIT(sk1,skf,scf,dxfr,isg,ovhd) \
    (CNTLR_TYPE) 0, (DRIVE_TYPE) 0, \
    (sk1), (skf), (scf), (dxfr), (isg), (ovhd)


/* Disc controller state */

typedef struct {
    CNTLR_TYPE         type;                    /* controller type */
    DEVICE            *device;                  /* controlling device pointer */
    CNTLR_STATE        state;                   /* controller state */
    CNTLR_OPCODE       opcode;                  /* controller opcode */
    CNTLR_STATUS       status;                  /* controller status */
    FLIP_FLOP          eoc;                     /* end-of-cylinder flag */
    t_bool             verify;                  /* address verification required */
    uint32             spd_unit;                /* S/P/D flags and unit number */
    uint32             file_mask;               /* file mask */
    uint32             cylinder;                /* cylinder address */
    uint32             head;                    /* head address */
    uint32             sector;                  /* sector address */
    uint32             count;                   /* count of words transferred or to verify */
    uint32             poll_unit;               /* last unit polled for attention */
    DL_BUFFER         *buffer;                  /* data buffer pointer */
    uint32             index;                   /* data buffer current index */
    uint32             length;                  /* data buffer valid length */
    DIAG_ENTRY        *dop_base;                /* pointer to the diagnostic override array */
    int32              dop_index;               /* current diagnostic override entry index */
    DELAY_PROPS       *fastptr;                 /* pointer to the FASTTIME delays */
    const DELAY_PROPS *dlyptr;                  /* current delay property pointer */
    } CNTLR_VARS;

typedef CNTLR_VARS          *CVPTR;             /* pointer to a controller state variable structure */


/* Controller state variable structure initialization.

   The supplied parameters are:

     ctype  - the type of the controller (CNTLR_TYPE)
     dev    - the device on which the controller operates (DEVICE)
     bufptr - a pointer to the data buffer (array of DL_BUFFER)
     doa    - a pointer to the diagnostic override array (array of DIAG_ENTRY)
              or NULL if this facility is not used
     fast   - a pointer to the fast timing values (DELAY_PROPS)
*/

#define CNTLR_INIT(ctype,dev,bufptr,doa,fast) \
          (ctype), &(dev), Idle_State, End, Normal_Completion, \
          CLEAR, FALSE, \
          0, 0, 0, 0, 0, 0, 0, \
          (bufptr), 0, 0, \
          (doa), -1, \
          &(fast), &(fast)


/* Disc controller device register definitions.

   These definitions should be included AFTER any interface-specific registers.

   The supplied parameters are:

     cntlr    - the controller state variable structure (CNTLR_VARS)
     units    - the unit array (array of UNIT)
     numunits - the number of units in the unit array
     buffer   - the sector buffer (array of DL_BUFFER)
     times    - the fast timing values structure (DELAY_PROPS)


   Implementation notes:

    1. The CNTLR_VARS fields "type", "device", "buffer", "dop_base", "fastptr",
       and "dlyptr" do not need to appear in the REG array, as "dlyptr" is reset
       by the dl_attach routine during a RESTORE, and the others are static.

    2. The fast timing structure does not use the controller and drive type
       fields, so they do not appear in hidden registers, as they need not be
       SAVEd or RESTOREd.
*/

#define DL_REGS(cntlr,units,numunits,buffer,times)  \
/*    Macro   Name      Location            Radix  Width        Depth              Flags        */ \
/*    ------  --------  ------------------  -----  -----  -----------------  -----------------  */ \
    { ORDATA (OPCODE,   (cntlr).opcode,              5),                               REG_RO   }, \
    { ORDATA (CSTATS,   (cntlr).status,              5),                               REG_RO   }, \
    { DRDATA (CSTATE,   (cntlr).state,               2),                     PV_LEFT | REG_RO   }, \
    { FLDATA (EOC,      (cntlr).eoc,                 0)                                         }, \
    { FLDATA (VERIFY,   (cntlr).verify,              0)                                         }, \
    { ORDATA (SPDU,     (cntlr).spd_unit,           16)                                         }, \
    { ORDATA (FLMASK,   (cntlr).file_mask,           4)                                         }, \
    { DRDATA (CYL,      (cntlr).cylinder,           16),                     PV_LEFT            }, \
    { DRDATA (HEAD,     (cntlr).head,                6),                     PV_LEFT            }, \
    { DRDATA (SECTOR,   (cntlr).sector,              8),                     PV_LEFT            }, \
    { DRDATA (COUNT,    (cntlr).count,              16),                     PV_LEFT            }, \
    { BRDATA (SECBUF,   (buffer),             8,    16,   DL_BUFSIZE),       REG_A              }, \
    { DRDATA (INDEX,    (cntlr).index,               8),                     PV_LEFT            }, \
    { DRDATA (LENGTH,   (cntlr).length,              8),                     PV_LEFT            }, \
    { DRDATA (POLLU,    (cntlr).poll_unit,           4),                               REG_HRO  }, \
    { DRDATA (DOINDX,   (cntlr).dop_index,          16),                     PV_LEFT | REG_HRO  }, \
                                                                                                   \
/*    Macro   Name    Location                  Width       Flags       */ \
/*    ------  ------  ------------------------  -----  ---------------- */ \
    { DRDATA (TTIME,  (times).seek_one,          24),  PV_LEFT | REG_NZ }, \
    { DRDATA (FTIME,  (times).seek_full,         24),  PV_LEFT | REG_NZ }, \
    { DRDATA (STIME,  (times).sector_full,       24),  PV_LEFT | REG_NZ }, \
    { DRDATA (XTIME,  (times).data_xfer,         24),  PV_LEFT | REG_NZ }, \
    { DRDATA (GTIME,  (times).intersector_gap,   24),  PV_LEFT | REG_NZ }, \
    { DRDATA (OTIME,  (times).overhead,          24),  PV_LEFT | REG_NZ }, \
                                                                           \
/*    Macro   Name     Location            Radix    Width    Offset     Depth           Flags       */ \
/*    ------  -------  ------------------  -----  ---------  ------  -----------  ----------------- */ \
    { URDATA (UCYL,    (units)[0].CYL,      10,      10,       0,    (numunits),  PV_LEFT)          }, \
    { URDATA (UOPCODE, (units)[0].OPCODE,    8,       6,       0,    (numunits),  PV_RZRO | REG_RO) }, \
    { URDATA (USTATUS, (units)[0].STATUS,    2,      32,       0,    (numunits),  PV_RZRO)          }, \
    { URDATA (USTATE,  (units)[0].PHASE,    10,       4,       0,    (numunits),  PV_RZRO | REG_RO) }, \
    { URDATA (UPOS,    (units)[0].pos,      10,   T_ADDR_W,    0,    (numunits),  PV_LEFT | REG_RO) }, \
    { URDATA (UWAIT,   (units)[0].wait,      8,      32,       0,    (numunits),  PV_LEFT)          }


/* Disc controller device modifier definitions.

   These definitions should be included BEFORE any device-specific modifiers.
*/

#define DL_MODS(cntlr,loadvalid,ovcount)    \
/*    Mask Value    Match Value  Print String       Match String  Validation     Display  Descriptor */ \
/*    ------------  -----------  -----------------  ------------  -------------  -------  ---------- */ \
    { UNIT_MODEL,   UNIT_7905,   "7905",            "7905",       &dl_set_model, NULL,    NULL       }, \
    { UNIT_MODEL,   UNIT_7906,   "7906",            "7906",       &dl_set_model, NULL,    NULL       }, \
    { UNIT_MODEL,   UNIT_7920,   "7920",            "7920",       &dl_set_model, NULL,    NULL       }, \
    { UNIT_MODEL,   UNIT_7925,   "7925",            "7925",       &dl_set_model, NULL,    NULL       }, \
                                                                                                        \
    { UNIT_UNLOAD,  0,           "heads loaded",    "LOAD",       &(loadvalid),  NULL,    NULL       }, \
    { UNIT_UNLOAD,  UNIT_UNLOAD, "heads unloaded",  "UNLOAD",     &(loadvalid),  NULL,    NULL       }, \
                                                                                                        \
    { UNIT_FMT,     UNIT_FMT,    "format enabled",  "FORMAT",     NULL,          NULL,    NULL       }, \
    { UNIT_FMT,     0,           "format disabled", "NOFORMAT",   NULL,          NULL,    NULL       }, \
                                                                                                        \
/*    Entry Flags            Value     Print String  Match String    Validation        Display            Descriptor        */ \
/*    -------------------  ----------  ------------  --------------  ----------------  -----------------  ----------------- */ \
    { MTAB_XUN,                1,      "",           "PROTECT",      &dl_set_protect,  &dl_show_protect,  NULL              }, \
    { MTAB_XUN,                0,      NULL,         "UNPROTECT",    &dl_set_protect,  NULL,              NULL              }, \
                                                                                                                               \
    { MTAB_XDV,                0,      NULL,         "FASTTIME",     &dl_set_timing,   NULL,              (void *) &(cntlr) }, \
    { MTAB_XDV,                1,      NULL,         "REALTIME",     &dl_set_timing,   NULL,              (void *) &(cntlr) }, \
    { MTAB_XDV,                0,      "TIMING",     NULL,           NULL,             &dl_show_timing,   (void *) &(cntlr) }, \
                                                                                                                               \
    { MTAB_XDV | MTAB_NMO, (ovcount),  "DIAG",       "DIAGNOSTIC",   &dl_set_diag,     &dl_show_diag,     (void *) &(cntlr) }, \
    { MTAB_XDV,                0,      "",           "NODIAGNOSTIC", &dl_set_diag,     &dl_show_diag,     (void *) &(cntlr) }



/* Disc library global controller routines */

extern CNTLR_IFN_IBUS dl_controller  (CVPTR cvptr, UNIT *uptr, CNTLR_FLAG_SET flags, CNTLR_IBUS data);
extern t_stat         dl_load_unload (CVPTR cvptr, UNIT *uptr, t_bool load);

/* Disc library global utility routines */

extern const char *dl_opcode_name (CNTLR_TYPE   controller, CNTLR_OPCODE opcode);
extern const char *dl_status_name (CNTLR_STATUS status);

/* Disc library global SCP support routines */

extern t_stat dl_attach (CVPTR cvptr, UNIT *uptr, CONST char *cptr);
extern t_stat dl_detach (CVPTR cvptr, UNIT *uptr);

extern t_stat dl_set_model   (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat dl_set_protect (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat dl_set_diag    (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
extern t_stat dl_set_timing  (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

extern t_stat dl_show_protect (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
extern t_stat dl_show_diag    (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
extern t_stat dl_show_timing  (FILE *st, UNIT *uptr, int32 value, CONST void *desc);
