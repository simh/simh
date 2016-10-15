/* hp3000_ms.c: HP 3000 30215A Magnetic Tape Controller Interface simulator

   Copyright (c) 2016, J. David Bryan

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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   MS           HP 30215A Magnetic Tape Controller Interface

   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   09-Jun-16    JDB     Added casts for ptrdiff_t to int32 values
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     Fixed interrupt mask setting
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Mar-16    JDB     Changed the buffer element type from uint8 to TL_BUFFER
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   10-Nov-15    JDB     First release version
   26-Oct-14    JDB     Passes the magnetic tape diagnostic (D433A)
   10-Feb-13    JDB     Created

   References:
     - 30115A Nine-Track (NRZI-PE) Magnetic Tape Subsystem Maintenance Manual
         (30115-90001, June 1976)
     - 30115A Nine-Track (NRZI-PE) Magnetic Tape Subsystem Microprogram Listing
         (30115-90005, January 1974)
     - Stand-Alone HP 30115A (7970B/E) Magnetic Tape (NRZI-PE) Diagnostic
         (30115-90014, May 1976)


   The HP 30115A Magnetic Tape Subsystem connects the 7970B/E 1/2-inch magnetic
   tape drives to the HP 3000.  The subsystem consists of a 30215A two-card tape
   controller processor and controller interface, and from one to four HP 7970B
   800-bpi NRZI or HP 7970E 1600-bpi PE drives.  The two drive types can be
   mixed on a single controller.  The subsystem uses the Multiplexer Channel to
   achieve a 36 KB/second (NRZI) or 72 KB/second (PE) transfer rate to the CPU.

   This module simulates the controller interface.  The controller processor
   simulation is provided by the HP magnetic tape controller simulator library
   (hp_tapelib).  Rather than simulating the signal interaction specific to
   these two cards, the HP tape library simulates an abstract controller having
   an electrical interface modelled on the HP 13037 disc controller.  The CPU
   interface and tape controller interact via 16-bit data, flag, and function
   buses.  Commands, status, and data are exchanged across the data bus, with
   the flag bus providing indications of the state of the interface and the
   function bus indicating what actions the interface must take in response to
   command processing by the controller.  By specifying the controller type as
   an HP 30215, the abstract controller adopts the personality of the HP 3000
   tape controller.

   While the interface and controller are idle, a drive unit that changes from
   Not Ready to Ready status will cause an interrupt.  This occurs when an
   offline drive is put online (e.g., after mounting a tape) and when a
   rewinding drive completes the action and is repositioned at the load point.

   An interrupt also occurs if an error terminates the current command.  The
   cause of the interrupt is encoded in the status word.  All error codes are
   cleared to the No Error state whenever a new SIO program is started.

   A new command may be rejected for one of several reasons:

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


   The tape interface responds to direct and programmed I/O instructions, as
   follows:

   Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = programmed master clear
     R = reset interrupts


   Control Word Format (SIO Control):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |  word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   - | unit  | 0   0   0   0 | command code  |  word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Unit:

     00 = select unit 0
     01 = select unit 1
     10 = select unit 2
     11 = select unit 3

   Command code:

     00 = Select Unit
     04 = Write Record
     05 = Write Gap
     06 = Read Record
     07 = Forward Space Record
     10 = Rewind
     11 = Rewind and Reset
     12 = Backspace Record
     13 = Backspace File
     14 = Write Record with Zero Parity
     15 = Write File Mark
     16 = Read Record with CRCC
     17 = Forward Space File

   Control word 1 is not used.

   The unit field is used only with the Select Unit command.  Bits 8-11 must be
   zero, or a Command Reject error will occur.  Command codes 01-03 are reserved
   and will cause a Command Reject error if specified.  Codes 14 and 16 are used
   for diagnostics only.


   Status Word Format (TIO and SIO Status):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | B | I | unit  | E | P | R | L | D | W | M | err code  | T |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK
     B = byte count is odd
     I = interrupt requested
     E = end of tape
     P = write protected
     R = drive ready
     L = load point
     D = density 800/1600 (0/1)
     W = write status (last operation was a write of any kind)
     M = tape mark
     T = 9-track drive/7-track drive (0/1)

   Unit:

     00 = reporting unit 0
     01 = reporting unit 1
     10 = reporting unit 2
     11 = reporting unit 3

   Error code:

     000 = unit interrupt
     001 = transfer error
     010 = command reject error
     011 = tape runaway error
     100 = timing error
     101 = tape error
     110 = (reserved)
     111 = no error

   A unit interrupt occurs when a drive goes online or when a rewind operation
   completes.  A transfer error occurs when the channel asserts XFERERROR to
   abort a transfer for a parity error or memory address out of bounds.  These
   two errors are generated by the interface and not by the HP tape library.

   A timing error occurs when a read overrun or write underrun occurs.  A tape
   error occurs when a tape parity, CRC error, or multi-track error occurs.
   Only these two errors may occur in the same transfer, with timing error
   having priority.  The other errors only occur independently.


   Output Data Word Format (SIO Write):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  data buffer register value                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Input Data Word Format (SIO Read):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  data buffer register value                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   The interface does not respond to WIO or RIO instructions.

   Tape read or write commands may transfer up to 4K words with a single SIO
   Read or Write order.  Chained orders are necessary if longer transfers are
   required.  However, if a chained read completes with a record shorter than
   the transfer length, a Command Reject will occur.


   Implementation notes:

    1. In hardware, each tape drive has four buttons numbered 0 to 3 that select
       the unit number to which the drive responds, plus an OFF button that
       inhibits drive selection (effectively disconnecting the drive from the
       controller).  Pressing a numbered button changes the unit number without
       altering the tape position or condition.

       In simulation, the tape unit number corresponds to the simulation unit
       number.  For example, simulation unit MS0 responds when the controller
       addresses tape unit 0.  The correspondence between tape and simulation
       unit numbers cannot be changed.  Therefore, changing a unit's number is
       accomplished by detaching the current tape image from the first
       simulation unit and attaching it to the second unit.  Note, however, that
       this resets the tape position to the load point, so it is not exactly
       equivalent.

    2. Per page 2-15 of the maintenance manual, during the idle state when no
       SIO program is active, the interface continuously selects one unit after
       another to look for a change from Not Ready to Ready status.  Therefore,
       the tape unit selected bits will be seen to change continuously.  In
       simulation, a change of status is noted when the change occurs, e.g.,
       when the SET <unit> ONLINE command is entered, so scanning is not
       necessary.  A program that continuously requests status will not see the
       unit select bits changing as in hardware.
*/



#include "hp3000_defs.h"
#include "hp3000_io.h"
#include "hp_tapelib.h"



/* Program constants */

#define DRIVE_COUNT         (TL_MAXDRIVE + 1)           /* the number of tape drive units */
#define UNIT_COUNT          (DRIVE_COUNT + TL_AUXUNITS) /* the total number of units */

#define cntlr_unit          ms_unit [TL_CNTLR_UNIT]     /* the controller unit alias */

#define UNUSED_COMMANDS     (STCFL | STDFL)             /* unused tape interface commands */


/* Debug flags (interface-specific) */

#define DEB_IOB             TL_DEB_IOB                  /* trace I/O bus signals and data words */
#define DEB_SERV            TL_DEB_SERV                 /* trace unit service scheduling calls */
#define DEB_CSRW            (1u << TL_DEB_V_UF + 0)     /* trace control, status, read, and write actions */


/* Control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | -   -   -   -   -   -   -   -   -   -   -   -   -   - |  DIO
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |  PIO word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   - | unit  | 0   0   0   0 | command code  |  PIO word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* (M) master reset */
#define CN_RIN              0040000u            /* (R) reset interrupt */
#define CN_UNIT_MASK        0001400u            /* unit number mask */
#define CN_RSVD_MASK        0000360u            /* reserved mask */
#define CN_CMD_MASK         0000017u            /* command code mask */

#define CN_CMD_RDR          0000006u            /* Read Record command */

#define CN_UNIT_SHIFT       8                   /* unit number alignment shift */
#define CN_CMD_SHIFT        0                   /* command code alignment shift */

#define CN_UNIT(c)          (((c) & CN_UNIT_MASK) >> CN_UNIT_SHIFT)
#define CN_CMD(c)           (((c) & CN_CMD_MASK)  >> CN_CMD_SHIFT)

static const BITSET_NAME control_names [] = {   /* Control word names */
    "master reset",                             /*   bit  0 */
    "reset interrupt"                           /*   bit  1 */
    };

static const BITSET_FORMAT control_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (control_names, 14, msb_first, no_alt, no_bar) };


/* Status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | B | I | unit  | E | P | R | L | D | W | M | err code  | T |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The status bits for the encoded error field are complemented from their
       actual values.  This allows the tape library to use an all-zeros value to
       represent No Error, which is consistent with the values used by other
       controllers.  The encoded error field bits must be complemented before
       reporting the controller status.
 */

#define ST_SIO_OK           0100000u            /* (S) SIO OK to use */
/*      ST_ODD_COUNT        0040000u */         /* (B) byte count is odd (supplied by hp_tapelib) */
#define ST_INTREQ           0020000u            /* (I) interrupt requested */
#define ST_UNIT_MASK        0014000u            /* unit selected mask */
/*      ST_EOT              0002000u */         /* (E) end of tape (supplied by hp_tapelib) */
/*      ST_PROTECTED        0001000u */         /* (P) write protected (supplied by hp_tapelib) */
/*      ST_READY            0000400u */         /* (R) unit ready (supplied by hp_tapelib) */
/*      ST_LOAD_POINT       0000200u */         /* (L) load point (supplied by hp_tapelib) */
/*      ST_DENSITY_1600     0000100u */         /* (D) 1600 bpi density (supplied by hp_tapelib) */
/*      ST_WRITE_STATUS     0000040u */         /* (W) write status (supplied by hp_tapelib) */
/*      ST_TAPE_MARK        0000020u */         /* (M) tape mark (supplied by hp_tapelib) */
#define ST_ERROR_MASK       0000016u            /* encoded error field mask */
/*      ST_7_TRACK          0000001u */         /* (T) 7-track unit (always off) */

#define ST_UNIT_SHIFT       11                  /* unit number alignment shift */
#define ST_ERROR_SHIFT      1                   /* encoded error alignment shift */

#define ST_UNIT(n)          ((n) << ST_UNIT_SHIFT & ST_UNIT_MASK)

#define ST_TO_UNIT(s)       (((s) & ST_UNIT_MASK)  >> ST_UNIT_SHIFT)
#define ST_TO_ERROR(s)      (((s) & ST_ERROR_MASK) >> ST_ERROR_SHIFT)


/* Error codes (complements of the values returned) */

#define ST_UNITIRQ          0000016u            /* unit interrupt */
#define ST_XFER             0000014u            /* transfer error */
/*      ST_REJECT           0000012u */         /* command reject (supplied by hp_tapelib) */
/*      ST_RUNAWAY          0000010u */         /* tape runaway (supplied by hp_tapelib) */
/*      ST_TIMING           0000006u */         /* timing error (supplied by hp_tapelib) */
/*      ST_PARITY           0000004u */         /* tape error (supplied by hp_tapelib) */
/*      ST_RESERVED         0000002u */         /* (reserved) */
/*      ST_NOERROR          0000000u */         /* no error */

static const BITSET_NAME status_names [] = {    /* Status word names */
    "SIO OK",                                   /*   bit  0 */
    "odd count",                                /*   bit  1 */
    "interrupt",                                /*   bit  2 */
    NULL,                                       /*   bit  3 */
    NULL,                                       /*   bit  4 */
    "end of tape",                              /*   bit  5 */
    "protected",                                /*   bit  6 */
    "ready",                                    /*   bit  7 */
    "load point",                               /*   bit  8 */
    "1600 bpi",                                 /*   bit  9 */
    "writing",                                  /*   bit 10 */
    "tape mark",                                /*   bit 11 */
    NULL,                                       /*   bit 12 */
    NULL,                                       /*   bit 13 */
    NULL,                                       /*   bit 14 */
    "7 track"                                   /*   bit 15 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 0, msb_first, no_alt, append_bar) };


static const char *const error_names [] = {     /* error status code names */
    "unit interrupt",                           /*   code 0 */
    "transfer error",                           /*   code 1 */
    "command reject",                           /*   code 2 */
    "tape runaway",                             /*   code 3 */
    "timing error",                             /*   code 4 */
    "tape error",                               /*   code 5 */
    "reserved",                                 /*   code 6 */
    "no error"                                  /*   code 7 */
    };


/* Interface command code to controller opcode translation table */

static const CNTLR_OPCODE to_opcode [] = {      /* opcode translation table (fully decoded) */
    Select_Unit_0,                              /*   000 SEL = Select Unit */
    Invalid_Opcode,                             /*   001 --- = invalid */
    Invalid_Opcode,                             /*   002 --- = invalid */
    Invalid_Opcode,                             /*   003 --- = invalid */
    Write_Record,                               /*   004 WRR = Write Record */
    Write_Gap,                                  /*   005 GAP = Write Gap */
    Read_Record,                                /*   006 RDR = Read Record */
    Forward_Space_Record,                       /*   007 FSR = Forward Space Record */
    Rewind,                                     /*   010 REW = Rewind */
    Rewind_Offline,                             /*   011 RST = Rewind and Reset */
    Backspace_Record,                           /*   012 BSR = Backspace Record */
    Backspace_File,                             /*   013 BSF = Backspace File */
    Write_Record_without_Parity,                /*   014 WRZ = Write Record with Zero Parity */
    Write_File_Mark,                            /*   015 WFM = Write File Mark */
    Read_Record_with_CRCC,                      /*   016 RDC = Read Record with CRCC */
    Forward_Space_File                          /*   017 FSF = Forward Space File */
    };


/* Tape controller library data structures */

#define MS_REW_START        uS (10)             /* fast rewind start time */
#define MS_REW_RATE         uS (1)              /* fast rewind time per inch of travel */
#define MS_REW_STOP         uS (10)             /* fast rewind stop time */
#define MS_START            uS (10)             /* fast BOT/interrecord start delay time */
#define MS_DATA             uS (1)              /* fast per-byte data transfer time */
#define MS_OVERHEAD         uS (10)             /* fast controller overhead time */

static DELAY_PROPS fast_times =                 /* FASTTIME delays */
    { DELAY_INIT (MS_REW_START, MS_REW_RATE,
                  MS_REW_STOP,  MS_START,
                  MS_START,     MS_DATA,
                  MS_OVERHEAD)  };


/* Interface state */

static FLIP_FLOP sio_busy       = CLEAR;                /* SIO busy flip-flop */
static FLIP_FLOP channel_sr     = CLEAR;                /* channel service request flip-flop */
static FLIP_FLOP device_sr      = CLEAR;                /* device service request flip-flop */
static FLIP_FLOP input_xfer     = CLEAR;                /* input transfer flip-flop */
static FLIP_FLOP output_xfer    = CLEAR;                /* output transfer flip-flop */
static FLIP_FLOP interrupt_mask = SET;                  /* interrupt mask flip-flop */
static FLIP_FLOP unit_interrupt = CLEAR;                /* unit ready flip-flop */
static FLIP_FLOP device_end     = CLEAR;                /* device end flip-flop */
static FLIP_FLOP xfer_error     = CLEAR;                /* transfer error flip-flop */

static HP_WORD        buffer_word    = 0;               /* data buffer word */
static HP_WORD        attention_unit = 0;               /* number of the unit requesting attention */
static CNTLR_CLASS    command_class  = Class_Invalid;   /* current command classification */
static CNTLR_FLAG_SET flags          = INTOK;           /* tape controller interface flag set */

static TL_BUFFER buffer [TL_BUFSIZE];                   /* the tape record buffer */

DEVICE ms_dev;                                          /* incomplete device structure */

static CNTLR_VARS ms_cntlr =                            /* the tape controller */
    { CNTLR_INIT (HP_30215, ms_dev, buffer, fast_times) };


/* Interface local SCP support routines */

static CNTLR_INTRF ms_interface;
static t_stat      ms_service   (UNIT   *uptr);
static t_stat      ms_reset     (DEVICE *dptr);
static t_stat      ms_boot      (int32  unit_number, DEVICE *dptr);
static t_stat      ms_attach    (UNIT   *uptr,       CONST char *cptr);
static t_stat      ms_onoffline (UNIT   *uptr,       int32      value, CONST char *cptr, void *desc);


/* Interface local utility routines */

static void   master_reset          (void);
static void   clear_interface_logic (void);
static t_stat call_controller       (UNIT *uptr);


/* Interface SCP data structures */


/* Device information block */

static DIB ms_dib = {
    &ms_interface,                              /* device interface */
    6,                                          /* device number */
    3,                                          /* service request number */
    14,                                         /* interrupt priority */
    INTMASK_E                                   /* interrupt mask */
    };

/* Unit list */

#define UNIT_FLAGS          (UNIT_ATTABLE | UNIT_ROABLE | UNIT_DISABLE | UNIT_OFFLINE)

static UNIT ms_unit [UNIT_COUNT] = {
    { UDATA (&ms_service, UNIT_FLAGS | UNIT_7970E, 0) },    /* drive unit 0 */
    { UDATA (&ms_service, UNIT_FLAGS | UNIT_7970E, 0) },    /* drive unit 1 */
    { UDATA (&ms_service, UNIT_FLAGS | UNIT_7970E, 0) },    /* drive unit 2 */
    { UDATA (&ms_service, UNIT_FLAGS | UNIT_7970E, 0) },    /* drive unit 3 */
    { UDATA (&ms_service, UNIT_DIS,                0) }     /* controller unit */
    };

/* Register list */

static REG ms_reg [] = {
/*    Macro   Name    Location        Width  Offset            Flags            */
/*    ------  ------  --------------  -----  ------  -------------------------  */
    { FLDATA (SIOBSY, sio_busy,                0)                               },
    { FLDATA (CHANSR, channel_sr,              0)                               },
    { FLDATA (DEVSR,  device_sr,               0)                               },
    { FLDATA (INXFR,  input_xfer,              0)                               },
    { FLDATA (OUTXFR, output_xfer,             0)                               },
    { FLDATA (INTMSK, interrupt_mask,          0)                               },
    { FLDATA (UINTRP, unit_interrupt,          0)                               },
    { FLDATA (DEVEND, device_end,              0)                               },
    { FLDATA (XFRERR, xfer_error,              0)                               },
    { ORDATA (BUFWRD, buffer_word,     16),          REG_A | REG_FIT | PV_RZRO  },
    { DRDATA (ATUNIT, attention_unit,  16),                  REG_FIT | PV_LEFT  },
    { DRDATA (CLASS,  command_class,    4),                            PV_LEFT  },
    { YRDATA (FLAGS,  flags,            8,                             PV_RZRO) },

      DIB_REGS (ms_dib),

      TL_REGS (ms_cntlr, ms_unit, DRIVE_COUNT, buffer, fast_times),

    { NULL }
    };

/* Modifier list */

static MTAB ms_mod [] = {

    TL_MODS (ms_cntlr, TL_7970B | TL_7970E, TL_FIXED, ms_onoffline),

/*    Entry Flags  Value        Print String  Match String  Validation   Display       Descriptor       */
/*    -----------  -----------  ------------  ------------  -----------  ------------  ---------------- */
    { MTAB_XDV,    VAL_DEVNO,   "DEVNO",      "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &ms_dib },
    { MTAB_XDV,    VAL_INTMASK, "INTMASK",    "INTMASK",    &hp_set_dib, &hp_show_dib, (void *) &ms_dib },
    { MTAB_XDV,    VAL_INTPRI,  "INTPRI",     "INTPRI",     &hp_set_dib, &hp_show_dib, (void *) &ms_dib },
    { MTAB_XDV,    VAL_SRNO,    "SRNO",       "SRNO",       &hp_set_dib, &hp_show_dib, (void *) &ms_dib },
    { 0 }
    };

/* Debugging trace list */

static DEBTAB ms_deb [] = {
    { "CMD",   TL_DEB_CMD   },                  /* controller commands */
    { "INCO",  TL_DEB_INCO  },                  /* controller command initiations and completions */
    { "CSRW",  DEB_CSRW     },                  /* interface control, status, read, and write actions */
    { "STATE", TL_DEB_STATE },                  /* controller execution state changes */
    { "SERV",  DEB_SERV     },                  /* controller unit service scheduling calls */
    { "XFER",  TL_DEB_XFER  },                  /* controller data reads and writes */
    { "IOBUS", DEB_IOB      },                  /* interface and controller I/O bus signals and data words */
    { NULL,    0            }
    };

/* Device descriptor */

DEVICE ms_dev = {
    "MS",                                       /* device name */
    ms_unit,                                    /* unit array */
    ms_reg,                                     /* register array */
    ms_mod,                                     /* modifier array */
    UNIT_COUNT,                                 /* number of units */
    10,                                         /* address radix */
    32,                                         /* address width = 4 GB */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ms_reset,                                  /* reset routine */
    &ms_boot,                                   /* boot routine */
    &ms_attach,                                 /* attach routine */
    &tl_detach,                                 /* detach routine */
    &ms_dib,                                    /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    ms_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Interface local SCP support routines */



/* Magnetic tape interface.

   The interface is installed on the IOP and Multiplexer Channel buses and
   receives direct and programmed I/O commands from the IOP and Multiplexer
   Channel, respectively.  In simulation, the asserted signals on the buses are
   represented as bits in the inbound_signals set.  Each signal is processed
   sequentially in numerical order, and a set of similar outbound_signals is
   assembled and returned to the caller, simulating assertion of the
   corresponding backplane signals.

   The DCONTSTB signal qualifies direct I/O control word bits 0 and 1 (master
   reset and reset interrupt, respectively) only.  The PCONTSTB signal does not
   enable these functions.  A master reset is identical to an IORESET signal
   assertion; the current command is aborted, all drives are stopped (unless
   rewinding), and the interface is cleared.  The reset interrupt function
   clears the Interrupt Request flip-flop; it does not affect the Interrupt
   Active flip-flop.

   Controller commands are executed by the PCONTSTB signal.  Command opcodes are
   carried in the IOAW of the control order.  The IOCW is not used.  Commands
   that transfer data must be followed by the appropriate read or write I/O
   order.  The controller sets up the associated command during PCONTSTB
   processing but does not actually initiate tape movement (i.e., does not begin
   start phase processing) until the corresponding TOGGLEINXFER or TOGGLEOUTXFER
   signal is asserted.

   The DSTATSTB and PSTATSTB signals are tied together in hardware and therefore
   perform identically.  Both return the status of the currently selected tape
   drive unit.

   The DREADSTB and DWRITESTB signals are acknowledged but perform no other
   function.  DREADSTB returns all-zeros data.

   A channel transfer error asserts XFERERROR, which sets the xfer_error
   flip-flop.  This causes the interface to assert a Transfer Error interrupt
   until the flip-flop is cleared by a Programmed Master Clear.  The controller
   sees no error indication; it simply hangs while waiting for the next data
   transfer, which does not occur because the channel transfer was aborted.
   This condition persists until a PMC occurs, which performs a hardware restart
   on the controller.


   Implementation notes:

    1. A unit interrupt ORs in the unit interrupt status code, rather than
       masking out any previous code.  This works because the code is all ones,
       which overrides any prior code.

       Similarly, a transfer error ORs in its status code, which is all ones
       except for the LSB.  This would fail if a code already present had the
       LSB set.  The only codes which do are ST_REJECT and ST_TIMING, and
       neither of these can be present when a transfer error occurs (a transfer
       error can only occur in the data phase due to a bad memory bank number; a
       timing error is set in the stop phase, after the transfer error has
       aborted the command, and a reject error is set in the wait phase, before
       the transfer is begun).

    2. Command errors and units becoming ready cause interrupts.  Once an
       interrupt is asserted, the controller sits in a tight loop waiting for
       the interrupt to be reset.  When it is, the controller returns to the
       idle loop and looks for the next command.

       In simulation, when a command is issued with an interrupt in process, the
       command is set up, the command ready flag is set, but the controller is
       not notified.  When DRESETINT is received, the controller will be
       called to start the command, which provides the same semantics.

    3. The maintenance manual states that DREADSTB and DWRITESTB are not used.
       But the schematic shows that DREADSTB is decoded and will enable the DATA
       IN lines when asserted.  However, none of the output drivers on that
       ground-true bus will be enabled.  There are pullups on all bits except
       6-13, which would be driven (if enabled) by the device number buffer.  So
       it appears that executing an RIO instruction will return zeros for bits
       0-5 and 14-15, with bits 6-13 indeterminate.

    4. The controller opcodes Select_Unit_0 through Select_Unit_3 are
       contiguous, so the interface may derive these opcodes for the SEL command
       by adding the unit number to the Select_Unit_0 value.

    5. In hardware, the controller microcode checks the input and output
       transfer flip-flops while waiting for a new command.  If either are set,
       a command reject is indicated.  This occurs if a Read or Write order
       precedes a Control order.  It also occurs if chained Read order is
       terminated with a Device End condition due to a record length shorter
       than the transfer length.

       In simulation, these conditions are tested separately.  A premature Read
       or Write order will be caught during TOGGLEINXFER or TOGGLEOUTXFER
       processing, and a chained Read order after a Device End will be caught
       during READNEXTWD processing when the device end flip-flop set.  In both
       cases, the controller is called to continue a command, but no command is
       in process, so a reject occurs.  Note that these conditions will no
       longer exist when a Control order is received, so tests there are not
       required.

    6. In hardware, the EOT, READNEXTWD, and SETJMP signals are ignored, and the
       JMPMET signal is asserted continuously when enabled by CHANSO.
*/

static SIGNALS_DATA ms_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
CNTLR_OPCODE   opcode;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (ms_dev, DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

while (working_set) {
    signal = IONEXTSIG (working_set);                   /* isolate the next signal */

    switch (signal) {                                   /* dispatch an I/O signal */

        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it now active */

                outbound_signals |= INTACK;             /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }

            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case SETINT:
        case DSETINT:
            dibptr->interrupt_request = SET;            /* request an interrupt */
            flags &= ~INTOK;                            /*   and clear the interrupt OK flag */

            if (interrupt_mask == SET)                  /* if the interrupt mask is satisfied */
                outbound_signals |= INTREQ;             /*   then assert the INTREQ signal */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* reset the interrupt active */
            unit_interrupt           = CLEAR;           /*   and unit interrupt flip-flops */

            if (dibptr->interrupt_request == CLEAR) {   /* if there's no request pending */
                if (sio_busy == CLEAR)                  /*   then if an SIO program is not executing */
                    flags |= INTOK;                     /*     then set the interrupt OK flag */

                if (flags & (CMRDY | INTOK))            /* if a command is present or a poll is needed */
                    call_controller (NULL);             /*   then tell the controller */

                if (device_sr)                          /* if the interface has requested service */
                    outbound_signals |= SRn;            /*   then assert SRn to the channel */
                }
            break;


        case DSETMASK:
            if (dibptr->interrupt_mask == INTMASK_E)            /* if the mask is always enabled */
                interrupt_mask = SET;                           /*   then set the mask flip-flop */
            else                                                /* otherwise */
                interrupt_mask = D_FF (dibptr->interrupt_mask   /*   set the mask flip-flop if the mask bit */
                                       & inbound_value);        /*     is present in the mask value */

            if (interrupt_mask && dibptr->interrupt_request)    /* if the mask is enabled and a request is pending */
                outbound_signals |= INTREQ;                     /*   then assert the INTREQ signal */
            break;


        case DCONTSTB:
            dprintf (ms_dev, DEB_CSRW, "Control is %s\n",
                     fmt_bitset (inbound_value, control_format));

            if (inbound_value & CN_MR)                      /* if the master reset bit is set */
                master_reset ();                            /*   then reset the interface */

            if (inbound_value & CN_RIN) {                   /* if the reset interrupt bit is set */
                dibptr->interrupt_request = CLEAR;          /*   then clear the interrupt request */

                if (dibptr->interrupt_active == CLEAR) {    /* if an interrupt is not active */
                    unit_interrupt = CLEAR;                 /*   then clear the unit interrupt flip-flop too */

                    if (sio_busy == CLEAR)                  /* if an SIO program is not executing */
                        flags |= INTOK;                     /*   then set the interrupt OK flag */
                    }
                }
            break;


        case PSTATSTB:
        case DSTATSTB:
            outbound_value = tl_status (&ms_cntlr);     /* get the controller and unit status */

            if (unit_interrupt)                         /* if a unit interrupt is pending */
                outbound_value =                        /*   then replace the selected unit */
                   outbound_value & ~ST_UNIT_MASK       /*     with the interrupting unit */
                   | ST_UNIT (attention_unit)           /*       and set the status code */
                   | ST_UNITIRQ;

            else if (xfer_error)                        /* otherwise if a transfer error occurred */
                outbound_value |= ST_XFER;              /*   then set the status bit */

            outbound_value ^= ST_ERROR_MASK;            /* complement the encoded error bits */

            if (sio_busy == CLEAR)                      /* if the interface is inactive */
                outbound_value |= ST_SIO_OK;            /*   then add the SIO OK status bit */

            if (dibptr->interrupt_request)              /* if an interrupt request is pending */
                outbound_value |= ST_INTREQ;            /*   then set the status bit */

            dprintf (ms_dev, DEB_CSRW, "Status is %s%s | unit %u\n",
                     fmt_bitset (outbound_value, status_format),
                     error_names [ST_TO_ERROR (outbound_value)],
                     ST_TO_UNIT (outbound_value));
            break;


        case DSTARTIO:
            dprintf (ms_dev, DEB_CSRW, "Channel program started\n");

            sio_busy = SET;                             /* set the SIO busy flip-flop */
            flags &= ~INTOK;                            /*   and clear the interrupt OK flag */

            mpx_assert_REQ (dibptr);                    /* request the channel */

            channel_sr = SET;                           /* set the service request flip-flop */
            outbound_signals |= SRn;                    /*   and assert a service request */
            break;


        case ACKSR:
            device_sr = CLEAR;                          /* acknowledge the service request */
            break;


        case TOGGLESR:
            TOGGLE (channel_sr);                        /* set or clear the channel service request flip-flop */
            break;


        case TOGGLESIOOK:
            TOGGLE (sio_busy);                              /* set or clear the SIO busy flip-flop */

            if (sio_busy == CLEAR) {                        /* if the flip-flop was cleared */
                dprintf (ms_dev, DEB_CSRW, "Channel program ended\n");

                if (dibptr->interrupt_request == CLEAR      /*   then if there's no interrupt request */
                  && dibptr->interrupt_active == CLEAR) {   /*     active or pending */
                    flags |= INTOK;                         /*       then set the interrupt OK flag */

                    call_controller (NULL);                 /* check for drive attention held off by INTOK denied */
                    }
                }
            break;


        case TOGGLEINXFER:
            TOGGLE (input_xfer);                        /* set or clear the input transfer flip-flop */

            if (input_xfer == SET) {                    /* if the transfer is starting */
                if (command_class == Class_Read)        /*   then if a read command is pending */
                    flags &= ~EOD;                      /*     then clear the EOD flag to enable the data transfer */

                call_controller (&cntlr_unit);          /* let the controller know the channel has started */
                }

            else {                                      /* otherwise the transfer is ending */
                flags |= EOD;                           /*   so set the end-of-data flag */
                device_end = CLEAR;                     /*     and clear any device end condition */
                }
            break;


        case TOGGLEOUTXFER:
            TOGGLE (output_xfer);                       /* set or clear the output transfer flip-flop */

            if (output_xfer == SET) {                   /* if the transfer is starting */
                if (command_class == Class_Write)       /*   then if a write command is pending */
                    flags &= ~EOD;                      /*     then clear the EOD flag to enable the data transfer */

                call_controller (&cntlr_unit);          /* let the controller know the channel has started */
                }

            else                                        /* otherwise the transfer is ending */
                flags |= EOD;                           /*   so set the end-of-data flag */
            break;


        case PCMD1:
            device_sr = SET;                            /* request the second control word */
            break;


        case PCONTSTB:
            opcode = to_opcode [CN_CMD (inbound_value)];    /* get the command code from the control word */

            if (opcode == Select_Unit_0)                    /* if this is a select unit command */
                opcode = opcode + CN_UNIT (inbound_value);  /*   then convert to a unit-specific opcode */

            dprintf (ms_dev, DEB_CSRW, "Control is %06o (%s)\n",
                     inbound_value, tl_opcode_name (opcode));

            if ((inbound_value & CN_RSVD_MASK) != 0)    /* if the reserved bits aren't zero */
                buffer_word = (HP_WORD) Invalid_Opcode; /*   then reject the command */
            else                                        /* otherwise */
                buffer_word = (HP_WORD) opcode;         /*   store the opcode in the data buffer register */

            flags |= CMRDY | CMXEQ;                     /* set the command ready and execute flags */

            if (dibptr->interrupt_request == CLEAR      /* if no interrupt is pending */
              && dibptr->interrupt_active == CLEAR) {   /*   or active */
                call_controller (NULL);                 /*     then tell the controller to start the command */

                unit_interrupt = CLEAR;                 /* clear the unit interrupt flip-flop */
                }
            break;


        case READNEXTWD:
            if (device_end == SET                       /* if the device end flip-flop is set */
              && (inbound_signals & TOGGLESR)) {        /*   and we're starting (not continuing) a transfer */
                call_controller (&cntlr_unit);          /*     then let the controller know to reject */

                device_end = CLEAR;                     /* clear the device end condition */
                }
            break;


        case PREADSTB:
            if (device_end) {                               /* if the transfer has been aborted */
                outbound_value = dibptr->device_number * 4; /*   then return the DRT address */
                outbound_signals |= DEVEND;                 /*     and indicate a device abort */
                }

            else {                                          /* otherwise the transfer continues */
                outbound_value = buffer_word;               /*   so return the data buffer register value */
                flags &= ~DTRDY;                            /*     and clear the data ready flag */
                }
            break;


        case PWRITESTB:
            buffer_word = inbound_value;                /* save the word to write */
            flags |= DTRDY;                             /*   and set the data ready flag */
            break;


        case DEVNODB:
            outbound_value = dibptr->device_number * 4; /* return the DRT address */
            break;


        case XFERERROR:
            dprintf (ms_dev, DEB_CSRW, "Channel program aborted\n");

            xfer_error = SET;                           /* set the transfer error flip-flop */
            flags |= XFRNG;                             /*   and controller flag */

            call_controller (NULL);                     /* let the controller know of the abort */

            clear_interface_logic ();                   /* clear the interface to abort the transfer */

            dibptr->interrupt_request = SET;            /* request an interrupt */
            flags &= ~INTOK;                            /*   and clear the interrupt OK flag */

            if (interrupt_mask == SET)                  /* if the interrupt mask is satisfied */
                outbound_signals |= INTREQ;             /*   then assert the INTREQ signal */
            break;


        case CHANSO:
            if (channel_sr | device_sr)                 /* if the interface has requested service */
                outbound_signals |= SRn;                /*   then assert SRn to the channel */

            outbound_signals |= JMPMET;                 /* JMPMET is tied active on this interface */
            break;


        case DREADSTB:                                  /* not used by this interface */
        case DWRITESTB:                                 /* not used by this interface */
        case EOT:                                       /* not used by this interface */
        case SETJMP:                                    /* not used by this interface */
        case PFWARN:                                    /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }

dprintf (ms_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Service the controller or a drive unit.

   The service routine is called to execute scheduled controller command phases
   for the specified unit.  The actions to be taken depend on the current state
   of the controller and the drive unit.

   This routine is entered when a tape unit or the controller unit is ready to
   execute the next command phase.  Generally, the controller library handles
   all of the tape operations.  All that is necessary is to notify the
   controller, which will process the next phase of command execution.  Because
   the controller can overlap operations, in particular scheduling rewinds on
   several drive units simultaneously, each drive unit carries its own current
   operation code and execution phase.  The controller uses these to determine
   what to do next.
*/

static t_stat ms_service (UNIT *uptr)
{
t_stat result;

dprintf (ms_dev, DEB_SERV, "%s service entered\n",
         tl_unit_name ((int32) (uptr - ms_unit)));

result = call_controller (uptr);                        /* call the controller */

if (device_sr == SET)                                   /* if the device has requested service */
    mpx_assert_SRn (&ms_dib);                           /*   then assert SR to the channel */

return result;
}


/* Device reset routine.

   This routine is called for a RESET, RESET MS, or BOOT MS command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to the Programmed Master Clear.  In
   addition, if a power-on reset (RESET -P) is done, the original FASTTIME
   settings are restored.
*/

static t_stat ms_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    fast_times.rewind_start = MS_REW_START;             /*   then reset the rewind initiation time, */
    fast_times.rewind_rate  = MS_REW_RATE;              /*     the rewind time per inch, */
    fast_times.bot_start    = MS_START;                 /*       the beginning-of-tape gap traverse time, */
    fast_times.ir_start     = MS_START;                 /*         the interrecord traverse time, */
    fast_times.data_xfer    = MS_DATA;                  /*           the per-byte data transfer time, */
    fast_times.overhead     = MS_OVERHEAD;              /*             and the controller execution overhead */
    }

master_reset ();                                        /* perform a master reset */

return tl_reset (&ms_cntlr);                            /* reset the controller and return the result */
}


/* Device boot routine.

   This routine is called for the BOOT MS command to initiate the system cold
   load procedure for the tape.  It is the simulation equivalent to presetting
   the System Switch Register to the appropriate control and device number bytes
   and then pressing the ENABLE and LOAD front panel switches.

   For this interface, the switch register is set to %0030nn, where "nn"
   is the current tape interface device number, which defaults to 6.  The
   control byte is 06 (Read Record).

   The cold load procedure always uses unit 0.
*/

static t_stat ms_boot (int32 unit_number, DEVICE *dptr)
{
if (unit_number != 0)                                   /* if a unit other than 0 is specified */
    return SCPE_ARG;                                    /*   then fail with an invalid argument error */

else {                                                  /* otherwise */
    cpu_front_panel (TO_WORD (CN_CMD_RDR,               /*   set up the Read Record command */
                              ms_dib.device_number),    /*     from tape unit 0 */
                     Cold_Load);

    return SCPE_OK;                                     /* return to run the bootstrap */
    }
}


/* Attach a tape image file to a drive unit.

   The specified file is attached to the indicated drive unit.  This is the
   simulation equivalent of mounting a tape reel on the drive and pressing the
   LOAD and ONLINE buttons.  The transition from offline to online causes a Unit
   Attention interrupt.

   The controller library routine handles command validation and setting the
   appropriate drive unit status.  It will return an error code if the command
   fails.  Otherwise, it will return SCPE_INCOMP if the command must be
   completed with a controller call or SCPE_OK if the command is complete.  If
   the controller is idle, a call will be needed to poll the drives for
   attention; otherwise, the drives will be polled the next time the controller
   becomes idle.


   Implementation notes:

    1. If we are called during a RESTORE command to reattach a file previously
       attached when the simulation was SAVEd, the unit status will not be
       changed by the controller, so the unit will not request attention.
*/

static t_stat ms_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = tl_attach (&ms_cntlr, uptr, cptr);             /* attach the drive */

if (result == SCPE_INCOMP) {                            /* if the controller must be called before returning */
    call_controller (NULL);                             /*   then let it know to poll the drives */
    return SCPE_OK;                                     /*     before returning with success */
    }

else                                                    /* otherwise */
    return result;                                      /*   return the status of the attach */
}


/* Set the drive online or offline.

   The SET MSn OFFLINE command simulates pressing the RESET button, and the SET
   MSn ONLINE command simulates pressing the ONLINE button.  The transition from
   offline to online causes a Unit Attention interrupt.  The SET request fails
   if there is no tape mounted on the drive, i.e., if the unit is not attached
   to a tape image file.

   The controller library routine handles command validation and setting the
   appropriate drive unit status.  It will return an error code if the command
   fails.  Otherwise, it will return SCPE_INCOMP if the command must be
   completed with a controller call or SCPE_OK if the command is complete.  If
   the controller is idle, a call will be needed to poll the drives for
   attention; otherwise, the drives will be polled the next time the controller
   becomes idle.
*/

static t_stat ms_onoffline (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const t_bool online = (value != UNIT_OFFLINE);          /* TRUE if the drive is being put online */
t_stat result;

result = tl_onoffline (&ms_cntlr, uptr, online);        /* set the drive online or offline */

if (result == SCPE_INCOMP) {                            /* if the controller must be called before returning */
    call_controller (NULL);                             /*   then let it know to poll the drives */
    return SCPE_OK;                                     /*     before returning with success */
    }

else                                                    /* otherwise */
    return result;                                      /*   return the status of the load or unload */
}



/* Interface local utility routines */


/* Master reset.

   A master reset is generated either by an I/O Reset signal or a Programmed
   Master Clear (CIO bit 0).  It initializes the interface and the tape
   controller to their respective idle states.  Clearing the controller aborts
   all commands in progress and stops all drive motion except for rewinding,
   which completes normally.
*/

static void master_reset (void)
{
tl_clear (&ms_cntlr);                                   /* clear the controller to stop the drives */

ms_dib.interrupt_request = CLEAR;                       /* clear any current */
ms_dib.interrupt_active  = CLEAR;                       /*   interrupt request */

interrupt_mask = SET;                                   /* set the interrupt mask */
flags = INTOK;                                          /*   and the Interrupt OK flag */

xfer_error = CLEAR;                                     /* clear the transfer error flip-flop */

clear_interface_logic ();                               /* clear the interface to abort the transfer */

return;
}


/* Clear interface logic.

   The clear interface logic signal is asserted during channel operation when
   the controller is reset or requests an interrupt, the channel indicates a
   transfer failure by asserting XFERERROR, or a master reset occurs.  It clears
   the SIO Busy, Channel and Device Service Request, Input Transfer, Output
   Transfer, and Device End flip-flops.
*/

static void clear_interface_logic (void)
{
sio_busy       = CLEAR;                                 /* clear the SIO busy flip-flop */
channel_sr     = CLEAR;                                 /*   and the channel service request flip-flop */
device_sr      = CLEAR;                                 /*   and the device service request flip-flop */
input_xfer     = CLEAR;                                 /*   and the input transfer flip-flop */
output_xfer    = CLEAR;                                 /*   and the output transfer flip-flop */
device_end     = CLEAR;                                 /*   and the device end flip-flop */

return;
}


/* Call the tape controller.

   The abstract tape controller connects to the CPU interface via 16-bit data,
   flag, and function buses.  The controller monitors the flag bus and reacts to
   the interface changing the flag states by placing or accepting data on the
   data bus and issuing commands to the interface via the function bus.

   In simulation, a call to the tl_controller routine informs the controller of
   a (potential) change in flag state.  The current set of flags and data bus
   value are supplied, and the controller returns a combined set of functions
   and a data bus value.

   The controller must be called any time there is a change in the state of the
   interface or the drive units.  Generally, the cases that require notification
   are when the interface:

     - has a new command to execute
     - has detected the channel starting, ending, or aborting the transfer
     - has a new data word available to send
     - has obtained the last data word received
     - has received a unit service event notification
     - has detected the mounting of the tape reel on a drive
     - has detected a drive being placed online or offline
     - has detected the interrupt request being reset

   The set of returned functions is processed sequentially, updating the
   interface state as indicated.  Some functions are not used by this interface,
   so they are masked off before processing to improve performance.

   Because the tape is a synchronous device, overrun or underrun can occur if
   the interface is not ready when the controller must transfer data.  There are
   four conditions that lead to an overrun or underrun:

    1. The controller is ready with a tape read word (IFIN), but the interface
       buffer is full (DTRDY).

    2. The controller needs a tape write word (IFOUT), but the interface buffer
       is empty (~DTRDY).

    3. The CPU attempts to read a word, but the interface buffer is empty
       (~DTRDY).

    4. The CPU attempts to write a word, but the interface buffer is full
       (DTRDY).

   The interface detects the first two conditions and sets the data overrun flag
   if either occurs.  The hardware design of the interface prevents the last two
   conditions, as the interface will assert SRn only when the buffer is full
   (read) or empty (write).


   Implementation notes:

    1. In hardware, data overrun and underrun are detected as each byte is moved
       between the tape unit and the data buffer register.  In simulation, OVRUN
       will not be asserted when the controller is called with the full or empty
       buffer; instead, it will be asserted for the next controller call.
       Because the controller will be called for the tape stop phase, and
       because OVRUN isn't checked until that point, this "late" assertion does
       not affect overrun or underrun detection.
*/

static t_stat call_controller (UNIT *uptr)
{
CNTLR_IFN_IBUS result;
CNTLR_IFN_SET  command_set;
CNTLR_IFN      command;
t_stat         status = SCPE_OK;

result =                                                /* call the controller to start or continue a command */
   tl_controller (&ms_cntlr, uptr, flags, (CNTLR_IBUS) buffer_word);

command_set = TLIFN (result) & ~UNUSED_COMMANDS;        /* strip the commands we don't use as an efficiency */

while (command_set) {                                   /* process the set of returned interface commands */
    command = TLNEXTIFN (command_set);                  /* isolate the next command */

    switch (command) {                                  /* dispatch an interface command */

        case IFIN:                                      /* Interface In */
            if (flags & DTRDY)                          /* if the buffer is still full */
                flags |= OVRUN;                         /*   then this input overruns it */

            buffer_word = TLIBUS (result);              /* store the data word in the buffer */
            flags |= DTRDY;                             /*   and set the data ready flag */
            break;


        case IFOUT:                                     /* Interface Out */
            if ((flags & DTRDY) == NO_FLAGS)            /* if the buffer is empty */
                flags |= OVRUN;                         /*   then this output underruns it */

            flags &= ~DTRDY;                            /* clear the data ready flag */
            break;


        case IFGTC:                                         /* Interface Get Command */
            flags = flags & INTOK | EOD;                    /* clear the interface transfer flags and set EOD */

            command_class = (CNTLR_CLASS) TLIBUS (result);  /* save the command classification */
            break;


        case RQSRV:                                     /* Request Service */
            device_sr = SET;                            /* set the device service request flip-flop */
            break;


        case DVEND:                                     /* Device End */
            device_end = SET;                           /* set the device end flip-flop */
            break;


        case DATTN:                                     /* Drive Attention */
            unit_interrupt = SET;                       /* set the unit interrupt flip-flop */
            attention_unit = TLIBUS (result);           /*   and save the number of the requesting unit */

        /* fall into the STINT case */

        case STINT:                                     /* Set Interrupt */
            flags = NO_FLAGS;                           /* clear the interface transfer flags and INTOK */

            clear_interface_logic ();                   /* clear the interface to abort the transfer */

            ms_dib.interrupt_request = SET;             /* set the interrupt request flip-flop */

            if (interrupt_mask == SET)                  /* if the interrupt mask is satisfied */
                iop_assert_INTREQ (&ms_dib);            /*   then assert the INTREQ signal */
            break;


        case SCPE:                                      /* SCP Error Status */
            status = TLIBUS (result);                   /* get the status code */
            break;

        case STDFL:                                     /* not decoded by this interface */
        case STCFL:                                     /* not decoded by this interface */
            break;
        }

    command_set &= ~command;                            /* remove the current command from the set */
    }                                                   /*   and continue with the remaining commands */

return status;                                          /* return the result of the call */
}
