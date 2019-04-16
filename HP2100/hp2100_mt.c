/* hp2100_mt.c: HP 2100 12559C 9-Track Magnetic Tape Unit Interface

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2019, J. David Bryan

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

   MT           12559C 9-Track Magnetic Tape Unit Interface

   01-Feb-19    JDB     Remap sim_tape_attach to avoid unwanted debug output
   24-Jan-19    JDB     Removed DEV_TAPE from DEVICE flags
   11-Jul-18    JDB     Revised I/O model
   20-Jul-17    JDB     Removed "mtc_stopioe" variable and register
   13-Mar-17    JDB     Deprecated LOCKED/WRITEENABLED for ATTACH -R
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Jan-13    MP      Added DEV_TAPE to DEVICE flags
   09-May-12    JDB     Separated assignments from conditional expressions
   25-Mar-12    JDB     Removed redundant MTAB_VUN from "format" MTAB entry
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   29-Oct-10    JDB     Fixed command scanning error in mtcio ioIOO handler
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   04-Sep-08    JDB     Fixed missing flag after CLR command
   02-Sep-08    JDB     Moved write enable and format commands from MTD to MTC
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   07-Oct-04    JDB     Allow enable/disable from either device
   14-Aug-04    RMS     Modified handling of end of medium (suggested by Dave Bryan)
   06-Jul-04    RMS     Fixed spurious timing error after CLC (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   21-Dec-03    RMS     Adjusted msc_ctime for TSB (from Mike Gemeny)
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised for magtape library
   30-Sep-02    RMS     Revamped error handling
   28-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length test
   20-Jan-02    RMS     Fixed bug on last character write
   03-Dec-01    RMS     Added read only unit, extended SET/SHOW support
   07-Sep-01    RMS     Moved function prototypes
   30-Nov-00    RMS     Made variable names unique
   04-Oct-98    RMS     V2.4 magtape format

   References:
     - 12559A 9-Track Magnetic Tape Unit Interface Kit Operating and Service Manual
         (12559-9001, July 1970)
     - SIMH Magtape Representation and Handling
         (Bob Supnik, 30-Aug-2006)


   The 3030 was one of HP's earliest tape drives.  The 12559A controller
   supported a single 800 bpi, 9-track drive, operating at 75 inches per second.
   It had two unusual characteristics:

    - The controller accepted only one byte per I/O word, rather than packing
      two bytes per word.

    - The drive could not read or write fewer than 12 bytes per record.

   The first behavior meant that DMA operation required the byte-unpacking
   feature of the 12578A DMA card for the 2116 computer.  The second meant that
   software drivers had to pad short records with blanks or nulls.


   Implementation notes:

    1. The HP 3030 Magnetic Tape Subsystem diagnostic, part number 20433-60001,
       has never been located, so this simulator has not been fully tested.  It
       does pass a functional test under DOS-III using driver DVR22.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"

#include "sim_tape.h"



/* Remap tape attach in 4.x to avoid unwanted debug output */

#if (SIM_MAJOR >= 4)
  #define sim_tape_attach(a,b) sim_tape_attach_ex (a, b, 0, 0)
#endif



#define DB_V_SIZE       16                      /* max data buf */
#define DBSIZE          (1 << DB_V_SIZE)        /* max data cmd */

/* Command - mtc_fnc */

#define FNC_CLR         0300                    /* clear */
#define FNC_WC          0031                    /* write */
#define FNC_RC          0023                    /* read */
#define FNC_GAP         0011                    /* write gap */
#define FNC_FSR         0003                    /* forward space */
#define FNC_BSR         0041                    /* backward space */
#define FNC_REW         0201                    /* rewind */
#define FNC_RWS         0101                    /* rewind and offline */
#define FNC_WFM         0035                    /* write file mark */

/* Status - stored in mtc_sta, (d) = dynamic */

#define STA_LOCAL       0400                    /* local (d) */
#define STA_EOF         0200                    /* end of file */
#define STA_BOT         0100                    /* beginning of tape */
#define STA_EOT         0040                    /* end of tape */
#define STA_TIM         0020                    /* timing error */
#define STA_REJ         0010                    /* programming error */
#define STA_WLK         0004                    /* write locked (d) */
#define STA_PAR         0002                    /* parity error */
#define STA_BUSY        0001                    /* busy (d) */

/* Interface state */

typedef struct {
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE mtd;                          /* data per-card state */
static CARD_STATE mtc;                          /* command per-card state */


/* Interface local SCP support routines */

static INTERFACE mtd_interface;
static INTERFACE mtc_interface;


static int32 mtc_fnc = 0;                       /* function */
static int32 mtc_sta = 0;                       /* status register */
static int32 mtc_dtf = 0;                       /* data xfer flop */
static int32 mtc_1st = 0;                       /* first svc flop */
static int32 mtc_ctime = 40;                    /* command wait */
static int32 mtc_gtime = 1000;                  /* gap stop time */
static int32 mtc_xtime = 15;                    /* data xfer time */
static uint8 mtxb[DBSIZE] = { 0 };              /* data buffer */
static t_mtrlnt mt_ptr = 0, mt_max = 0;         /* buffer ptrs */
static const uint32 mtc_cmd[] = {
    FNC_WC, FNC_RC, FNC_GAP, FNC_FSR, FNC_BSR, FNC_REW, FNC_RWS, FNC_WFM };
static const uint32 mtc_cmd_count = sizeof (mtc_cmd) / sizeof (mtc_cmd[0]);

static t_stat mtc_svc (UNIT *uptr);
static t_stat mt_reset (DEVICE *dptr);
static t_stat mtc_attach (UNIT *uptr, CONST char *cptr);
static t_stat mtc_detach (UNIT *uptr);
static t_stat mt_map_err (UNIT *uptr, t_stat st);
static t_stat mt_clear (void);

/* Device information blocks */

static DIB mt_dib [] = {
    { &mtd_interface,                                                   /* the device's I/O interface function pointer */
      MTD,                                                              /* the device's select code (02-77) */
      0,                                                                /* the card index */
      "12559C 9-Track Magnetic Tape Unit Interface Data Channel",       /* the card description */
      NULL },                                                           /* the ROM description */

    { &mtc_interface,                                                   /* the device's I/O interface function pointer */
      MTC,                                                              /* the device's select code (02-77) */
      0,                                                                /* the card index */
      "12559C 9-Track Magnetic Tape Unit Interface Command Channel",    /* the card description */
      NULL }                                                            /* the ROM description */
    };

#define mtd_dib             mt_dib [0]
#define mtc_dib             mt_dib [1]


/* Data card SCP data structures */


/* Unit list */

static UNIT mtd_unit [] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (NULL,              0,         0)           }
    };


/* Register list */

static REG mtd_reg [] = {
    { FLDATA (FLG, mtd.flag,    0) },
    { FLDATA (FBF, mtd.flag_buffer, 0) },
    { BRDATA (DBUF, mtxb, 8, 8, DBSIZE) },
    { DRDATA (BPTR, mt_ptr, DB_V_SIZE + 1) },
    { DRDATA (BMAX, mt_max, DB_V_SIZE + 1) },

      DIB_REGS (mtd_dib),

    { NULL }
    };


/* Modifier list */

static MTAB mtd_mod [] = {
/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor       */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &mt_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &mt_dib },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB mt_deb [] = {
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE mtd_dev = {
    "MTD",                                      /* device name */
    mtd_unit,                                   /* unit array */
    mtd_reg,                                    /* register array */
    mtd_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    16,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mt_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mtd_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    mt_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };


/* Command card SCP data structures */


/* Unit list */

#define UNIT_FLAGS          (UNIT_ATTABLE | UNIT_ROABLE)

static UNIT mtc_unit [] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (&mtc_svc,      UNIT_FLAGS,    0)           },
    };


/* Register list */

static REG mtc_reg [] = {
    { ORDATA (FNC, mtc_fnc, 8) },
    { ORDATA (STA, mtc_sta, 9) },
    { ORDATA (BUF, mtc_unit [0].buf, 8) },
    { FLDATA (CTL, mtc.control, 0) },
    { FLDATA (FLG, mtc.flag,    0) },
    { FLDATA (FBF, mtc.flag_buffer, 0) },
    { FLDATA (DTF, mtc_dtf, 0) },
    { FLDATA (FSVC, mtc_1st, 0) },
    { DRDATA (POS, mtc_unit [0].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (CTIME, mtc_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (GTIME, mtc_gtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, mtc_xtime, 24), REG_NZ + PV_LEFT },

      DIB_REGS (mtc_dib),

    { NULL }
    };


/* Modifier list.

   The LOCKED and WRITEENABLED modifiers are deprecated.  The supported method
   of write-protecting a tape drive is to attach the tape image with the -R
   (read-only) switch or by setting the host operating system's read-only
   attribute on the tape image file.  This simulates removing the write ring
   from the tape reel before mounting it on the drive.  There is no hardware
   method of write-protecting a mounted and positioned tape reel.


   Implementation notes:

    1. The UNIT_RO modifier displays "write ring" if the flag is not set.  There
       is no corresponding entry for the opposite condition because "read only"
       is automatically printed after the attached filename.

    2. FORMAT is really a unit option, but as there is only one unit, it is
       specified as MTAB_XDV so that SHOW MTC FORMAT is accepted, rather than
       requiring SHOW MTC0 FORMAT.
*/

static MTAB mtc_mod [] = {
/*    Mask Value     Match Value    Print String      Match String     Validation    Display  Descriptor */
/*    -------------  -------------  ----------------  ---------------  ------------  -------  ---------- */
    { UNIT_RO,       0,             "write ring",     NULL,            NULL,         NULL,    NULL       },

    { MTUF_WLK,      0,             NULL,             "WRITEENABLED",  NULL,         NULL,    NULL       },
    { MTUF_WLK,      MTUF_WLK,      NULL,             "LOCKED",        NULL,         NULL,    NULL       },


/*    Entry Flags          Value  Print String  Match String  Validation         Display             Descriptor       */
/*    -------------------  -----  ------------  ------------  -----------------  ------------------  ---------------- */
    { MTAB_XDV,              0,   "FORMAT",     "FORMAT",     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL             },

    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,       &hp_show_dib,       (void *) &mt_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,       &hp_show_dib,       (void *) &mt_dib },
    { 0 }
    };


/* Device descriptor */

DEVICE mtc_dev = {
    "MTC",                                      /* device name */
    mtc_unit,                                   /* unit array */
    mtc_reg,                                    /* register array */
    mtc_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mt_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    &mtc_attach,                                /* attach routine */
    &mtc_detach,                                /* detach routine */
    &mtc_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    mt_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Data channel interface.

   The 12559A data channel interface has a number of non-standard features:

     - The card does not drive PRL or IRQ.
     - The card does not respond to IAK.
     - There is no control flip-flop; CLC resets the data transfer flip-flop.
     - POPIO issues a CLR command and clears the flag and flag buffer flip-flops.
     - CRS is not used.

   Implementation notes:

    1. The data channel has a flag buffer flip-flop (necessary for the proper
       timing of the flag flip-flop), but the data channel does not interrupt,
       so the flag buffer serves no other purpose.
*/

static SIGNALS_VALUE mtd_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            mtd.flag_buffer = CLEAR;                    /* reset the flag buffer */
            mtd.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            mtd.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (mtd.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                mtd.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (mtd.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (mtd.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = mtc_unit [0].buf;          /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            mtc_unit [0].buf = inbound_value & D8_MASK; /* store data */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            mt_clear ();                                /* issue CLR to controller */
            mtd.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            mtd.flag_buffer = CLEAR;                    /* reset the flag buffer */
            mtd.flag        = CLEAR;                    /*   and flag flip-flops */

            mtc_dtf = 0;                                /* clr xfer flop */
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (mtd.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioPRH:                                         /* Priority High */
            outbound.signals |= ioPRL | cnPRL | cnVALID;    /* PRL is tied to PRH */
            break;


        case ioSTC:                                     /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioIAK:                                     /* not used by this interface */
        case ioIEN:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* Command channel interface.

   The 12559A command interface is reasonably standard, although POPIO clears,
   rather than sets, the flag and flag buffer flip-flops.  One unusual feature
   is that commands are initiated when they are output to the interface with
   OTA/B, rather than waiting until control is set with STC.  STC simply enables
   command-channel interrupts.

   Implementation notes:

    1. In hardware, the command channel card passes PRH to PRL.  The data card
       actually drives PRL with the command channel's control and flag states.
       That is, the priority chain is broken at the data card, although the
       command card is interrupting.  This works in hardware, but we must break
       PRL at the command card under simulation to allow the command card to
       interrupt.

    2. In hardware, the CLR command takes 5 milliseconds to complete.  During
       this time, the BUSY bit is set in the status word.  Under simulation, we
       complete immediately, and the BUSY bit never sets..
*/

static SIGNALS_VALUE mtc_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
uint32         i, data;
int32          valid;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            mtc.flag_buffer = CLEAR;                    /* reset the flag buffer */
            mtc.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            mtc.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (mtc.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                mtc.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (mtc.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (mtc.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = mtc_sta & ~(STA_LOCAL | STA_WLK | STA_BUSY);

            if (mtc_unit [0].flags & UNIT_ATT) {        /* construct status */
                if (sim_is_active (mtc_unit))
                    outbound.value |= STA_BUSY;

                if (sim_tape_wrp (mtc_unit))
                    outbound.value |= STA_WLK;
                }

            else
                outbound.value |= STA_BUSY | STA_LOCAL;
            break;


        case ioIOO:                                         /* I/O data output */
            data = inbound_value & D8_MASK;                 /* only the lower 8 bits are connected */
            mtc_sta = mtc_sta & ~STA_REJ;                   /* clear reject */

            if (data == FNC_CLR) {                          /* clear? */
                mt_clear ();                                /* send CLR to controller */

                mtd.flag_buffer = mtd.flag = CLEAR;         /* clear data flag buffer and flag */
                mtc.flag_buffer = mtc.flag = SET;           /* set command flag buffer and flag */
                break;                                      /* command completes immediately */
                }

            for (i = valid = 0; i < mtc_cmd_count; i++)     /* is fnc valid? */
                if (data == mtc_cmd[i]) {
                    valid = 1;
                    break;
                    }

            if (!valid || sim_is_active (mtc_unit) ||       /* is cmd valid? */
               ((mtc_sta & STA_BOT) && (data == FNC_BSR)) ||
               (sim_tape_wrp (mtc_unit) &&
                 ((data == FNC_WC) || (data == FNC_GAP) || (data == FNC_WFM))))
                mtc_sta = mtc_sta | STA_REJ;

            else {
                sim_activate (mtc_unit, mtc_ctime);         /* start tape */
                mtc_fnc = data;                             /* save function */
                mtc_sta = STA_BUSY;                         /* unit busy */
                mt_ptr = 0;                                 /* init buffer ptr */

                mtd.flag_buffer = mtd.flag = CLEAR;         /* clear data flag buffer and flag */
                mtc.flag_buffer = mtc.flag = CLEAR;         /*   and command flag buffer and flag */

                mtc_1st = 1;                                /* set 1st flop */
                mtc_dtf = 1;                                /* set xfer flop */
                }
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            mtc.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
            mtc.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioCRS:                                     /* Control Reset */
            mtc.control = CLEAR;                        /* clear the control flip-flop */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            mtc.control = CLEAR;
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            mtc.control = SET;
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (mtc.control & mtc.flag)                 /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (mtc.control & mtc.flag & mtc.flag_buffer)   /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;        /*   then conditionally assert IRQ */

            if (mtc.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            mtc.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
            break;


        case ioIEN:                                     /* Interrupt Enable */
            irq_enabled = TRUE;                         /* permit IRQ to be asserted */
            break;


        case ioPRH:                                         /* Priority High */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;                  /*   then assert it unconditionally */
            break;


        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt

   Can't be write locked, can only write lock detached unit
*/

t_stat mtc_svc (UNIT *uptr)
{
t_mtrlnt tbc;
t_stat st, r = SCPE_OK;

if ((mtc_unit [0].flags & UNIT_ATT) == 0) {             /* offline? */
    mtc_sta = STA_LOCAL | STA_REJ;                      /* rejected */

    mtc.flag_buffer = SET;                              /* set the flag buffer */
    io_assert (&mtc_dev, ioa_ENF);                      /*   and flag flip-flops */

    return SCPE_OK;
    }

switch (mtc_fnc) {                                      /* case on function */

    case FNC_REW:                                       /* rewind */
        sim_tape_rewind (uptr);                         /* BOT */
        mtc_sta = STA_BOT;                              /* update status */
        break;

    case FNC_RWS:                                       /* rewind and offline */
        sim_tape_rewind (uptr);                         /* clear position */
        return sim_tape_detach (uptr);                  /* don't set cch flg */

    case FNC_WFM:                                       /* write file mark */
        st = sim_tape_wrtmk (uptr);                     /* write tmk */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        mtc_sta = STA_EOF;                              /* set EOF status */
        break;

    case FNC_GAP:                                       /* erase gap */
        break;

    case FNC_FSR:                                       /* space forward */
        st = sim_tape_sprecf (uptr, &tbc);              /* space rec fwd */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;

    case FNC_BSR:                                       /* space reverse */
        st = sim_tape_sprecr (uptr, &tbc);              /* space rec rev */
        if (st != MTSE_OK)                              /* error? */
            r = mt_map_err (uptr, st);                  /* map error */
        break;

    case FNC_RC:                                        /* read */
        if (mtc_1st) {                                  /* first svc? */
            mtc_1st = mt_ptr = 0;                       /* clr 1st flop */
            st = sim_tape_rdrecf (uptr, mtxb, &mt_max, DBSIZE); /* read rec */
            if (st == MTSE_RECE) mtc_sta = mtc_sta | STA_PAR;   /* rec in err? */
            else if (st != MTSE_OK) {                   /* other error? */
                r = mt_map_err (uptr, st);              /* map error */
                if (r == SCPE_OK) {                     /* recoverable? */
                    sim_activate (uptr, mtc_gtime);     /* sched IRG */
                    mtc_fnc = 0;                        /* NOP func */
                    return SCPE_OK;
                    }
                break;                                  /* non-recov, done */
                }
            if (mt_max < 12) {                          /* record too short? */
                mtc_sta = mtc_sta | STA_PAR;            /* set flag */
                break;
                }
            }
        if (mtc_dtf && (mt_ptr < mt_max)) {             /* more chars? */
            if (mtd.flag) mtc_sta = mtc_sta | STA_TIM;
            mtc_unit [0].buf = mtxb [mt_ptr++];         /* fetch next */

            mtd.flag_buffer = SET;                      /* set the flag buffer */
            io_assert (&mtd_dev, ioa_ENF);              /*   and flag flip-flops */

            sim_activate (uptr, mtc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        sim_activate (uptr, mtc_gtime);                 /* schedule gap */
        mtc_fnc = 0;                                    /* nop */
        return SCPE_OK;

    case FNC_WC:                                                /* write */
        if (mtc_1st) mtc_1st = 0;                       /* no xfr on first */
        else {
            if (mt_ptr < DBSIZE) {                      /* room in buffer? */
                mtxb[mt_ptr++] = (uint8) mtc_unit [0].buf;
                mtc_sta = mtc_sta & ~STA_BOT;           /* clear BOT */
                }
            else mtc_sta = mtc_sta | STA_PAR;
            }
        if (mtc_dtf) {                                  /* xfer flop set? */
            mtd.flag_buffer = SET;                      /* set the flag buffer */
            io_assert (&mtd_dev, ioa_ENF);              /*   and flag flip-flops */

            sim_activate (uptr, mtc_xtime);             /* re-activate */
            return SCPE_OK;
            }
        if (mt_ptr) {                                   /* write buffer */
            st = sim_tape_wrrecf (uptr, mtxb, mt_ptr);  /* write */
            if (st != MTSE_OK) {                        /* error? */
                r = mt_map_err (uptr, st);              /* map error */
                break;                                  /* done */
                }
            }
        sim_activate (uptr, mtc_gtime);                 /* schedule gap */
        mtc_fnc = 0;                                    /* nop */
        return SCPE_OK;

    default:                                            /* unknown */
        break;
        }

mtc.flag_buffer = SET;                                  /* set the flag buffer */
io_assert (&mtc_dev, ioa_ENF);                          /*   and flag flip-flops */
mtc_sta = mtc_sta & ~STA_BUSY;                          /* not busy */
return r;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* unattached */
        mtc_sta = mtc_sta | STA_REJ;                    /* reject */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;                               /* never get here! */

    case MTSE_EOM:                                      /* end of medium */
    case MTSE_TMK:                                      /* end of file */
        mtc_sta = mtc_sta | STA_EOF;                    /* eof */
        break;

    case MTSE_IOERR:                                    /* IO error */
        mtc_sta = mtc_sta | STA_PAR;                    /* error */
        return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mtc_sta = mtc_sta | STA_PAR;
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        mtc_sta = mtc_sta | STA_PAR;                    /* error */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        mtc_sta = mtc_sta | STA_BOT;                    /* set status */
        break;

    case MTSE_WRP:                                      /* write protect */
        mtc_sta = mtc_sta | STA_REJ;                    /* reject */
        break;
        }

return SCPE_OK;
}


/* Controller clear */

t_stat mt_clear (void)
{
t_stat st;

if (sim_is_active (mtc_unit) &&                         /* write in prog? */
    (mtc_fnc == FNC_WC) && (mt_ptr > 0)) {              /* yes, bad rec */
    st = sim_tape_wrrecf (mtc_unit, mtxb, mt_ptr | MTR_ERF);
    if (st != MTSE_OK)
        mt_map_err (mtc_unit, st);
    }

if (((mtc_fnc == FNC_REW) || (mtc_fnc == FNC_RWS)) && sim_is_active (mtc_unit))
    sim_cancel (mtc_unit);

mtc_1st = mtc_dtf = 0;
mtc_sta = mtc_sta & STA_BOT;

return SCPE_OK;
}


/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &mtd_dev) ? &mtc_dev : &mtd_dev);

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

mtc_fnc = 0;
mtc_1st = mtc_dtf = 0;

sim_cancel (mtc_unit);                                  /* cancel activity */
sim_tape_reset (mtc_unit);

if (mtc_unit [0].flags & UNIT_ATT)
    mtc_sta = (sim_tape_bot (mtc_unit)? STA_BOT: 0) |
              (sim_tape_wrp (mtc_unit)? STA_WLK: 0);
else
    mtc_sta = STA_LOCAL | STA_BUSY;

return SCPE_OK;
}


/* Attach routine */

t_stat mtc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);                       /* attach unit */
if (r != SCPE_OK) return r;                             /* update status */
mtc_sta = STA_BOT;
return r;
}

/* Detach routine */

t_stat mtc_detach (UNIT* uptr)
{
mtc_sta = 0;                                            /* update status */
return sim_tape_detach (uptr);                          /* detach unit */
}
