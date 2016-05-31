/* hp_disclib.h: HP MAC/ICD disc controller simulator library definitions

   Copyright (c) 2011-2016, J. David Bryan
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

   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   07-May-12    JDB     Added end-of-track delay time as a controller variable
   02-May-12    JDB     First release
   09-Nov-11    JDB     Created disc controller common library from DS simulator


   This file defines the interface between interface simulators and the
   simulation library for the HP 13037 and 13365 disc controllers.  It must be
   included by the interface-specific modules (DA, DS, etc.).
*/



#include "hp2100_defs.h"



/* Program limits */

#define DL_MAXDRIVE      7                              /* last valid drive number */
#define DL_MAXUNIT      10                              /* last legal unit number */

#define DL_AUXUNITS      2                              /* number of MAC auxiliary units required */

#define DL_WPSEC        128                             /* words per normal sector */
#define DL_WPFSEC       138                             /* words per full sector */
#define DL_BUFSIZE      DL_WPFSEC                       /* required buffer size in words */


/* Default controller times */

#define DL_EOT_TIME     160                             /* end-of-track delay time */
#define DL_SEEK_TIME    100                             /* seek delay time (per cylinder) */
#define DL_SECTOR_TIME   27                             /* intersector delay time */
#define DL_CMD_TIME       3                             /* command start delay time */
#define DL_DATA_TIME      1                             /* data transfer delay time */

#define DL_WAIT_TIME    2749200                         /* command wait timeout (1.74 seconds) */


/* Common per-unit disc drive state variables */

#define CYL             u3                              /* current drive cylinder */
#define STAT            u4                              /* current drive status (Status 2) */
#define OP              u5                              /* current drive operation in process */
#define PHASE           u6                              /* current drive operation phase */


/* Unit flags and accessors */

#define UNIT_V_MODEL    (UNIT_V_UF + 0)                 /* bits 1-0: model ID */
#define UNIT_V_WLK      (UNIT_V_UF + 2)                 /* bits 2-2: write locked (protect switch) */
#define UNIT_V_UNLOAD   (UNIT_V_UF + 3)                 /* bits 3-3: heads unloaded */
#define UNIT_V_FMT      (UNIT_V_UF + 4)                 /* bits 4-4: format enabled */
#define UNIT_V_AUTO     (UNIT_V_UF + 5)                 /* bits 5-5: autosize */
#define DL_V_UF         (UNIT_V_UF + 6)                 /* first free unit flag bit */

#define UNIT_M_MODEL    03                              /* model ID mask */

#define UNIT_MODEL      (UNIT_M_MODEL << UNIT_V_MODEL)
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_UNLOAD     (1 << UNIT_V_UNLOAD)
#define UNIT_FMT        (1 << UNIT_V_FMT)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)

#define UNIT_WPROT      (UNIT_WLK | UNIT_RO)            /* write protected if locked or read-only */

#define GET_MODEL(t)    (((t) >> UNIT_V_MODEL) & UNIT_M_MODEL)
#define SET_MODEL(t)    (((t) & UNIT_M_MODEL) << UNIT_V_MODEL)


/* Status-1 accessors */

#define DL_V_S1SPD      13                              /* bits 15-13: S/P/D flags */
#define DL_V_S1STAT      8                              /* bits 12- 8: controller status */
#define DL_V_S1UNIT      0                              /* bits  3- 0: last unit number */

#define DL_M_S1UNIT     017                             /* unit number mask */

#define GET_S1UNIT(v)   (((v) >> DL_V_S1UNIT) & DL_M_S1UNIT)

#define SET_S1SPD(v)    ((v) << DL_V_S1SPD)
#define SET_S1STAT(v)   ((v) << DL_V_S1STAT)
#define SET_S1UNIT(v)   ((v) << DL_V_S1UNIT)


/* Status-2 accessors (+ = kept in unit status, - = determined dynamically) */

#define DL_V_S2ERR      15                              /* bits 15-15: (-) any error flag */
#define DL_V_S2DTYP      9                              /* bits 12- 9: (-) drive type */
#define DL_V_S2ATN       7                              /* bits  7- 7: (+) attention flag */
#define DL_V_S2RO        6                              /* bits  6- 6: (-) read only flag */
#define DL_V_S2FMT       5                              /* bits  5- 5: (-) format enabled flag */
#define DL_V_S2FAULT     4                              /* bits  4- 4: (+) drive fault flag */
#define DL_V_S2FS        3                              /* bits  3- 3: (+) first status flag */
#define DL_V_S2SC        2                              /* bits  2- 2: (+) seek check flag */
#define DL_V_S2NR        1                              /* bits  1- 1: (-) not ready flag */
#define DL_V_S2BUSY      0                              /* bits  0- 1: (-) drive busy flag */

#define DL_S2ERR        (1 << DL_V_S2ERR)
#define DL_S2DTYP       (1 << DL_V_S2DTYP)
#define DL_S2ATN        (1 << DL_V_S2ATN)
#define DL_S2RO         (1 << DL_V_S2RO)
#define DL_S2FMT        (1 << DL_V_S2FMT)
#define DL_S2FAULT      (1 << DL_V_S2FAULT)
#define DL_S2FS         (1 << DL_V_S2FS)
#define DL_S2SC         (1 << DL_V_S2SC)
#define DL_S2NR         (1 << DL_V_S2NR)
#define DL_S2BUSY       (1 << DL_V_S2BUSY)

#define DL_S2STOPS      (DL_S2FAULT | DL_S2SC | DL_S2NR)                /* bits that stop drive access */
#define DL_S2ERRORS     (DL_S2FAULT | DL_S2SC | DL_S2NR | DL_S2BUSY)    /* bits that set S2ERR */
#define DL_S2CPS        (DL_S2ATN | DL_S2FAULT | DL_S2FS | DL_S2SC)     /* bits cleared by Controller Preset */


/* Drive properties.

   The controller library supports four different disc drive models with these
   properties:

      Drive  Model  Drive  Sectors   Heads per  Cylinders  Megabytes
      Model   ID    Type   per Head  Cylinder   per Drive  per Drive
      -----  -----  -----  --------  ---------  ---------  ---------
      7905     0      2       48         3         411         15
      7906     1      0       48         4         411         20
      7920     2      1       48         5         823         50
      7925     3      3       64         9         823        120

   The Drive Type is reported by the controller in the second status word
   (Status-2) returned by the Request Status command.

   Model IDs are used in the unit flags to identify the unit's model.  For the
   autosizing feature to work, models must be assigned ascending IDs in order of
   ascending drive sizes.
*/

#define D7905_MODEL       0
#define D7905_SECTS      48
#define D7905_HEADS       3
#define D7905_CYLS      411
#define D7905_TYPE      (2 << DL_V_S2DTYP)
#define D7905_WORDS     (D7905_SECTS * D7905_HEADS * D7905_CYLS * DL_WPSEC)

#define D7906_MODEL       1
#define D7906_SECTS      48
#define D7906_HEADS       4
#define D7906_CYLS      411
#define D7906_TYPE      (0 << DL_V_S2DTYP)
#define D7906_WORDS     (D7906_SECTS * D7906_HEADS * D7906_CYLS * DL_WPSEC)

#define D7920_MODEL       2
#define D7920_SECTS      48
#define D7920_HEADS       5
#define D7920_CYLS      823
#define D7920_TYPE      (1 << DL_V_S2DTYP)
#define D7920_WORDS     (D7920_SECTS * D7920_HEADS * D7920_CYLS * DL_WPSEC)

#define D7925_MODEL       3
#define D7925_SECTS      64
#define D7925_HEADS       9
#define D7925_CYLS      823
#define D7925_TYPE      (3 << DL_V_S2DTYP)
#define D7925_WORDS     (D7925_SECTS * D7925_HEADS * D7925_CYLS * DL_WPSEC)

#define MODEL_7905      SET_MODEL (D7905_MODEL)
#define MODEL_7906      SET_MODEL (D7906_MODEL)
#define MODEL_7920      SET_MODEL (D7920_MODEL)
#define MODEL_7925      SET_MODEL (D7925_MODEL)


/* Controller types */

typedef enum {
    MAC = 0,
    ICD,
    last_type = ICD,                                    /* last valid type */
    type_count                                          /* count of controller types */
    } CNTLR_TYPE;


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
    Wakeup                 = 026,
    Last_Opcode            = Wakeup                     /* last valid opcode */
    } CNTLR_OPCODE;

#define DL_OPCODE_MASK  037


/* Controller command phases */

typedef enum {
    start_phase = 0,
    data_phase,
    end_phase,
    last_phase = end_phase                              /* last valid phase */
    } CNTLR_PHASE;


/* Controller status.

   Not all status values are returned by the library.  The values not currently
   returned are:

    - illegal_drive_type
    - cylinder_miscompare
    - head_sector_miscompare
    - io_program_error
    - sync_timeout
    - correctable_data_error
    - illegal_spare_access
    - defective_track
    - protected_track
*/

typedef enum {
    normal_completion        = 000,
    illegal_opcode           = 001,
    unit_available           = 002,
    illegal_drive_type       = 003,
    cylinder_miscompare      = 007,
    uncorrectable_data_error = 010,
    head_sector_miscompare   = 011,
    io_program_error         = 012,
    sync_timeout             = 013,
    end_of_cylinder          = 014,
    data_overrun             = 016,
    correctable_data_error   = 017,
    illegal_spare_access     = 020,
    defective_track          = 021,
    access_not_ready         = 022,
    status_2_error           = 023,
    protected_track          = 026,
    unit_unavailable         = 027,
    drive_attention          = 037
    } CNTLR_STATUS;


/* Controller execution states */

typedef enum {
    cntlr_idle,                                         /* idle */
    cntlr_wait,                                         /* command wait */
    cntlr_busy                                          /* busy */
    } CNTLR_STATE;


/* Controller command classifications */

typedef enum {
    class_invalid,                                      /* invalid classification */
    class_read,                                         /* read classification */
    class_write,                                        /* write classification */
    class_control,                                      /* control classification */
    class_status                                        /* status classification */
    } CNTLR_CLASS;


/* Controller clear types */

typedef enum {
    hard_clear,                                         /* power-on/preset hard clear */
    soft_clear                                          /* programmed soft clear */
    } CNTLR_CLEAR;


/* Controller state variables */

typedef struct {
    CNTLR_TYPE   type;                                  /* controller type */
    CNTLR_STATE  state;                                 /* controller state */
    CNTLR_OPCODE opcode;                                /* controller opcode */
    CNTLR_STATUS status;                                /* controller status */
    FLIP_FLOP    eoc;                                   /* end-of-cylinder flag */
    FLIP_FLOP    eod;                                   /* end-of-data flag */
    uint32       spd_unit;                              /* S/P/D flags and unit number */
    uint32       file_mask;                             /* file mask */
    uint32       retry;                                 /* retry counter */
    uint32       cylinder;                              /* cylinder address */
    uint32       head;                                  /* head address */
    uint32       sector;                                /* sector address */
    uint32       verify_count;                          /* count of sectors to verify */
    uint32       poll_unit;                             /* last unit polled for attention */
    uint16      *buffer;                                /* data buffer pointer */
    uint32       index;                                 /* data buffer current index */
    uint32       length;                                /* data buffer valid length */
    UNIT        *aux;                                   /* MAC auxiliary units (controller and timer) */
    int32        eot_time;                              /* end-of-track read delay time */
    int32        seek_time;                             /* per-cylinder seek delay time */
    int32        sector_time;                           /* intersector delay time */
    int32        cmd_time;                              /* command response time */
    int32        data_time;                             /* data transfer response time */
    int32        wait_time;                             /* command wait time */
    } CNTLR_VARS;


typedef CNTLR_VARS *CVPTR;                              /* pointer to controller state variables */

/* Controller state variables initialization.

   The parameters are:

     ctype  - type of the controller (CNTLR_TYPE)
     bufptr - pointer to the data buffer
     auxptr - pointer to the auxiliary units (MAC only; NULL for ICD)
*/

#define CNTLR_INIT(ctype,bufptr,auxptr) \
          (ctype), cntlr_idle, End, normal_completion, \
          CLEAR, CLEAR, \
          0, 0, 0, 0, 0, 0, 0, 0, \
          (bufptr), 0, 0, (auxptr), \
          DL_EOT_TIME, DL_SEEK_TIME, DL_SECTOR_TIME, \
          DL_CMD_TIME, DL_DATA_TIME, DL_WAIT_TIME



/* Disc library global controller routines */

extern t_bool  dl_prepare_command    (CVPTR cvptr, UNIT *units, uint32 unit_limit);
extern UNIT   *dl_start_command      (CVPTR cvptr, UNIT *units, uint32 unit_limit);
extern void    dl_end_command        (CVPTR cvptr, CNTLR_STATUS status);
extern t_bool  dl_poll_drives        (CVPTR cvptr, UNIT *units, uint32 unit_limit);
extern t_stat  dl_service_drive      (CVPTR cvptr, UNIT *uptr);
extern t_stat  dl_service_controller (CVPTR cvptr, UNIT *uptr);
extern t_stat  dl_service_timer      (CVPTR cvptr, UNIT *uptr);
extern void    dl_idle_controller    (CVPTR cvptr);
extern t_stat  dl_clear_controller   (CVPTR cvptr, UNIT *uptr, CNTLR_CLEAR clear_type);
extern t_stat  dl_load_unload        (CVPTR cvptr, UNIT *uptr, t_bool load);

/* Disc library global utility routines */

extern CNTLR_CLASS dl_classify     (CNTLR_VARS  cntlr);
extern const char  *dl_opcode_name (CNTLR_TYPE  controller, CNTLR_OPCODE opcode);
extern const char  *dl_phase_name  (CNTLR_PHASE phase);

/* Disc library global VM routines */

extern t_stat dl_attach    (CVPTR cvptr, UNIT  *uptr, CONST char *cptr);
extern t_stat dl_detach    (CVPTR cvptr, UNIT  *uptr);
extern t_stat dl_set_model (UNIT  *uptr, int32 value, CONST char *cptr, void *desc);
