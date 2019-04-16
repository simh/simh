/* hp2100_dr.c: HP 2100 12606B/12610B fixed head disk/drum simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

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

   DR           12606B 2770/2771 fixed head disk
                12610B 2773/2774/2775 drum

   11-Jul-18    JDB     Revised I/O model
   27-Feb-18    JDB     Added the BBDL, reworked drc_boot to use cpu_copy_loader
   21-Feb-18    JDB     ATTACH -N now creates a full-size disc image
   19-Jul-17    JDB     Removed "dr_stopioe" variable and register
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   10-Nov-16    JDB     Modified the drc_boot routine to use the BBDL
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   09-Jul-08    JDB     Revised drc_boot to use ibl_copy
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   07-Oct-04    JDB     Fixed enable/disable from either device
                        Fixed sector return in status word
                        Provided protected tracks and "Writing Enabled" status bit
                        Fixed DMA last word write, incomplete sector fill value
                        Added "parity error" status return on writes for 12606
                        Added track origin test for 12606
                        Added SCP test for 12606
                        Fixed 12610 SFC operation
                        Added "Sector Flag" status bit
                        Added "Read Inhibit" status bit for 12606
                        Fixed current-sector determination
                        Added PROTECTED, UNPROTECTED, TRACKPROT modifiers
   26-Aug-04    RMS     Fixed CLC to stop operation (from Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Revised boot rom to use IBL algorithm
                        Implemented DMA SRQ (follows FLG)
   27-Jul-03    RMS     Fixed drum sizes
                        Fixed variable capacity interaction with SAVE/RESTORE
   10-Nov-02    RMS     Added BOOT command

   References:
     - 12606B Disc Memory Interface Kit Operating and Service Manual
         (12606-90012, March 1970)
     - 12610B Drum Memory Interface Kit Operating and Service Manual
         (12610-9001, February 1970)


   These head-per-track devices are buffered in memory, to minimize overhead.

   The drum data channel does not have a command flip-flop.  Its control
   flip-flop is not wired into the interrupt chain; accordingly, the
   simulator uses command rather than control for the data channel.  Its
   flag does not respond to SFS, SFC, or STF.

   The drum control channel does not have any of the traditional flip-flops.

   The 12606 interface implements two diagnostic tests.  An SFS CC instruction
   will skip if the disk has passed the track origin (sector 0) since the last
   CLF CC instruction, and an SFC CC instruction will skip if the Sector Clock
   Phase (SCP) flip-flop is clear, indicating that the current sector is
   accessible.  The 12610 interface does not support these tests; the SKF signal
   is not driven, so neither SFC CC nor SFS CC will skip.

   The interface implements a track protect mechanism via a switch and a set of
   on-card diodes.  The switch sets the protected/unprotected status, and the
   particular diodes installed indicate the range of tracks (a power of 2) that
   are read-only in the protected mode.

   Somewhat unusually, writing to a protected track completes normally, but the
   data isn't actually written, as the write current is inhibited.  There is no
   "failure" status indication.  Instead, a program must note the lack of
   "Writing Enabled" status before the write is attempted.

   Specifications (2770/2771):
   - 90 sectors per logical track
   - 45 sectors per revolution
   - 64 words per sector
   - 2880 words per revolution
   - 3450 RPM = 17.4 ms/revolution
   - data timing = 6.0 us/word, 375 us/sector
   - inst timing = 4 inst/word, 11520 inst/revolution

   Specifications 2773/2774/2775:
   - 32 sectors per logical track
   - 32 sectors per revolution
   - 64 words per sector
   - 2048 words per revolution
   - 3450 RPM = 17.4 ms/revolution
   - data timing = 8.5 us/word, 550 us/sector
   - inst timing = 6 inst/word, 12288 inst/revolution
*/



#include <math.h>

#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Constants */

#define DR_NUMWD        64                              /* words/sector */
#define DR_FNUMSC       90                              /* fhd sec/track */
#define DR_DNUMSC       32                              /* drum sec/track */
#define DR_NUMSC        ((drc_unit [0].flags & UNIT_DRUM)? DR_DNUMSC: DR_FNUMSC)
#define DR_SIZE         (512 * DR_DNUMSC * DR_NUMWD)    /* initial size */
#define DR_FTIME        4                               /* fhd per-word time */
#define DR_DTIME        6                               /* drum per-word time */
#define DR_OVRHEAD      5                               /* overhead words at track start */
#define UNIT_V_PROT     (UNIT_V_UF + 0)                 /* track protect */
#define UNIT_V_SZ       (UNIT_V_UF + 1)                 /* disk vs drum */
#define UNIT_M_SZ       017                             /* size */
#define UNIT_PROT       (1 << UNIT_V_PROT)
#define UNIT_SZ         (UNIT_M_SZ << UNIT_V_SZ)
#define UNIT_DRUM       (1 << UNIT_V_SZ)                /* low order bit */
#define  SZ_180K        000                             /* disks */
#define  SZ_360K        002
#define  SZ_720K        004
#define  SZ_1024K       001                             /* drums: default size */
#define  SZ_1536K       003
#define  SZ_384K        005
#define  SZ_512K        007
#define  SZ_640K        011
#define  SZ_768K        013
#define  SZ_896K        015
#define DR_GETSZ(x)     (((x) >> UNIT_V_SZ) & UNIT_M_SZ)

/* Command word */

#define CW_WR           0100000                         /* write vs read */
#define CW_V_FTRK       7                               /* fhd track */
#define CW_M_FTRK       0177
#define CW_V_DTRK       5                               /* drum track */
#define CW_M_DTRK       01777
#define MAX_TRK         (((drc_unit [0].flags & UNIT_DRUM)? CW_M_DTRK: CW_M_FTRK) + 1)
#define CW_GETTRK(x)    ((drc_unit [0].flags & UNIT_DRUM)? \
                            (((x) >> CW_V_DTRK) & CW_M_DTRK): \
                            (((x) >> CW_V_FTRK) & CW_M_FTRK))
#define CW_PUTTRK(x)    ((drc_unit [0].flags & UNIT_DRUM)? \
                            (((x) & CW_M_DTRK) << CW_V_DTRK): \
                            (((x) & CW_M_FTRK) << CW_V_FTRK))
#define CW_V_FSEC       0                               /* fhd sector */
#define CW_M_FSEC       0177
#define CW_V_DSEC       0                               /* drum sector */
#define CW_M_DSEC       037
#define CW_GETSEC(x)    ((drc_unit [0].flags & UNIT_DRUM)? \
                            (((x) >> CW_V_DSEC) & CW_M_DSEC): \
                            (((x) >> CW_V_FSEC) & CW_M_FSEC))
#define CW_PUTSEC(x)    ((drc_unit [0].flags & UNIT_DRUM)? \
                            (((x) & CW_M_DSEC) << CW_V_DSEC): \
                            (((x) & CW_M_FSEC) << CW_V_FSEC))

/* Status register, ^ = dynamic */

#define DRS_V_NS        8                       /* ^next sector */
#define DRS_M_NS        0177
#define DRS_SEC         0100000                 /* ^sector flag */
#define DRS_RDY         0000200                 /* ^ready */
#define DRS_RIF         0000100                 /* ^read inhibit */
#define DRS_SAC         0000040                 /* sector coincidence */
#define DRS_ABO         0000010                 /* abort */
#define DRS_WEN         0000004                 /* ^write enabled */
#define DRS_PER         0000002                 /* parity error */
#define DRS_BSY         0000001                 /* ^busy */

#define CALC_SCP(x)     (((int32) fmod ((x) / (double) dr_time,  \
                        (double) (DR_NUMWD))) >= (DR_NUMWD - 3))

typedef struct {
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    } CARD_STATE;

static CARD_STATE drd;                          /* data per-card state */

static int32 drc_cw = 0;                        /* fnc, addr */
static int32 drc_sta = 0;                       /* status */
static int32 drc_run = 0;                       /* run flip-flop */

static int32 drd_ibuf = 0;                      /* input buffer */
static int32 drd_obuf = 0;                      /* output buffer */
static int32 drd_ptr = 0;                       /* sector pointer */
static int32 drc_pcount = 1;                    /* number of prot tracks */
static int32 dr_time = DR_DTIME;                /* time per word */

static int32 sz_tab [16] = {
     184320,                                    /*  180K  2770A     */
    1048576,                                    /* 1024K  2774A-002 */
     368640,                                    /*  360K  2771A     */
    1572864,                                    /* 1536K  2775A     */
     737280,                                    /*  720K  2771A-001 */
     393216,                                    /*  384K  2773A     */
          0,
     524288,                                    /*  512K  2773A-001 */
          0,
     655360,                                    /*  640K  2773A-002 */
          0,
     786432,                                    /*  768K  2774A     */
          0,
     917504,                                    /*  896K  2774A-001 */
          0,
          0
    };

/* Interface local SCP support routines */

static INTERFACE drd_interface;
static INTERFACE drc_interface;

static t_stat drc_svc (UNIT *uptr);
static t_stat drc_reset (DEVICE *dptr);
static t_stat drc_attach (UNIT *uptr, CONST char *cptr);
static t_stat drc_boot (int32 unitno, DEVICE *dptr);
static int32  dr_incda (int32 trk, int32 sec, int32 ptr);
static int32  dr_seccntr (double simtime);
static t_stat dr_set_prot (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat dr_show_prot (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat dr_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);


/* Device information blocks */

static DIB dr_dib [] = {
    { &drd_interface,                                                       /* the device's I/O interface function pointer */
      DRD,                                                                  /* the device's select code (02-77) */
      0,                                                                    /* the card index */
      "12606B Disc Memory/12610B Drum Memory Interface Data Channel",       /* the card description */
      NULL },                                                               /* the ROM description */

    { &drc_interface,                                                       /* the device's I/O interface function pointer */
      DRC,                                                                  /* the device's select code (02-77) */
      0,                                                                    /* the card index */
      "12606B Disc Memory/12610B Drum Memory Interface Command Channel",    /* the card description */
      NULL }                                                                /* the ROM description */
    };

#define drd_dib             dr_dib [0]
#define drc_dib             dr_dib [1]


/* Data card SCP data structures */


/* Unit list */

static UNIT drd_unit [] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (NULL,              0,         0)           },
    { UDATA (NULL,           UNIT_DIS,     0)           }
    };

#define TMR_ORG         0                       /* origin timer unit */
#define TMR_INH         1                       /* inhibit timer unit */


/* Register list */

static REG drd_reg [] = {
    { ORDATA (IBUF, drd_ibuf, 16) },
    { ORDATA (OBUF, drd_obuf, 16) },
    { FLDATA (CTL, drd.control, 0) },
    { FLDATA (FLG, drd.flag,    0) },
    { ORDATA (BPTR, drd_ptr, 6) },

      DIB_REGS (drd_dib),

    { NULL }
    };


/* Modifier list */

static MTAB drd_mod [] = {
/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor       */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &dr_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &dr_dib },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB drd_deb [] = {
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE drd_dev = {
    "DRD",                                      /* device name */
    drd_unit,                                   /* unit array */
    drd_reg,                                    /* register array */
    drd_mod,                                    /* modifier array */
    2,                                          /* number of units */
    0,                                          /* address radix */
    0,                                          /* address width */
    0,                                          /* address increment */
    0,                                          /* data radix */
    0,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &drc_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &drd_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    drd_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };


/* Command card SCP data structures */


/* Unit list */

#define UNIT_FLAGS          (UNIT_FIX | UNIT_ATTABLE | UNIT_BUFABLE | UNIT_MUSTBUF | UNIT_DRUM | UNIT_BINK)

static UNIT drc_unit [] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (&drc_svc,      UNIT_FLAGS, DR_SIZE)        },
    };


/* Register list */

static REG drc_reg[] = {
    { DRDATA (PCNT, drc_pcount, 10), REG_HIDDEN | PV_LEFT },
    { ORDATA (CW, drc_cw, 16) },
    { ORDATA (STA, drc_sta, 16) },
    { FLDATA (RUN, drc_run, 0) },
    { DRDATA (TIME, dr_time, 24), REG_NZ + PV_LEFT },
    { DRDATA (CAPAC, drc_unit [0].capac, 24), REG_HRO },

      DIB_REGS (drc_dib),

    { NULL }
    };


/* Modifier list */

static MTAB drc_mod [] = {
/*    Mask Value    Match Value            Print String    Match String    Validation         Display  Descriptor */
/*    ------------  ---------------------  --------------  --------------  -----------------  -------  ---------- */
    { UNIT_DRUM,    0,                     "disk",         NULL,           NULL,              NULL,    NULL       },
    { UNIT_DRUM,    UNIT_DRUM,             "drum",         NULL,           NULL,              NULL,    NULL       },
    { UNIT_SZ,      SZ_180K  << UNIT_V_SZ, NULL,           "180K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_360K  << UNIT_V_SZ, NULL,           "360K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_720K  << UNIT_V_SZ, NULL,           "720K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_384K  << UNIT_V_SZ, NULL,           "384K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_512K  << UNIT_V_SZ, NULL,           "512K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_640K  << UNIT_V_SZ, NULL,           "640K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_768K  << UNIT_V_SZ, NULL,           "768K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_896K  << UNIT_V_SZ, NULL,           "896K",         &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_1024K << UNIT_V_SZ, NULL,           "1024K",        &dr_set_size,      NULL,    NULL       },
    { UNIT_SZ,      SZ_1536K << UNIT_V_SZ, NULL,           "1536K",        &dr_set_size,      NULL,    NULL       },
    { UNIT_PROT,    UNIT_PROT,             "protected",    "PROTECTED",    NULL,              NULL,    NULL       },
    { UNIT_PROT,    0,                     "unprotected",  "UNPROTECTED",  NULL,              NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor       */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,               0,  "TRACKPROT",  "TRACKPROT",  &dr_set_prot, &dr_show_prot, NULL             },

    { MTAB_XDV,              2u,  "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &dr_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &dr_dib },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB drc_deb [] = {
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE drc_dev = {
    "DRC",                                      /* device name */
    drc_unit,                                   /* unit array */
    drc_reg,                                    /* register array */
    drc_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    21,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &drc_reset,                                 /* reset routine */
    &drc_boot,                                  /* boot routine */
    &drc_attach,                                /* attach routine */
    NULL,                                       /* detach routine */
    &drc_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    drc_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Data channel interface.

   The data channel card does not follow the usual interface I/O configuration.
   PRL is tied to PRH, so it is always valid.  The card does not drive IRQ, FLG,
   or SKF and does not respond to IAK.  SRQ is driven by the output of the flag
   flip-flop, which obeys CLF only.  There is no flag buffer.  The control
   flip-flop obeys STC and CLC.  Clearing control clears the flag flip-flop, and
   setting control sets the flag flip-flop if the interface is configured for
   writing.  On the 12606B, POPIO and CRS clear the track address register.

   Implementation notes:

    1. In response to CRS, the 12606B data channel clears only the track address
       register; the command channel clears the sector address register and the
       direction flip-flop.  Under simulation, all three form the control word,
       and as CRS is sent to all devices, we simply clear the control word here.
*/

static SIGNALS_VALUE drd_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
int32          t;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            drd.flag = CLEAR;                           /* clear the flag flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            drd.flag = SET;                             /* set the flag flip-flop */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = drd_ibuf;                  /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            drd_obuf = inbound_value;                   /* clear supplied status */
            break;


        case ioCRS:                                     /* Control Reset */
            if (!(drc_unit [0].flags & UNIT_DRUM))      /* 12606B? */
                drc_cw = 0;                             /* clear control word */

        /* fall through into the CLC handler */

        case ioCLC:                                     /* Clear Control flip-flop */
            drd.control = CLEAR;                        /* clear the control flip-flop */
            drd.flag    = CLEAR;                        /*   and the flag flip-flop */

            if (!drc_run)                               /* cancel curr op */
                sim_cancel (drc_unit);

            drc_sta = drc_sta & ~DRS_SAC;               /* clear SAC flag */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            drd.control = SET;                          /* set the control flip-flop */

            if (drc_cw & CW_WR)                         /* writing? */
                drd.flag = SET;                         /* prime DMA */

            drc_sta = 0;                                /* clr status */
            drd_ptr = 0;                                /* clear sec ptr */
            sim_cancel (drc_unit);                      /* cancel curr op */

            t = CW_GETSEC (drc_cw) - dr_seccntr (sim_gtime());

            if (t <= 0)
                t = t + DR_NUMSC;

            sim_activate (drc_unit, t * DR_NUMWD * dr_time);
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (drd.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioPRH:                                         /* Priority High */
            outbound.signals |= ioPRL | cnPRL | cnVALID;    /* PRL is tied to PRH */
            break;


        case ioSTF:                                     /* not used by this interface */
        case ioSFC:                                     /* not used by this interface */
        case ioSFS:                                     /* not used by this interface */
        case ioPOPIO:                                   /* not used by this interface */
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

   The command channel card does not follow the usual interface I/O
   configuration.  PRL is tied to PRH, so it is always valid.  The card does not
   drive IRQ, FLG, or SRQ and does not respond to IAK.  There are no control,
   flag, or flag buffer flip-flops.  CLF clears the track origin flip-flop; STF
   is ignored.  The 12606B drives SKF, whereas the 12610B does not.  On the
   12610B, SFS tests the Track Origin flip-flop, and SFC tests the Sector Clock
   Phase (SCP) flip-flop.

   Implementation notes:

    1. CRS clears the Run Flip-Flop, stopping the current operation.  Under
       simulation, we allow the data channel signal handler to do this, as the
       same operation is invoked by CLC DC, and as CRS is sent to all devices.
*/

static SIGNALS_VALUE drc_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
int32          sec;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            if (!(drc_unit [0].flags & UNIT_DRUM)) {    /* disk? */
                sec = dr_seccntr (sim_gtime ());        /* current sector */
                sim_cancel (&drd_unit[TMR_ORG]);        /* sched origin tmr */
                sim_activate (&drd_unit[TMR_ORG],
                    (DR_FNUMSC - sec) * DR_NUMWD * dr_time);
                }
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (!(drc_unit [0].flags & UNIT_DRUM)       /* 12606? */
              && CALC_SCP (sim_gtime ()) > 0)           /* skip if nearing end of sector */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (!(drc_unit [0].flags & UNIT_DRUM)       /* 12606? */
              && !sim_is_active (&drd_unit [TMR_ORG]))  /* skip if origin seen */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = drc_sta;                   /* static bits */

            if (!(drc_unit [0].flags & UNIT_PROT) ||    /* not protected? */
                 (CW_GETTRK(drc_cw) >= drc_pcount))     /* or not in range? */
                outbound.value |= DRS_WEN;              /* set wrt enb status */

            if (drc_unit [0].flags & UNIT_ATT) {        /* attached? */
                outbound.value |= (HP_WORD) (dr_seccntr (sim_gtime()) << DRS_V_NS) | DRS_RDY;

                if (sim_is_active (drc_unit))           /* op in progress? */
                    outbound.value |= DRS_BSY;

                if (CALC_SCP (sim_gtime()))             /* SCP ff set? */
                    outbound.value |= DRS_SEC;          /* set sector flag */

                if (sim_is_active (&drd_unit [TMR_INH]) /* inhibit timer on? */
                  && !(drc_cw & CW_WR))
                    outbound.value |= DRS_RIF;          /* set read inh flag */
                }
            break;


        case ioIOO:                                     /* I/O data output */
            drc_cw = inbound_value;                     /* get control word */

            if (!(drc_unit [0].flags & UNIT_DRUM)) {    /* disk? */
                sim_cancel (&drd_unit[TMR_INH]);        /* schedule inhibit timer */
                sim_activate (&drd_unit[TMR_INH], DR_FTIME * DR_NUMWD);
                }
            break;


        case ioPRH:                                         /* Priority High */
            outbound.signals |= ioPRL | cnPRL | cnVALID;    /* PRL is tied to PRH */
            break;


        case ioSTF:                                     /* not used by this interface */
        case ioENF:                                     /* not used by this interface */
        case ioPOPIO:                                   /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioCLC:                                     /* not used by this interface */
        case ioSTC:                                     /* not used by this interface */
        case ioSIR:                                     /* not used by this interface */
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


/* Unit service */

t_stat drc_svc (UNIT *uptr)
{
int32 trk, sec;
uint32 da;
uint16 *bptr = (uint16 *) uptr->filebuf;

if ((uptr->flags & UNIT_ATT) == 0) {
    drc_sta = DRS_ABO;
    return SCPE_OK;
    }

trk = CW_GETTRK (drc_cw);
sec = CW_GETSEC (drc_cw);
da = ((trk * DR_NUMSC) + sec) * DR_NUMWD;
drc_sta = drc_sta | DRS_SAC;
drc_run = 1;                                            /* set run ff */

if (drc_cw & CW_WR) {                                   /* write? */
    if ((da < uptr->capac) && (sec < DR_NUMSC)) {
        bptr[da + drd_ptr] = (uint16) drd_obuf;
        if (((uint32) (da + drd_ptr)) >= uptr->hwmark)
            uptr->hwmark = da + drd_ptr + 1;
        }
    drd_ptr = dr_incda (trk, sec, drd_ptr);             /* inc disk addr */
    if (drd.control) {                                  /* dch active? */
        io_assert (&drd_dev, ioa_ENF);                  /* set the flag and SRQ */
        sim_activate (uptr, dr_time);                   /* sched next word */
        }
    else {                                              /* done */
        if (drd_ptr)                                    /* need to fill? */
            for ( ; drd_ptr < DR_NUMWD; drd_ptr++)
                bptr[da + drd_ptr] = (uint16) drd_obuf; /* fill with last word */
        if (!(drc_unit [0].flags & UNIT_DRUM))          /* disk? */
            drc_sta = drc_sta | DRS_PER;                /* parity bit sets on write */
        drc_run = 0;                                    /* clear run ff */
        }
    }                                                   /* end write */
else {                                                  /* read */
    if (drd.control) {                                  /* dch active? */
        if ((da >= uptr->capac) || (sec >= DR_NUMSC)) drd_ibuf = 0;
        else drd_ibuf = bptr[da + drd_ptr];
        drd_ptr = dr_incda (trk, sec, drd_ptr);
        io_assert (&drd_dev, ioa_ENF);                  /* set the flag and SRQ */
        sim_activate (uptr, dr_time);                   /* sched next word */
        }
    else drc_run = 0;                                   /* clear run ff */
    }
return SCPE_OK;
}

/* Increment current disk address */

int32 dr_incda (int32 trk, int32 sec, int32 ptr)
{
ptr = ptr + 1;                                          /* inc pointer */
if (ptr >= DR_NUMWD) {                                  /* end sector? */
    ptr = 0;                                            /* new sector */
    sec = sec + 1;                                      /* adv sector */
    if (sec >= DR_NUMSC) {                              /* end track? */
        sec = 0;                                        /* new track */
        trk = trk + 1;                                  /* adv track */
        if (trk >= MAX_TRK) trk = 0;                    /* wraps at max */
        }
    drc_cw = (drc_cw & CW_WR) | CW_PUTTRK (trk) | CW_PUTSEC (sec);
    }
return ptr;
}

/* Read the sector counter

   The hardware sector counter contains the number of the next sector that will
   pass under the heads (so it is one ahead of the current sector).  For the
   duration of the last sector of the track, the sector counter contains 90 for
   the 12606 and 0 for the 12610.  The sector counter resets to 0 at track
   origin and increments at the start of the first sector.  Therefore, the
   counter value ranges from 0-90 for the 12606 and 0-31 for the 12610.  The 0
   state is quite short in the 12606 and long in the 12610, relative to the
   other sector counter states.

   The simulated sector counter is calculated from the simulation time, based on
   the time per word and the number of words per track.
*/

int32 dr_seccntr (double simtime)
{
int32 curword;

curword = (int32) fmod (simtime / (double) dr_time,
                    (double) (DR_NUMWD * DR_NUMSC + DR_OVRHEAD));
if (curword <= DR_OVRHEAD) return 0;
else return ((curword - DR_OVRHEAD) / DR_NUMWD +
         ((drc_unit [0].flags & UNIT_DRUM) ? 0 : 1));
}

/* Reset routine */

t_stat drc_reset (DEVICE *dptr)
{
hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &drd_dev)? &drc_dev: &drd_dev);

if (sim_switches & SWMASK ('P')) {                      /* power-on reset? */
    drd_ptr = 0;                                        /* clear sector pointer */
    drc_sta = drc_cw = 0;                               /* clear controller state variables */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

sim_cancel (drc_unit);
sim_cancel (&drd_unit [TMR_ORG]);
sim_cancel (&drd_unit [TMR_INH]);

return SCPE_OK;
}

/* Attach a drive unit.

   The specified file is attached to the indicated drive unit.  If a new file is
   specified, the file is initialized to its capacity by setting the high-water
   mark to the last byte in the file.
*/

t_stat drc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat      result;
const int32 sz = sz_tab [DR_GETSZ (uptr->flags)];

if (sz == 0)
    return SCPE_IERR;
else
    uptr->capac = sz;

result = attach_unit (uptr, cptr);                      /* attach the drive */

if (result == SCPE_OK && (sim_switches & SWMASK ('N'))) /* if the attach was successful and a new image was specified */
    uptr->hwmark = (uint32) uptr->capac;                /*   then set the high-water mark to the last byte */

return result;                                          /* return the result of the attach */
}

/* Set protected track count */

t_stat dr_set_prot (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 count;
t_stat status;

if (cptr == NULL)
    return SCPE_ARG;
count = (int32) get_uint (cptr, 10, 768, &status);
if (status != SCPE_OK)
    return status;
else switch (count) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
        drc_pcount = count;
        break;
    case 256:
    case 512:
    case 768:
        if (drc_unit [0].flags & UNIT_DRUM)
            drc_pcount = count;
        else return SCPE_ARG;
        break;
    default:
        return SCPE_ARG;
        }
return SCPE_OK;
}

/* Show protected track count */

t_stat dr_show_prot (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "protected tracks=%d", drc_pcount);
return SCPE_OK;
}

/* Set size routine */

t_stat dr_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 sz;
int32 szindex;

if (val < 0) return SCPE_IERR;
if ((sz = sz_tab[szindex = DR_GETSZ (val)]) == 0) return SCPE_IERR;
if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
uptr->capac = sz;
if (szindex & UNIT_DRUM) dr_time = DR_DTIME;            /* drum */
else {
    dr_time = DR_FTIME;                                 /* disk */
    if (drc_pcount > 128) drc_pcount = 128;             /* max prot track count */
    }
return SCPE_OK;
}


/* 277x fixed disc/drum bootstrap loaders (BBDL).

   The Basic Binary Disc Loader (BBDL) consists of two programs.  The program
   starting at address x7700 loads absolute paper tapes into memory.  The
   program starting at address x7760 loads a disc-resident bootstrap from the
   277x fixed-head disc/drum into memory.  The S register setting does not
   affect loader operation.

   Entering a LOAD DRC or BOOT DRC command loads the BBDL into memory and
   executes the disc portion starting at x7760.  The bootstrap issues a CLC 0,C
   to clear the disc track and sector address registers and then sets up a
   64-word read from track 0 sector 0 to memory locations 0-77 octal.  It then
   stores a JMP * instruction in location 77, starts the read, and jumps to
   location 77.  The JMP * causes the CPU to loop until the last word read from
   the disc extension overlays location 77 which, typically, would be a JMP
   instruction to the start of the disc-resident bootstrap.  The success or
   failure of the transfer is not checked.

   The HP 1000 does not support the 277x drives, so there is no 1000 boot loader
   ROM for these peripherals.  Attempting to LOAD DRC or BOOT DRC while the CPU
   is configured as a 1000 will be rejected.


   Implementation notes:

    1. After the BBDL is loaded into memory, the paper tape portion may be
       executed manually by setting the P register to the starting address
       (x7700).

    2. For compatibility with the cpu_copy_loader routine, the BBDL has been
       altered from the standard HP version.  The device I/O instructions are
       modified to address select codes 10 and 11.

    3. The "HP 20854A Timeshared BASIC/2000, Level F System Operator's Manual"
       (HP 02000-90074, November 1974) lists an IBL procedure for booting a 21MX
       (i.e., 1000 M-Series) CPU from the fixed-head disc.  However, there is no
       evidence that a fixed-head disc boot loader ROM ever existed.  Moreover,
       the procedure listed is suspicious, as it specifies the command channel
       select code instead of the data channel select code, so I/O instruction
       configuration would be incorrect.  Also, the equivalent 2100 boot
       procedure printed adjacently gives the wrong BBDL starting address (it is
       listed correctly in the 1973 version of the manual).  Actually, the 21MX
       and 2100 procedures appear to be verbatim copies of the moving-head disc
       boot procedures listed two pages earlier.  Consequently, it would appear
       that 21MX-based 2000 F TSB systems with fixed-head drives must boot from
       paper tape.
*/

static const LOADER_ARRAY dr_loaders = {
    {                               /* HP 21xx Basic Binary Disc Loader (BBDL) */
      060,                          /*   loader starting index */
      056,                          /*   DMA index */
      055,                          /*   FWA index */
      { 0107700,                    /*   77700:  ST2   CLC 0,C         START OF PAPER TAPE LOADER */
        0002401,                    /*   77701:        CLA,RSS         */
        0063726,                    /*   77702:  CONT2 LDA CM21        */
        0006700,                    /*   77703:        CLB,CCE         */
        0017742,                    /*   77704:        JSB READ2       */
        0007306,                    /*   77705:  LEDR2 CMB,CCE,INB,SZB */
        0027713,                    /*   77706:        JMP RECL2       */
        0002006,                    /*   77707:  EOTC2 INA,SZA         */
        0027703,                    /*   77710:        JMP CONT2+1     */
        0102077,                    /*   77711:        HLT 77B         */
        0027700,                    /*   77712:        JMP ST2         */
        0077754,                    /*   77713:  RECL2 STB CNT2        */
        0017742,                    /*   77714:        JSB READ2       */
        0017742,                    /*   77715:        JSB READ2       */
        0074000,                    /*   77716:        STB A           */
        0077757,                    /*   77717:        STB ADR11       */
        0067757,                    /*   77720:  SUCID LDB ADR11       */
        0047755,                    /*   77721:        ADB MAXAD       */
        0002040,                    /*   77722:        SEZ             */
        0027740,                    /*   77723:        JMP RESCU       */
        0017742,                    /*   77724:  LOAD2 JSB READ2       */
        0040001,                    /*   77725:        ADA B           */
        0177757,                    /*   77726:  CM21  STB ADR11,I     */
        0037757,                    /*   77727:        ISZ ADR11       */
        0000040,                    /*   77730:        CLE             */
        0037754,                    /*   77731:        ISZ CNT2        */
        0027720,                    /*   77732:        JMP SUCID       */
        0017742,                    /*   77733:        JSB READ2       */
        0054000,                    /*   77734:        CPB A           */
        0027702,                    /*   77735:        JMP CONT2       */
        0102011,                    /*   77736:        HLT 11B         */
        0027700,                    /*   77737:        JMP ST2         */
        0102055,                    /*   77740:  RESCU HLT 55B         */
        0027700,                    /*   77741:        JMP ST2         */
        0000000,                    /*   77742:  READ2 NOP             */
        0006600,                    /*   77743:        CLB,CME         */
        0103710,                    /*   77744:  RED2  STC PR,C        */
        0102310,                    /*   77745:        SFS PR          */
        0027745,                    /*   77746:        JMP *-1         */
        0107410,                    /*   77747:        MIB PR,C        */
        0002041,                    /*   77750:        SEZ,RSS         */
        0127742,                    /*   77751:        JMP READ2,I     */
        0005767,                    /*   77752:        BLF,CLE,BLF     */
        0027744,                    /*   77753:        JMP RED2        */
        0000000,                    /*   77754:  CNT2  NOP             */
        0000000,                    /*   77755:  MAXAD NOP             */
        0020010,                    /*   77756:  CWORD ABS 20000B+DC   */
        0000000,                    /*   77757:  ADR11 NOP             */
        0107700,                    /*   77760:  DLDR  CLC 0,C         START OF FIXED DISC LOADER */
        0063756,                    /*   77761:        LDA CWORD       */
        0102606,                    /*   77762:        OTA 6           */
        0002700,                    /*   77763:        CLA,CCE         */
        0102611,                    /*   77764:        OTA CC          */
        0001500,                    /*   77765:        ERA             */
        0102602,                    /*   77766:        OTA 2           */
        0063777,                    /*   77767:        LDA WRDCT       */
        0102702,                    /*   77770:        STC 2           */
        0102602,                    /*   77771:        OTA 2           */
        0103706,                    /*   77772:        STC 6,C         */
        0102710,                    /*   77773:        STC DC          */
        0067776,                    /*   77774:        LDB JMP77       */
        0074077,                    /*   77775:        STB 77B         */
        0024077,                    /*   77776:  JMP77 JMP 77B         */
        0177700 } },                /*   77777:  WRDCT OCT -100        */

    {                               /* HP 1000 Loader ROM does not exist */
      IBL_NA,                       /*   loader starting index */
      IBL_NA,                       /*   DMA index */
      IBL_NA,                       /*   FWA index */
      { 0 } }
    };


/* Device boot routine.

   This routine is called by the LOAD DRC and BOOT DRC commands to copy the
   device bootstrap into the upper 64 words of the logical address space.  On
   entry, the "unitno" parameter is checked to ensure that it is 0, as the
   bootstrap only loads from unit 0.  Then the BBDL is loaded into memory, the
   disc portion is configured for the DRD/DRC select code pair, and the paper
   tape portion is  configured for the select code of the paper tape reader.


   Implementation notes:

    1. The fixed-head disc/drum device is not supported on the HP 1000, so this
       routine cannot be called by a BOOT CPU or LOAD CPU command.

    2. In hardware, the BBDL was hand-configured for the disc and paper tape
       reader select codes when it was installed on a given system.  Under
       simulation, the LOAD and BOOT commands automatically configure the BBDL
       to the current select codes of the PTR and DR devices.
 */

t_stat drc_boot (int32 unitno, DEVICE *dptr)
{
uint32 start;

if (unitno != 0)                                        /* a BOOT DRC for a non-zero unit */
    return SCPE_NOFNC;                                  /*   is rejected as unsupported */

else                                                            /* otherwise this is a BOOT/LOAD DRC */
    start = cpu_copy_loader (dr_loaders, drd_dib.select_code,   /*   so copy the boot loader to memory */
                             IBL_S_NOCLEAR, IBL_S_NOSET);       /*     and preserve the S register */

if (start == 0)                                         /* if the copy failed */
    return SCPE_NOFNC;                                  /*   then reject the command */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   the boot loader was successfully copied */
}
