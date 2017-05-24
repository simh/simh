/* hp2100_ds.c: HP 13037D/13175D disc controller/interface simulator

   Copyright (c) 2004-2012, Robert M. Supnik
   Copyright (c) 2012-2016  J. David Bryan

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

   DS           13037D/13175D disc controller/interface

   13-May-16    JDB     Modified for revised SCP API function parameter types
   04-Mar-16    JDB     Name changed to "hp2100_disclib" until HP 3000 integration
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Use T_ADDR_FMT with t_addr values for 64-bit compatibility
   18-Mar-13    JDB     Fixed poll_drives definition to match declaration
   24-Oct-12    JDB     Changed CNTLR_OPCODE to title case to avoid name clash
   29-Mar-12    JDB     Rewritten to use the MAC/ICD disc controller library
                        ioIOO now notifies controller service of parameter output
   14-Feb-12    JDB     Corrected SRQ generation and FIFO under/overrun detection
                        Corrected Clear command to conform to the hardware
                        Fixed Request Status to return Unit Unavailable if illegal
                        Seek and Cold Load Read now Seek Check if seek in progress
                        Remodeled command wait for seek completion
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   21-Jun-11    JDB     Corrected status returns for disabled drive, auto-seek
                        beyond drive limits, Request Sector Address and Wakeup
                        with invalid or offline unit
                        Address verification reenabled if auto-seek during
                        Read Without Verify
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   31-Dec-07    JDB     Corrected and verified ioCRS action
   20-Dec-07    JDB     Corrected DPTR register definition from FLDATA to DRDATA
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   03-Aug-06    JDB     Fixed REQUEST STATUS command to clear status-1
                        Removed redundant attached test in "ds_detach"
   18-Mar-05    RMS     Added attached test to detach routine
   01-Mar-05    JDB     Added SET UNLOAD/LOAD

   References:
   - 13037 Disc Controller Technical Information Package (13037-90902, Aug-1980)
   - 7925D Disc Drive Service Manual (07925-90913, Apr-1984)
   - HP 12992 Loader ROMs Installation Manual (12992-90001, Apr-1986)
   - DVR32 RTE Moving Head Driver source (92084-18711, Revision 5000)


   The 13037D multiple-access (MAC) disc controller supports from one to eight
   HP 7905 (15 MB), 7906 (20MB), 7920 (50 MB), and 7925 (120 MB) disc drives
   accessed by one to eight CPUs.  The controller hardware consists of a 16-bit
   microprogrammed processor constructed from 74S181 bit slices operating at 5
   MHz, a device controller providing the interconnections to the drives and CPU
   interfaces, and an error correction controller that enables the correction of
   up to 32-bit error bursts.  1024 words of 24-bit firmware are stored in ROM.

   The 13175D disc interface is used to connect the HP 1000 CPU to the 13037
   device controller.  In a multiple-CPU system, one interface is strapped to
   reset the controller when the CPU's front panel PRESET button is pressed.

   This module simulates a 13037D connected to a single 13175D interface.  From
   one to eight drives may be connected, and drive types may be freely
   intermixed.  A unit that is enabled but not attached appears to be a
   connected drive that does not have a disc pack in place.  A unit that is
   disabled appears to be disconnected.

   This simulator is an adaptation of the code originally written by Bob Supnik.
   The functions of the controller have been separated from the functions of the
   interface, with the former placed into a separate disc controller library.
   This allows the library to support other CPU interfaces, such as the 12821A
   HP-IB disc interface, that use substantially different communication
   protocols.  The library functions implement the controller command set for
   the drive units.  The interface functions handle the transfer of commands and
   data to and from the CPU.

   In hardware, the controller runs continuously in one of three states: in the
   Poll Loop (idle state), in the Command Wait Loop (wait state), or in command
   execution (busy state).  In simulation, the controller is run only when a
   command is executing or when a transition into or out of the two loops might
   occur.  Internally, the controller handles these transitions:

    - when a command other than End terminates (busy => wait)
    - when the End command terminates (busy => idle)
    - when a command timeout occurs (wait => idle)
    - when a parameter timeout occurs (busy => idle)
    - when a seek completes (if idle and interrupts are enabled, idle => wait)

   The interface must call the controller library to handle these transitions:

    - when a command is received from the CPU (idle or wait => busy)
    - when interrupts are enabled (if idle and drive Attention, idle => wait)

   In addition, each transition to the wait state must check for a pending
   command, and each transition to the idle state must check for both a pending
   command and a drive with Attention status asserted.


   Implementation notes:

    1. Although the 13175D has a 16-word FIFO, the "full" level is set at 5
       entries in hardware to avoid a long DCPC preemption time at the start of
       a disc write as the FIFO fills.
*/



#include "hp2100_defs.h"
#include "hp2100_disclib.h"



/* Program constants */

#define DS_DRIVES       (DL_MAXDRIVE + 1)               /* number of disc drive units */
#define DS_UNITS        (DS_DRIVES + DL_AUXUNITS)       /* total number of units */

#define ds_cntlr        ds_unit [DL_MAXDRIVE + 1]       /* controller unit alias */

#define FIFO_SIZE       16                              /* FIFO depth */

#define FIFO_EMPTY      (ds.fifo_count == 0)            /* FIFO empty test */
#define FIFO_STOP       (ds.fifo_count >= 5)            /* FIFO stop filling test */
#define FIFO_FULL       (ds.fifo_count == FIFO_SIZE)    /* FIFO full test */

#define PRESET_ENABLE   TRUE                            /* Preset Jumper (W4) is enabled */


/* Debug flags */

#define DEB_CPU         (1 << 0)                        /* words received from and sent to the CPU */
#define DEB_CMDS        (1 << 1)                        /* interface commands received from the CPU */
#define DEB_BUF         (1 << 2)                        /* data read from and written to the card FIFO */
#define DEB_RWSC        (1 << 3)                        /* device read/write/status/control commands */
#define DEB_SERV        (1 << 4)                        /* unit service scheduling calls */



/* Per-card state variables */

typedef struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    FLIP_FLOP srq;                                      /* SRQ flip-flop */
    FLIP_FLOP edt;                                      /* EDT flip-flop */
    FLIP_FLOP cmfol;                                    /* command follows flip-flop */
    FLIP_FLOP cmrdy;                                    /* command ready flip-flop */
    uint16    fifo [FIFO_SIZE];                         /* FIFO buffer */
    uint32    fifo_count;                               /* FIFO occupancy counter */
    REG      *fifo_reg;                                 /* FIFO register pointer */
    } CARD_STATE;


/* MAC disc state variables */

static UNIT ds_unit [DS_UNITS];                         /* unit array */

static CARD_STATE ds;                                   /* card state */

static uint16 buffer [DL_BUFSIZE];                      /* command/status/sector buffer */

static CNTLR_VARS mac_cntlr =                           /* MAC controller */
    { CNTLR_INIT (MAC, buffer, &ds_cntlr) };



/* MAC disc global VM routines */

IOHANDLER ds_io;
t_stat    ds_service_drive      (UNIT   *uptr);
t_stat    ds_service_controller (UNIT   *uptr);
t_stat    ds_service_timer      (UNIT   *uptr);
t_stat    ds_reset              (DEVICE *dptr);
t_stat    ds_attach             (UNIT   *uptr,  CONST char *cptr);
t_stat    ds_detach             (UNIT   *uptr);
t_stat    ds_boot               (int32  unitno, DEVICE *dptr);

/* MAC disc global SCP routines */

t_stat ds_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

/* MAC disc local utility routines */

static void   start_command     (void);
static void   poll_interface    (void);
static void   poll_drives       (void);
static void   fifo_load         (uint16 data);
static uint16 fifo_unload       (void);
static void   fifo_clear        (void);
static t_stat activate_unit     (UNIT *uptr);



/* MAC disc VM data structures.

   ds_dib       DS device information block
   ds_unit      DS unit list
   ds_reg       DS register list
   ds_mod       DS modifier list
   ds_deb       DS debug table
   ds_dev       DS device descriptor

   For the drive models, the modifiers provide this SHOW behavior:

    - when detached and autosized, prints "autosize"
    - when detached and not autosized, prints the model number
    - when attached, prints the model number (regardless of autosizing)


   Implementation notes:

    1. The validation routine does not allow the model number or autosizing
       option to be changed when the unit is attached.  Therefore, specifying
       UNIT_ATT in the mask field has no adverse effect.

    2. The modifier DEVNO is deprecated in favor of SC but is retained for
       compatibility.
*/


DEVICE ds_dev;

static DIB ds_dib = { &ds_io, DS };

#define UNIT_FLAGS  (UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE | UNIT_DISABLE | UNIT_UNLOAD)

static UNIT ds_unit [] = {
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 0 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 1 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 2 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 3 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 4 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 5 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 6 */
    { UDATA (&ds_service_drive,      UNIT_FLAGS | MODEL_7905, D7905_WORDS) },   /* drive unit 7 */
    { UDATA (&ds_service_controller, UNIT_DIS,                0)           },   /* controller unit */
    { UDATA (&ds_service_timer,      UNIT_DIS,                0)           }    /* timer unit */
    };

static REG ds_reg [] = {
    { FLDATA (CMFOL,  ds.cmfol,                0)                           },
    { FLDATA (CMRDY,  ds.cmrdy,                0)                           },
    { DRDATA (FCNT,   ds.fifo_count,           5)                           },
    { BRDATA (FIFO,   ds.fifo,                 8, 16, FIFO_SIZE), REG_CIRC  },
    { ORDATA (FREG,   ds.fifo_reg,            32), REG_HRO                  },

    { ORDATA (CNTYPE, mac_cntlr.type,          2), REG_HRO                  },
    { ORDATA (STATE,  mac_cntlr.state,         2)                           },
    { ORDATA (OPCODE, mac_cntlr.opcode,        6)                           },
    { ORDATA (STATUS, mac_cntlr.status,        6)                           },
    { FLDATA (EOC,    mac_cntlr.eoc,           0)                           },
    { FLDATA (EOD,    mac_cntlr.eod,           0)                           },
    { ORDATA (SPDU,   mac_cntlr.spd_unit,     16)                           },
    { ORDATA (FLMASK, mac_cntlr.file_mask,     4)                           },
    { ORDATA (RETRY,  mac_cntlr.retry,         4), REG_HRO                  },
    { ORDATA (CYL,    mac_cntlr.cylinder,     16)                           },
    { ORDATA (HEAD,   mac_cntlr.head,          6)                           },
    { ORDATA (SECTOR, mac_cntlr.sector,        8)                           },
    { ORDATA (VFYCNT, mac_cntlr.verify_count, 16)                           },
    { ORDATA (LASPOL, mac_cntlr.poll_unit,     3)                           },
    { HRDATA (BUFPTR, mac_cntlr.buffer,       32), REG_HRO                  },
    { BRDATA (BUFFER, buffer,              8, 16, DL_BUFSIZE)               },
    { DRDATA (INDEX,  mac_cntlr.index,         8)                           },
    { DRDATA (LENGTH, mac_cntlr.length,        8)                           },
    { HRDATA (AUXPTR, mac_cntlr.aux,          32), REG_HRO                  },
    { DRDATA (STIME,  mac_cntlr.seek_time,    24), PV_LEFT | REG_NZ         },
    { DRDATA (ITIME,  mac_cntlr.sector_time,  24), PV_LEFT | REG_NZ         },
    { DRDATA (CTIME,  mac_cntlr.cmd_time,     24), PV_LEFT | REG_NZ         },
    { DRDATA (DTIME,  mac_cntlr.data_time,    24), PV_LEFT | REG_NZ         },
    { DRDATA (WTIME,  mac_cntlr.wait_time,    31), PV_LEFT | REG_NZ         },

    { FLDATA (CTL,    ds.control,              0)                           },
    { FLDATA (FLG,    ds.flag,                 0)                           },
    { FLDATA (FBF,    ds.flagbuf,              0)                           },
    { FLDATA (SRQ,    ds.srq,                  0)                           },
    { FLDATA (EDT,    ds.edt,                  0)                           },

    { URDATA (UCYL,   ds_unit[0].CYL,   10, 10,       0, DS_UNITS, PV_LEFT) },
    { URDATA (UOP,    ds_unit[0].OP,     8,  6,       0, DS_UNITS, PV_RZRO) },
    { URDATA (USTAT,  ds_unit[0].STAT,   2,  8,       0, DS_UNITS, PV_RZRO) },
    { URDATA (UPHASE, ds_unit[0].PHASE,  8,  3,       0, DS_UNITS, PV_RZRO) },
    { URDATA (UPOS,   ds_unit[0].pos,    8, T_ADDR_W, 0, DS_UNITS, PV_LEFT) },
    { URDATA (UWAIT,  ds_unit[0].wait,   8, 32,       0, DS_UNITS, PV_LEFT) },

    { ORDATA (SC,     ds_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO,  ds_dib.select_code, 6), REG_HRO },
    { NULL }
    };

static MTAB ds_mod [] = {
/*    mask         match        pstring            mstring         valid            disp  desc */
    { UNIT_UNLOAD, UNIT_UNLOAD, "heads unloaded",  "UNLOADED",     &ds_load_unload, NULL, NULL },
    { UNIT_UNLOAD, 0,           "heads loaded",    "LOADED",       &ds_load_unload, NULL, NULL },

    { UNIT_WLK,    UNIT_WLK,    "write locked",    "LOCKED",       NULL,            NULL, NULL },
    { UNIT_WLK,    0,           "write enabled",   "WRITEENABLED", NULL,            NULL, NULL },

    { UNIT_FMT,    UNIT_FMT,    "format enabled",  "FORMAT",       NULL,            NULL, NULL },
    { UNIT_FMT,    0,           "format disabled", "NOFORMAT",     NULL,            NULL, NULL },

/*    mask                               match                  pstring     mstring     valid          disp  desc */
    { UNIT_AUTO | UNIT_ATT,              UNIT_AUTO,             "autosize", "AUTOSIZE", &dl_set_model, NULL, NULL },
    { UNIT_AUTO | UNIT_ATT | UNIT_MODEL, MODEL_7905,            "7905",     "7905",     &dl_set_model, NULL, NULL },
    { UNIT_AUTO | UNIT_ATT | UNIT_MODEL, MODEL_7906,            "7906",     "7906",     &dl_set_model, NULL, NULL },
    { UNIT_AUTO | UNIT_ATT | UNIT_MODEL, MODEL_7920,            "7920",     "7920",     &dl_set_model, NULL, NULL },
    { UNIT_AUTO | UNIT_ATT | UNIT_MODEL, MODEL_7925,            "7925",     "7925",     &dl_set_model, NULL, NULL },
    { UNIT_ATT | UNIT_MODEL,             UNIT_ATT | MODEL_7905, "7905",     NULL,       NULL,          NULL, NULL },
    { UNIT_ATT | UNIT_MODEL,             UNIT_ATT | MODEL_7906, "7906",     NULL,       NULL,          NULL, NULL },
    { UNIT_ATT | UNIT_MODEL,             UNIT_ATT | MODEL_7920, "7920",     NULL,       NULL,          NULL, NULL },
    { UNIT_ATT | UNIT_MODEL,             UNIT_ATT | MODEL_7925, "7925",     NULL,       NULL,          NULL, NULL },

    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &ds_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &ds_dev },
    { 0 }
    };

static DEBTAB ds_deb [] = {
    { "CPU",  DEB_CPU  },
    { "CMDS", DEB_CMDS },
    { "BUF",  DEB_BUF  },
    { "RWSC", DEB_RWSC },
    { "SERV", DEB_SERV },
    { NULL,   0 }
    };

DEVICE ds_dev = {
    "DS",                                               /* device name */
    ds_unit,                                            /* unit array */
    ds_reg,                                             /* register array */
    ds_mod,                                             /* modifier array */
    DS_UNITS,                                           /* number of units */
    8,                                                  /* address radix */
    27,                                                 /* address width = 128 MB */
    1,                                                  /* address increment */
    8,                                                  /* data radix */
    16,                                                 /* data width */
    NULL,                                               /* examine routine */
    NULL,                                               /* deposit routine */
    &ds_reset,                                          /* reset routine */
    &ds_boot,                                           /* boot routine */
    &ds_attach,                                         /* attach routine */
    &ds_detach,                                         /* detach routine */
    &ds_dib,                                            /* device information block */
    DEV_DEBUG | DEV_DISABLE,                            /* device flags */
    0,                                                  /* debug control flags */
    ds_deb,                                             /* debug flag name table */
    NULL,                                               /* memory size change routine */
    NULL                                                /* logical device name */
    };



/* MAC disc global VM routines */


/* I/O signal handler.

   The 13175D disc interface data path consists of an input multiplexer/latch
   and a 16-word FIFO buffer.  The FIFO source may be either the CPU's I/O
   input bus or the controller's interface data bus.  The output of the FIFO may
   be enabled either to the CPU's I/O output bus or the interface data bus.

   The control path consists of the usual control, flag buffer, flag, and SRQ
   flip-flops, although flag and SRQ are decoupled to allow the full DCPC
   transfer rate through the FIFO (driving SRQ from the flag limits transfers to
   every other cycle).  SRQ is based on the FIFO level: if data or room in the
   FIFO is available, SRQ is set to initiate a transfer.  The flag is only used
   to signal an interrupt at the end of a command.

   One unusual aspect is that SFC and SFS test different things, rather than
   complementary states of the same thing.  SFC tests the controller busy state,
   and SFS tests the flag flip-flop.

   In addition, the card contains end-of-data-transfer, command-follows, and
   command-ready flip-flops.  EDT is set when the DCPC EDT signal is asserted
   and is used in conjunction with the FIFO level to assert the end-of-data
   signal to the controller.  The command-follows flip-flop is set by a CLC to
   indicate that the next data word output from the CPU is a disc command.  The
   command-ready flip-flop is set when a command is received to schedule an
   interface poll.


   Implementation notes:

    1. In hardware, SRQ is enabled only when the controller is reading or
       writing the disc (IFIN or IFOUT functions are asserted) and set when the
       FIFO is not empty (read) or not full (write).  In simulation, SRQ is set
       by the unit service read/write data phase transfers and cleared in the
       IOI and IOO signal handlers when the FIFO is empty (read) or full
       (write).

    2. The DCPC EDT signal cannot set the controller's end-of-data flag directly
       because a write EOD must occur only after the FIFO has been drained.

    3. Polling the interface or drives must be deferred to the end of I/O signal
       handling.  If they are performed in the IOO/STC handlers themselves, an
       associated CLF might clear the flag that was set by the poll.

    4. Executing a CLC sets the controller's end-of-data flag, which will abort
       a read or write data transfer in progress.  Parameter transfers are not
       affected.  If a command is received when a parameter is expected, the
       word is interpreted as data, even though the command-ready flip-flop is
       set.  The controller firmware only checks DTRDY for a parameter transfer,
       and DTRDY is asserted whenever the FIFO is not empty.

    5. The hardware Interface Function and Flag Buses are not implemented
       explicitly.  Instead, interface functions and signals are inferred by the
       interface from the current command operation and phase.
*/

uint32 ds_io (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
static const char * const output_state [] = { "Data", "Command" };
const char * const hold_or_clear = (signal_set & ioCLF ? ",C" : "");

uint16   data;
t_stat   status;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */
t_bool   command_issued = FALSE;
t_bool   interrupt_enabled = FALSE;

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            ds.flag = CLEAR;                            /* clear the flag */
            ds.flagbuf = CLEAR;                         /*   and flag buffer */

            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fputs (">>DS cmds: [CLF] Flag cleared\n", sim_deb);
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            ds.flag = SET;                              /* set the flag */
            ds.flagbuf = SET;                           /*   and flag buffer */

            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fputs (">>DS cmds: [STF] Flag set\n", sim_deb);
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setSKF (mac_cntlr.state != cntlr_busy);     /* skip if the controller is not busy */
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (ds);                             /* assert SKF if the flag is set */
            break;


        case ioIOI:                                         /* I/O data input */
            data = fifo_unload ();                          /* unload the next word from the FIFO */
            stat_data = IORETURN (SCPE_OK, data);           /* merge in the return status */

            if (DEBUG_PRI (ds_dev, DEB_CPU))
                fprintf (sim_deb, ">>DS cpu:  [LIx%s] Data = %06o\n", hold_or_clear, data);

            if (FIFO_EMPTY) {                               /* is the FIFO now empty? */
                if (ds.srq == SET && DEBUG_PRI (ds_dev, DEB_CMDS))
                    fprintf (sim_deb, ">>DS cmds: [LIx%s] SRQ cleared\n", hold_or_clear);

                ds.srq = CLEAR;                             /* clear SRQ */

                if (ds_cntlr.PHASE == data_phase) {         /* is this an outbound parameter? */
                    ds_cntlr.wait = mac_cntlr.data_time;    /* activate the controller */
                    activate_unit (&ds_cntlr);              /*   to acknowledge the data */
                    }
                }
            break;


        case ioIOO:                                         /* I/O data output */
            data = IODATA (stat_data);                      /* mask to just the data word */

            if (DEBUG_PRI (ds_dev, DEB_CPU))
                fprintf (sim_deb, ">>DS cpu:  [OTx%s] %s = %06o\n",
                         hold_or_clear, output_state [ds.cmfol], data);

            fifo_load (data);                               /* load the word into the FIFO */

            if (ds.cmfol == SET) {                          /* are we expecting a command? */
                ds.cmfol = CLEAR;                           /* clear the command follows flip-flop */
                ds.cmrdy = SET;                             /* set the command ready flip-flop */
                command_issued = TRUE;                      /*   and request an interface poll */
                }

            else {                                          /* not a command */
                if (ds_cntlr.PHASE == data_phase) {         /* is this an inbound parameter? */
                    ds_cntlr.wait = mac_cntlr.data_time;    /* activate the controller */
                    activate_unit (&ds_cntlr);              /*   to receive the data */
                    }

                if (FIFO_STOP) {                            /* is the FIFO now full enough? */
                    if (ds.srq == SET && DEBUG_PRI (ds_dev, DEB_CMDS))
                        fprintf (sim_deb, ">>DS cmds: [OTx%s] SRQ cleared\n", hold_or_clear);

                    ds.srq = CLEAR;                         /* clear SRQ to stop filling */
                    }
                }
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            ds.flag = SET;                              /* set the flag */
            ds.flagbuf = SET;                           /*   and flag buffer */
            ds.cmrdy = CLEAR;                           /* clear the command ready flip-flop */

            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fputs (">>DS cmds: [POPIO] Flag set\n", sim_deb);
            break;


        case ioCRS:                                         /* control reset */
            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fputs (">>DS cmds: [CRS] Master reset\n", sim_deb);

            ds.control = CLEAR;                             /* clear the control */
            ds.cmfol = CLEAR;                               /*   and command follows flip-flops */

            if (PRESET_ENABLE) {                            /* is preset enabled for this interface? */
                fifo_clear ();                              /* clear the FIFO */

                status = dl_clear_controller (&mac_cntlr,   /* do a hard clear of the controller */
                                              ds_unit, hard_clear);

                stat_data = IORETURN (status, 0);           /* return the status from the controller */
                }
            break;


        case ioCLC:                                     /* clear control flip-flop */
            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fprintf (sim_deb, ">>DS cmds: [CLC%s] Control cleared\n", hold_or_clear);

            ds.control = CLEAR;                         /* clear the control */
            ds.edt = CLEAR;                             /*   and EDT flip-flops */
            ds.cmfol = SET;                             /* set the command follows flip-flop */
            mac_cntlr.eod = SET;                        /* set the controller's EOD flag */

            fifo_clear ();                              /* clear the FIFO */
            break;


        case ioSTC:                                     /* set control flip-flop */
            ds.control = SET;                           /* set the control flip-flop */

            interrupt_enabled = TRUE;                   /* check for drive attention */

            if (DEBUG_PRI (ds_dev, DEB_CMDS))
                fprintf (sim_deb, ">>DS cmds: [STC%s] Control set\n", hold_or_clear);
            break;


        case ioEDT:                                     /* end data transfer */
            ds.edt = SET;                               /* set the EDT flip-flop */

            if (DEBUG_PRI (ds_dev, DEB_CPU))
                fputs (">>DS cpu:  [EDT] DCPC transfer ended\n", sim_deb);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (ds);                             /* set the standard PRL signal */
            setstdIRQ (ds);                             /* set the standard IRQ signal */
            setSRQ (dibptr->select_code, ds.srq);       /* set the SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            ds.flagbuf = CLEAR;                         /* clear the flag */
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove the current signal from the set */
    }


if (command_issued)                                     /* was a command received? */
    poll_interface ();                                  /* poll the interface for the next command */
else if (interrupt_enabled)                             /* were interrupts enabled? */
    poll_drives ();                                     /* poll the drives for Attention */

return stat_data;
}


/* Service the disc drive unit.

   The unit service routine is called to execute scheduled controller commands
   for the specified unit.  The actions to be taken depend on the current state
   of the controller and the unit.

   Generally, the controller library service routine handles all of the disc
   operations except data transfer to and from the interface.  Read transfers
   are responsible for loading words from the sector buffer into the FIFO and
   enabling SRQ.  If the current sector transfer is complete, either due to EDT
   assertion or buffer exhaustion, the controller is moved to the end phase to
   complete or continue the read with the next sector.  In either case, the unit
   is rescheduled.  If the FIFO overflows, the read terminates with a data
   overrun error.

   Write transfers set the initial SRQ to request words from the CPU.  As each
   word arrives, it is unloaded from the FIFO into the sector buffer, and SRQ is
   enabled.  If the current sector transfer is complete, the controller is moved
   to the end phase.  If the FIFO underflows, the write terminates with a data
   overrun error.

   The synchronous nature of the disc drive requires that data be supplied or
   accepted continuously by the CPU.  DCPC generally assures that this occurs,
   and the FIFO allows for some latency before an overrun or underrun occurs.

   The other operation the interface must handle is seek completion.  The
   controller handles seek completion by setting Attention status in the drive's
   status word.  The interface is responsible for polling the drives if the
   controller is idle and interrupts are enabled.


   Implementation notes:

    1. Every command except Seek, Recalibrate, and End sets the flag when the
       command completes.  A command completes when the controller is no longer
       busy (it becomes idle for Seek, Recalibrate, and End, or it becomes
       waiting for all others).  Seek and Recalibrate may generate errors (e.g.,
       heads unloaded), in which case the flag must be set.  But in these cases,
       the controller state is waiting, not idle.

       However, it is insufficient simply to check that the controller has moved
       to the wait state, because a seek may complete while the controller is
       waiting for the next command.  For example, a Seek is started on unit 0,
       and the controller moves to the idle state.  But before the seek
       completes, another command is issued that attempts to access unit 1,
       which is not ready.  The command fails with a Status-2 error, and the
       controller moves to the wait state.  When the seek completes, the
       controller is waiting with error status.  We must determine whether the
       seek completed successfully or not, as we must interrupt in the latter
       case.

       Therefore, we determine seek completion by checking if the Attention
       status was set.  Attention sets only if the seek completes successfully.

       (Actually, Attention sets if a seek check occurs, but in that case, the
       command terminated before the seek ever started.  Also, a seek may
       complete while the controller is busy, waiting, or idle.)

    2. For debug printouts, we want to print the name of the command that has
       completed when the controller returns to the idle or wait state.
       Normally, we would use the controller's "opcode" field to identify the
       command that completed.  However, while waiting for Seek or Recalibrate
       completion, "opcode" may be set to another command if that command does
       not access this drive.  For example, it might be set to a Read of another
       unit, or a Request Status for this unit.  So we can't rely on "opcode" to
       report the correct name of the completed positioning command.

       However, we cannot rely on "uptr->OP" either, as that can be changed
       during the course of a command.  For example, Read Without Verify is
       changed to Read after a track crossing.

       Instead, we have to determine whether a seek is completing.  If it is,
       then we report "uptr->OP"; otherwise, we report "opcode".

    3. The initial write SRQ must set only at the transition from the start
       phase to the data phase.  If a write command begins with an auto-seek,
       the drive service will be entered twice in the start phase (the first
       entry performs the seek, and the second begins the write).  In hardware,
       SRQ does not assert until the write begins.

    4. The DCPC EDT signal cannot set the controller's end-of-data flag
       directly because a write EOD must only occur after the FIFO has been
       drained.
*/

t_stat ds_service_drive (UNIT *uptr)
{
static const char completion_message [] = ">>DS rwsc: Unit %d %s command completed\n";
t_stat result;
t_bool seek_completion;
int32 unit;
FLIP_FLOP entry_srq = ds.srq;                           /* get the SRQ state on entry */
CNTLR_PHASE entry_phase = (CNTLR_PHASE) uptr->PHASE;    /* get the operation phase on entry */
uint32 entry_status = uptr->STAT;                       /* get the drive status on entry */

result = dl_service_drive (&mac_cntlr, uptr);           /* service the drive */

if ((CNTLR_PHASE) uptr->PHASE == data_phase)            /* is the drive in the data phase? */
    switch ((CNTLR_OPCODE) uptr->OP) {                  /* dispatch the current operation */

        case Read:                                      /* read operations */
        case Read_Full_Sector:
        case Read_With_Offset:
        case Read_Without_Verify:
            if (mac_cntlr.length == 0 || ds.edt == SET) {   /* is the data phase complete? */
                mac_cntlr.eod = ds.edt;                     /* set EOD if DCPC is done */
                uptr->PHASE = end_phase;                    /* set the end phase */
                uptr->wait = mac_cntlr.cmd_time;            /*   and schedule the controller */
                }

            else if (FIFO_FULL)                             /* is the FIFO already full? */
                dl_end_command (&mac_cntlr, data_overrun);  /* terminate the command with an overrun */

            else {
                fifo_load (buffer [mac_cntlr.index++]);     /* load the next word into the FIFO */
                mac_cntlr.length--;                         /* count it */
                ds.srq = SET;                               /* ask DCPC to pick it up */
                ds_io (&ds_dib, ioSIR, 0);                  /*   and recalculate the interrupts */
                uptr->wait = mac_cntlr.data_time;           /* schedule the next data transfer */
                }

            break;


        case Write:                                     /* write operations */
        case Write_Full_Sector:
        case Initialize:
            if (entry_phase == start_phase) {           /* is this the phase transition? */
                ds.srq = SET;                           /* start the DCPC transfer */
                ds_io (&ds_dib, ioSIR, 0);              /*   and recalculate the interrupts */
                }

            else if (FIFO_EMPTY)                            /* is the FIFO empty? */
                dl_end_command (&mac_cntlr, data_overrun);  /* terminate the command with an underrun */

            else {
                buffer [mac_cntlr.index++] = fifo_unload ();    /* unload the next word from the FIFO */
                mac_cntlr.length--;                             /* count it */

                if (ds.edt == SET && FIFO_EMPTY)                /* if DCPC is complete and the FIFO is empty */
                    mac_cntlr.eod = SET;                        /*   then set the end-of-data flag */

                if (mac_cntlr.length == 0 || mac_cntlr.eod == SET) {    /* is the data phase complete? */
                    uptr->PHASE = end_phase;                            /* set the end phase */
                    uptr->wait = mac_cntlr.cmd_time;                    /*   and schedule the controller */
                    }

                else {
                    if (ds.edt == CLEAR) {              /* if DCPC is still transferring */
                        ds.srq = SET;                   /*   then request the next word */
                        ds_io (&ds_dib, ioSIR, 0);      /*   and recalculate the interrupts */
                        }

                    uptr->wait = mac_cntlr.data_time;   /* schedule the next data transfer */
                    }
                }

            break;


        default:                                        /* we were entered with an invalid state */
            result = SCPE_IERR;                         /* return an internal (programming) error */
            break;
        }                                               /* end of data phase operation dispatch */


if (DEBUG_PRI (ds_dev, DEB_CMDS) && entry_srq != ds.srq)
    fprintf (sim_deb, ">>DS cmds: SRQ %s\n", ds.srq == SET ? "set" : "cleared");


if (uptr->wait)                                             /* was service requested? */
    activate_unit (uptr);                                   /* schedule the next event */

seek_completion = ~entry_status & uptr->STAT & DL_S2ATN;    /* seek is complete when Attention sets */

if (mac_cntlr.state != cntlr_busy) {                        /* is the command complete? */
    if (mac_cntlr.state == cntlr_wait && !seek_completion)  /* is it command but not seek completion? */
        ds_io (&ds_dib, ioENF, 0);                          /* set the data flag to interrupt the CPU */

    poll_interface ();                                      /* poll the interface for the next command */
    poll_drives ();                                         /* poll the drives for Attention */
    }


if (DEBUG_PRI (ds_dev, DEB_RWSC)) {
    unit = uptr - ds_unit;                                  /* get the unit number */

    if (result == SCPE_IERR)                                /* did an internal error occur? */
        fprintf (sim_deb, ">>DS rwsc: Unit %d %s command %s phase service not handled\n",
                 unit, dl_opcode_name (MAC, (CNTLR_OPCODE) uptr->OP),
                 dl_phase_name ((CNTLR_PHASE) uptr->PHASE));

    else if (seek_completion)                               /* if a seek has completed */
        fprintf (sim_deb, completion_message,               /*   report the unit command */
                 unit, dl_opcode_name (MAC, (CNTLR_OPCODE) uptr->OP));

    else if (mac_cntlr.state == cntlr_wait)                 /* if the controller has stopped */
        fprintf (sim_deb, completion_message,               /*   report the controller command */
                 unit, dl_opcode_name (MAC, mac_cntlr.opcode));
    }

return result;                                              /* return the result of the service */
}


/* Service the controller unit.

   The controller service routine is called to execute scheduled controller
   commands that do not access drive units.  It is also called to obtain command
   parameters from the interface and to return command result values to the
   interface.

   Most controller commands are handled completely in the library's service
   routine, so we call that first.  Commands that neither accept nor supply
   parameters are complete when the library routine returns, so all we have to
   do is set the interface flag if required.

   For parameter transfers in the data phase, the interface is responsible for
   moving words between the sector buffer and the FIFO and setting the flag to
   notify the CPU.


   Implementation notes:

    1. In hardware, the Read With Offset command sets the data flag after the
       offset parameter has been read and the head positioner has been moved by
       the indicated amount.  The intent is to delay the DCPC start until the
       drive is ready to supply data from the disc.

       In simulation, the flag is set as soon as the parameter is received.
*/

t_stat ds_service_controller (UNIT *uptr)
{
t_stat result;
const CNTLR_OPCODE opcode = (CNTLR_OPCODE) uptr->OP;

result = dl_service_controller (&mac_cntlr, uptr);      /* service the controller */

switch ((CNTLR_PHASE) uptr->PHASE) {                    /* dispatch the current phase */

    case start_phase:                                   /* most controller operations */
    case end_phase:                                     /*   start and end on the same phase */
        switch (opcode) {                               /* dispatch the current operation */

            case Request_Status:
            case Request_Sector_Address:
            case Address_Record:
            case Request_Syndrome:
            case Load_TIO_Register:
            case Request_Disc_Address:
            case End:
                break;                                  /* complete the operation without setting the flag */


            case Clear:
            case Set_File_Mask:
            case Wakeup:
                ds_io (&ds_dib, ioENF, 0);              /* complete the operation and set the flag */
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of start and end phase handlers */


    case data_phase:
        switch (opcode) {                               /* dispatch the current operation */

            case Seek:                                  /* operations that accept parameters */
            case Verify:
            case Address_Record:
            case Read_With_Offset:
            case Load_TIO_Register:
                buffer [mac_cntlr.index++] = fifo_unload ();    /* unload the next word from the FIFO */
                mac_cntlr.length--;                             /* count it */

                if (mac_cntlr.length)                           /* are there more words to transfer? */
                    ds_io (&ds_dib, ioENF, 0);                  /* set the flag to request the next one */

                else {                                          /* all parameters have been received */
                    uptr->PHASE = end_phase;                    /* set the end phase */

                    if (opcode == Read_With_Offset)             /* a Read With Offset command sets the flag */
                        ds_io (&ds_dib, ioENF, 0);              /*   to indicate that offsetting is complete */

                    start_command ();                           /* the command is now ready to execute */
                    }
                break;


            case Request_Status:                        /* operations that supply parameters */
            case Request_Sector_Address:
            case Request_Syndrome:
            case Request_Disc_Address:
                if (mac_cntlr.length) {                         /* are there more words to return? */
                    fifo_load (buffer [mac_cntlr.index++]);     /* load the next word into the FIFO */
                    mac_cntlr.length--;                         /* count it */

                    ds_io (&ds_dib, ioENF, 0);                  /* set the flag to request pickup by the CPU */
                    }

                else {                                  /* all parameters have been sent */
                    uptr->PHASE = end_phase;            /* set the end phase */
                    uptr->wait = mac_cntlr.cmd_time;    /* schedule the controller */
                    activate_unit (uptr);               /*   to complete the command */
                    }
                break;


            default:                                    /* we were entered with an invalid state */
                result = SCPE_IERR;                     /* return an internal (programming) error */
                break;
            }                                           /* end of operation dispatch */
        break;                                          /* end of data phase handlers */
    }                                                   /* end of phase dispatch */


if (result == SCPE_IERR && DEBUG_PRI (ds_dev, DEB_RWSC))    /* did an internal error occur? */
    fprintf (sim_deb, ">>DS rwsc: Controller %s command %s phase service not handled\n",
             dl_opcode_name (MAC, opcode), dl_phase_name ((CNTLR_PHASE) uptr->PHASE));


if (mac_cntlr.state != cntlr_busy) {                    /* has the controller stopped? */
    poll_interface ();                                  /* poll the interface for the next command */
    poll_drives ();                                     /* poll the drives for Attention status */

    if (DEBUG_PRI (ds_dev, DEB_RWSC))
        fprintf (sim_deb, ">>DS rwsc: Controller %s command completed\n",
                 dl_opcode_name (MAC, opcode));
    }

return result;                                          /* return the result of the service */
}


/* Service the command wait timer unit.

   The command wait timer service routine is called if the command wait timer
   expires.  The library is called to reset the file mask and idle the
   controller.  Then the interface is polled for a command and the drives are
   polled for Attention status.
*/

t_stat ds_service_timer (UNIT *uptr)
{
t_stat result;

result = dl_service_timer (&mac_cntlr, uptr);           /* service the timer */

poll_interface ();                                      /* poll the interface for the next command */
poll_drives ();                                         /* poll the drives for Attention status */

return result;                                          /* return the result of the service */
}


/* Reset the simulator.

   In hardware, the PON signal clears the Interface Selected flip-flop,
   disconnecting the interface from the disc controller.  In simulation, the
   interface always remains connected to the controller, so no special action is
   needed.


   Implementation notes:

    1. During a power-on reset, a pointer to the FIFO simulation register is
       saved to allow access to the "qptr" field during FIFO loading and
       unloading.  This enables SCP to view the FIFO as a circular queue, so
       that the bottom word of the FIFO is always displayed as FIFO[0],
       regardless of where it is in the actual FIFO array.

    2. SRQ is denied because neither IFIN nor IFOUT is asserted when the
       interface is not selected.
*/

t_stat ds_reset (DEVICE *dptr)
{
uint32 unit;

if (sim_switches & SWMASK ('P')) {                      /* is this a power-on reset? */
    ds.fifo_reg = find_reg ("FIFO", NULL, dptr);        /* find the FIFO register entry */

    if (ds.fifo_reg == NULL)                            /* if it cannot be found, */
        return SCPE_IERR;                               /*   report a programming error */

    else {                                              /* found it */
        ds.fifo_reg->qptr = 0;                          /*   so reset the FIFO bottom index */
        ds.fifo_count = 0;                              /*   and clear the FIFO */
        }

    for (unit = 0; unit < dptr->numunits; unit++) {     /* loop through all of the units */
        sim_cancel (dptr->units + unit);                /* cancel activation */
        dptr->units [unit].CYL = 0;                     /* reset the head position to cylinder 0 */
        dptr->units [unit].pos = 0;                     /* (irrelevant for the controller and timer) */
        }
    }

IOPRESET (&ds_dib);                                     /* PRESET the device */
ds.srq = CLEAR;                                         /* clear SRQ */

return SCPE_OK;
}


/* Attach a drive unit.

   The specified file is attached to the indicated drive unit.  The library
   attach routine will load the heads.  This will set the First Status and
   Attention bits in the drive status, so we poll the drives to ensure that the
   CPU is notified that the drive is now online.


   Implementation notes:

    1. If we are called during a RESTORE command, the drive status will not be
       changed, so polling the drives will have no effect.
*/

t_stat ds_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = dl_attach (&mac_cntlr, uptr, cptr);            /* attach the drive */

if (result == SCPE_OK)                                  /* was the attach successful? */
    poll_drives ();                                     /* poll the drives to notify the CPU */

return result;
}


/* Detach a drive unit.

   The specified file is detached from the indicated drive unit.  The library
   detach routine will unload the heads.  This will set the Attention bit in the
   drive status, so we poll the drives to ensure that the CPU is notified that
   the drive is now offline.
*/

t_stat ds_detach (UNIT *uptr)
{
t_stat result;

result = dl_detach (&mac_cntlr, uptr);                  /* detach the drive */

if (result == SCPE_OK)                                  /* was the detach successful? */
    poll_drives ();                                     /* poll the drives to notify the CPU */

return result;
}


/* Boot a MAC disc drive.

   The MAC disc bootstrap program is loaded from the HP 12992B Boot Loader ROM
   into memory, the I/O instructions are configured for the interface card's
   select code, and the program is run to boot from the specified unit.  The
   loader supports booting from cylinder 0 of drive unit 0 only.  Before
   execution, the S register is automatically set as follows:

     15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     ------  ------  ----------------------   ---------   ---------
     ROM #    0   1       select code         reserved      head

   The boot routine sets bits 15-6 of the S register to appropriate values.
   Bits 5-3 and 1-0 retain their original values, so S should be set before
   booting.  These bits are typically set to 0, although bit 5 is set for an RTE
   reconfiguration boot, and bits 1-0 may be set if booting from a head other
   than 0 is desired.


   Implementation notes:

    1. The Loader ROMs manual indicates that bits 2-0 select the head to use,
       implying that heads 0-7 are valid.  However, Table 5 has entries only for
       heads 0-3, and the boot loader code will malfunction if heads 4-7 are
       specified.  The code masks the head number to three bits but forms the
       Cold Load Read command by shifting the head number six bits to the left.
       As the head field in the command is only two bits wide, specifying heads
       4-7 will result in bit 2 being shifted into the opcode field, resulting
       in a Recalibrate command.
*/


const BOOT_ROM ds_rom = {
    0017727,                    /* START JSB STAT      GET STATUS */
    0002021,                    /*       SSA,RSS       IS DRIVE READY ? */
    0027742,                    /*       JMP DMA         YES, SET UP DMA */
    0013714,                    /*       AND B20         NO, CHECK STATUS BITS */
    0002002,                    /*       SZA           IS DRIVE FAULTY OR HARD DOWN ? */
    0102030,                    /*       HLT 30B         YES, HALT 30B, "RUN" TO TRY AGAIN */
    0027700,                    /*       JMP START       NO, TRY AGAIN FOR DISC READY */
    0102011,                    /* ADDR1 OCT 102011    */
    0102055,                    /* ADDR2 OCT 102055    */
    0164000,                    /* CNT   DEC -6144     */
    0000007,                    /* D7    OCT 7         */
    0001400,                    /* STCMD OCT 1400      */
    0000020,                    /* B20   OCT 20        */
    0017400,                    /* STMSK OCT 17400     */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /*       NOP           */
    0000000,                    /* STAT  NOP           STATUS CHECK SUBROUTINE */
    0107710,                    /*       CLC DC,C      SET STATUS COMMAND MODE */
    0063713,                    /*       LDA STCMD     GET STATUS COMMAND */
    0102610,                    /*       OTA DC        OUTPUT STATUS COMMAND */
    0102310,                    /*       SFS DC        WAIT FOR STATUS#1 WORD */
    0027733,                    /*       JMP *-1       */
    0107510,                    /*       LIB DC,C         B-REG = STATUS#1 WORD */
    0102310,                    /*       SFS DC         WAIT FOR STATUS#2 WORD */
    0027736,                    /*       JMP *-1       */
    0103510,                    /*       LIA DC,C         A-REG = STATUS#2 WORD */
    0127727,                    /*       JMP STAT,I    RETURN */
    0067776,                    /* DMA   LDB DMACW     GET DMA CONTROL WORD */
    0106606,                    /*       OTB 6         OUTPUT DMA CONTROL WORD */
    0067707,                    /*       LDB ADDR1     GET MEMORY ADDRESS */
    0106702,                    /*       CLC 2         SET MEMORY ADDRESS INPUT MODE */
    0106602,                    /*       OTB 2         OUTPUT MEMORY ADDRESS TO DMA */
    0102702,                    /*       STC 2         SET WORD COUNT INPUT MODE */
    0067711,                    /*       LDB CNT       GET WORD COUNT */
    0106602,                    /*       OTB 2         OUTPUT WORD COUNT TO DMA */
    0106710,                    /* CLDLD CLC DC        SET COMMAND INPUT MODE */
    0102501,                    /*       LIA 1         LOAD SWITCH */
    0106501,                    /*       LIB 1         REGISTER SETTINGS */
    0013712,                    /*       AND D7        ISOLATE HEAD NUMBER */
    0005750,                    /*       BLF,CLE,SLB   BIT 12=0? */
    0027762,                    /*       JMP *+3       NO,MANUAL BOOT */
    0002002,                    /*       SZA           YES,RPL BOOT. HEAD#=0? */
    0001000,                    /*       ALS           NO,HEAD#1, MAKE HEAD#=2 */
    0001720,                    /*       ALF,ALS       FORM COLD LOAD */
    0001000,                    /*       ALS           COMMAND WORD */
    0103706,                    /*       STC 6,C       ACTIVATE DMA */
    0103610,                    /*       OTA DC,C      OUTPUT COLD LOAD COMMAND */
    0102310,                    /*       SFS DC        IS COLD LOAD COMPLETED ? */
    0027766,                    /*       JMP *-1         NO, WAIT */
    0017727,                    /*       JSB STAT        YES, GET STATUS */
    0060001,                    /*       LDA 1         */
    0013715,                    /*       AND STMSK     A-REG = STATUS BITS OF STATUS#1 WD */
    0002002,                    /*       SZA           IS TRANSFER OK ? */
    0027700,                    /*       JMP START       NO,TRY AGAIN */
    0117710,                    /* EXIT  JSB ADDR2,I     YES, EXEC LOADED PROGRAM _@ 2055B */
    0000010,                    /* DMACW ABS DC        */
    0170100,                    /*       ABS -START    */
    };

t_stat ds_boot (int32 unitno, DEVICE *dptr)
{
if (unitno != 0)                                        /* boot supported on drive unit 0 only */
    return SCPE_NOFNC;                                  /* report "Command not allowed" if attempted */

if (ibl_copy (ds_rom, ds_dib.select_code,               /* copy the boot ROM to memory and configure */
              IBL_OPT | IBL_DS_HEAD,                    /*   the S register accordingly */
              IBL_DS | IBL_MAN | IBL_SET_SC (ds_dib.select_code)))
    return SCPE_IERR;                                   /* return an internal error if the copy failed */
else
    return SCPE_OK;
}



/* MAC disc global SCP routines */


/* Load or unload the drive heads.

   The SCP command SET DSn UNLOADED simulates setting the hardware RUN/STOP
   switch to STOP.  The heads are unloaded, and the drive is spun down.

   The SET DSn LOADED command simulates setting the switch to RUN.  The drive is
   spun up, and the heads are loaded.

   The library handles command validation and setting the appropriate drive unit
   status.
*/

t_stat ds_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const t_bool load = (value != UNIT_UNLOAD);             /* true if the heads are loading */

return dl_load_unload (&mac_cntlr, uptr, load);         /* load or unload the heads */
}



/* MAC disc local utility routines */


/* Start a command.

   The previously prepared command is executed by calling the corresponding
   library routine.  On entry, the controller's opcode field contains the
   command to start, and the buffer contains the command word in element 0 and
   the parameters required by the command, if any, beginning in element 1.

   If the command started, the returned pointer will point at the unit to
   activate (if that unit's "wait" field is non-zero).  If the returned pointer
   is NULL, the command failed to start, and the controller status has been set
   to indicate the reason.  The interface flag is set to notify the CPU of the
   failure.


   Implementation notes:

    1. If a command that accesses the drive is attempted on a drive currently
       seeking, the returned pointer will be valid, but the unit's "wait" time
       will be zero.  The unit must not be activated (as it already is active).
       When the seek completes, the command will be executed automatically.

       If a Seek or Cold Load Read command is attempted on a drive currently
       seeking, seek completion will occur normally, but Seek Check status will
       be set.

    2. For debug printouts, we want to print the name of the command (Seek or
       Recalibrate) in progress when a new command is started.  However, when
       the library routine returns, the unit operation and controller opcode
       have been changed to reflect the new command.  Therefore, we must record
       the operation in progress before calling the library.

       The problem is in determining which unit's operation code to record.  We
       cannot blindly use the unit field from the new command, as recorded in
       the controller, as preparation has ensured only that the target unit
       number is legal but not necessarily valid.  Therefore, we must validate
       the unit number before accessing the unit's operation code.

       If the unit number is invalid, the command will not start, but the
       compiler does not know this.  Therefore, we must ensure that the saved
       operation code is initialized, or a "variable used uninitialized" warning
       will occur.
*/

static void start_command (void)
{
int32 unit, time;
UNIT *uptr;
CNTLR_OPCODE drive_command;

unit = GET_S1UNIT (mac_cntlr.spd_unit);                 /* get the (prepared) unit from the command */

if (unit <= DL_MAXDRIVE)                                /* is the unit number valid? */
    drive_command = (CNTLR_OPCODE) ds_unit [unit].OP;   /* get the opcode from the unit that will be used */
else                                                    /* the unit is invalid, so the command will not start */
    drive_command = End;                                /*   but the compiler doesn't know this! */

uptr = dl_start_command (&mac_cntlr, ds_unit, DL_MAXDRIVE); /* ask the controller to start the command */

if (uptr) {                                             /* did the command start? */
    time = uptr->wait;                                  /* save the activation time */

    if (time)                                           /* was the unit scheduled? */
        activate_unit (uptr);                           /* activate it (and clear the "wait" field) */

    if (DEBUG_PRI (ds_dev, DEB_RWSC)) {
        unit = uptr - ds_unit;                          /* get the unit number */

        if (time == 0)                                  /* was the unit busy? */
            fprintf (sim_deb, ">>DS rwsc: Unit %d %s in progress\n",
                     unit, dl_opcode_name (MAC, drive_command));

        fputs (">>DS rwsc: ", sim_deb);

        if (unit > DL_MAXDRIVE)
            fputs ("Controller ", sim_deb);
        else
            fprintf (sim_deb, "Unit %d position %" T_ADDR_FMT "d ", unit, uptr->pos);

        fprintf (sim_deb, "%s command initiated\n",
                 dl_opcode_name (MAC, mac_cntlr.opcode));
        }
    }

else                                                    /* the command failed to start */
    ds_io (&ds_dib, ioENF, 0);                          /*   so set the flag to notify the CPU */

return;
}


/* Poll the interface for a new command.

   If a new command is available, and the controller is not busy, prepare the
   command for execution.  If preparation succeeded, and the command needs
   parameters before executing, set the flag to request the first one from the
   CPU.  If no parameters are needed, the command is ready to execute.

   If preparation failed, set the flag to notify the CPU.  The controller
   status contains the reason for the failure.
*/

static void poll_interface (void)
{
if (ds.cmrdy == SET && mac_cntlr.state != cntlr_busy) {     /* are the interface and controller ready? */
    buffer [0] = fifo_unload ();                            /* unload the command into the buffer */

    if (dl_prepare_command (&mac_cntlr, ds_unit, DL_MAXDRIVE)) {    /* prepare the command; did it succeed? */
        if (mac_cntlr.length)                                       /* does the command require parameters? */
            ds_io (&ds_dib, ioENF, 0);                              /* set the flag to request the first one */
        else                                                        /* if not waiting for parameters */
            start_command ();                                       /*   start the command */
        }

    else                                                /* preparation failed */
        ds_io (&ds_dib, ioENF, 0);                      /*   so set the flag to notify the CPU */

    ds.cmrdy = CLEAR;                                   /* flush the command from the interface */
    }

return;
}


/* Poll the drives for attention requests.

   If the controller is idle and interrupts are allowed, the drives are polled
   to see if any drive is requesting attention.  If one is found, the controller
   resets that drive's Attention status, saves the drive's unit number, sets
   Drive Attention status, and waits for a command from the CPU.  The interface
   sets the flag to notify the CPU.
*/

static void poll_drives (void)
{
if (mac_cntlr.state == cntlr_idle && ds.control == SET)     /* is the controller idle and interrupts are allowed? */
    if (dl_poll_drives (&mac_cntlr, ds_unit, DL_MAXDRIVE))  /* poll the drives; was Attention seen? */
        ds_io (&ds_dib, ioENF, 0);                          /* request an interrupt */
return;
}


/* Load a word into the FIFO.

   A word is loaded into the next available location in the FIFO, and the FIFO
   occupancy count is incremented.  If the FIFO is full on entry, the load is
   ignored.


   Implementation notes:

    1. The FIFO is implemented as circular queue to take advantage of REG_CIRC
       EXAMINE semantics.  REG->qptr is the index of the first word currently in
       the FIFO.  By specifying REG_CIRC, examining FIFO[0-n] will always
       display the words in load order, regardless of the actual array index of
       the start of the list.  The number of words currently present in the FIFO
       is kept in fifo_count (0 = empty, 1-16 = number of words available).

       If fifo_count < FIFO_SIZE, (REG->qptr + fifo_count) mod FIFO_SIZE is the
       index of the new word location.  Loading stores the word there and then
       increments fifo_count.

    2. Because the load and unload routines need access to qptr in the REG
       structure for the FIFO array, a pointer to the REG is stored in the
       fifo_reg variable during device reset.
*/

static void fifo_load (uint16 data)
{
uint32 index;

if (FIFO_FULL) {                                            /* is the FIFO already full? */
    if (DEBUG_PRI (ds_dev, DEB_BUF))
        fprintf (sim_deb, ">>DS buf:  Attempted load to full FIFO, data %06o\n", data);

    return;                                                 /* return with the load ignored */
    }

index = (ds.fifo_reg->qptr + ds.fifo_count) % FIFO_SIZE;    /* calculate the index of the next available location */

ds.fifo [index] = data;                                     /* store the word in the FIFO */
ds.fifo_count = ds.fifo_count + 1;                          /* increment the count of words stored */

if (DEBUG_PRI (ds_dev, DEB_BUF))
    fprintf (sim_deb, ">>DS buf:  Data %06o loaded into FIFO (%d)\n",
             data, ds.fifo_count);

return;
}


/* Unload a word from the FIFO.

   A word is unloaded from the first location in the FIFO, and the FIFO
   occupancy count is decremented.  If the FIFO is empty on entry, the unload
   returns dummy data.


   Implementation notes:

    1. If fifo_count > 0, REG->qptr is the index of the word to remove.  Removal
       gets the word and then increments qptr (mod FIFO_SIZE) and decrements
       fifo_count.
*/

static uint16 fifo_unload (void)
{
uint16 data;

if (FIFO_EMPTY) {                                           /* is the FIFO already empty? */
    if (DEBUG_PRI (ds_dev, DEB_BUF))
        fputs (">>DS buf:  Attempted unload from empty FIFO\n", sim_deb);

    return 0;                                               /* return with no data */
    }

data = ds.fifo [ds.fifo_reg->qptr];                         /* get the word from the FIFO */

ds.fifo_reg->qptr = (ds.fifo_reg->qptr + 1) % FIFO_SIZE;    /* update the FIFO queue pointer */
ds.fifo_count = ds.fifo_count - 1;                          /* decrement the count of words stored */

if (DEBUG_PRI (ds_dev, DEB_BUF))
    fprintf (sim_deb, ">>DS buf:  Data %06o unloaded from FIFO (%d)\n",
             data, ds.fifo_count);

return data;
}


/* Clear the FIFO.

   The FIFO is cleared by setting the occupancy counter to zero.
*/

static void fifo_clear (void)
{
ds.fifo_count = 0;                                      /* clear the FIFO */

if (DEBUG_PRI (ds_dev, DEB_BUF))
    fputs (">>DS buf:  FIFO cleared\n", sim_deb);

return;
}


/* Activate the unit.

   The specified unit is activated using the unit's "wait" time.  If debugging
   is enabled, the activation is logged to the debug file.
*/

static t_stat activate_unit (UNIT *uptr)
{
int32 unit;
t_stat result;

if (DEBUG_PRI (ds_dev, DEB_SERV)) {
    unit = uptr - ds_unit;                              /* calculate the unit number */

    if (uptr == &ds_cntlr)
        fprintf (sim_deb, ">>DS serv: Controller delay %d service scheduled\n",
                 uptr->wait);
    else
        fprintf (sim_deb, ">>DS serv: Unit %d delay %d service scheduled\n",
                 unit, uptr->wait);
    }

result = sim_activate (uptr, uptr->wait);               /* activate the unit */
uptr->wait = 0;                                         /* reset the activation time */

return result;                                          /* return the activation status */
}
