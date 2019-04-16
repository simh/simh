/* hp2100_lps.c: HP 2100 12653A Line Printer Interface simulator

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

   LPS          HP 12653A Line Printer Interface

   14-Nov-18    JDB     Diagnostic mode now uses GP Register jumper configuration
   26-Jun-18    JDB     Revised I/O model
   03-Aug-17    JDB     Changed perror call for I/O errors to cprintf
   20-Jul-17    JDB     Removed "lps_stopioe" variable and register
   17-Mar-17    JDB     Added "-N" handling to the attach routine
   15-Mar-17    JDB     Changed DEBUG_PRS calls to tprintfs
   13-May-16    JDB     Modified for revised SCP API function parameter types
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
                        Revised detection of CLC at last DMA cycle
   19-Oct-10    JDB     Corrected 12566B (DIAG mode) jumper settings
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   10-May-07    RMS     Added UNIT_TEXT flag
   11-Jan-07    JDB     CLC cancels I/O event if DIAG (jumper W9 in "A" pos)
                        Added ioCRS state to I/O decoders
   19-Nov-04    JDB     Added restart when set online, etc.
                        Fixed col count for non-printing chars
   01-Oct-04    JDB     Added SET OFFLINE/ONLINE, POWEROFF/POWERON
                        Fixed status returns for error conditions
                        Fixed handling of non-printing characters
                        Fixed handling of characters after column 80
                        Improved timing model accuracy for RTE
                        Added fast/realistic timing
                        Added debug printouts
   03-Jun-04    RMS     Fixed timing (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Revised for extended file support
   24-Oct-02    RMS     Added microcircuit test features
   30-May-02    RMS     Widened POS to 32b
   03-Dec-01    RMS     Changed DEVNO to use extended SET/SHOW
   07-Sep-01    RMS     Moved function prototypes
   21-Nov-00    RMS     Fixed flag, fbf power up state
                        Added command flop
   15-Oct-00    RMS     Added variable device number support

   References:
     - 2767A Line Printer Operating and Service Manual
         (02767-90002, October 1973)
     - 12653A Line Printer Interface Kit
         (12653-90002, October 1971)
     - General Purpose Register Diagnostic Reference Manual
         (24391-90001, April 1982)


   The HP 12653A Line Printer Interface Kit connects the 2767A printer to the HP
   1000 family.  The subsystem consists of an interface card employing TTL-level
   line drivers and receivers, an interconnecting cable, and an HP 2767A (from
   356 to 1110 lines per minute) line printer.  The interface is supported by
   RTE and DOS drivers DVR12.  The interface supports DMA transfers, but the OS
   drivers do not use them.

   The 2767 impact printer has a rotating drum with 80 columns of 64 raised
   characters.  ASCII codes 32 through 95 (SPACE through "_") form the print
   repertoire.  The printer responds to the control characters FF, LF, and CR.

   The 80 columns are divided into four zones of 20 characters each that are
   addressed sequentially.  Received characters are buffered in a 20-character
   memory.  When the 20th printable character is received, the current zone is
   printed, and the memory is reset.  In the absence of print command
   characters, a zone print operation will commence after each group of 20
   printable characters is transmitted to the printer.

   The print command characters have these actions:

    * CR -- print the characters in the current zone, reset to zone 1, and clear
            the buffer memory.
    * LF -- same as CR, plus advances the paper one line.
    * FF -- same as CR, plus advances the paper to the top of the next form.

   The 2767 provides two status bits via the interface:

     bit 15 -- printer not ready
     bit  0 -- printer busy

   The expected status returns are:

     100001 -- power off or cable disconnected
     100001 -- initial power on, then changes to 000001 within sixty
               seconds of initial power on
     000001 -- power on, paper unloaded or printer offline or not idle
     000000 -- power on, paper loaded and printer online and idle

   These simulator commands provide the listed printer states:

     SET LPS POWEROFF --> power off or cable disconnected
     SET LPS POWERON  --> power on
     SET LPS OFFLINE  --> printer offline
     SET LPS ONLINE   --> printer online
     ATT LPS <file>   --> paper loaded
     DET LPS          --> paper out

   The following implemented behaviors have been inferred from secondary sources
   (diagnostics, operating system drivers, etc.), due to absent or contradictory
   authoritative information; future correction may be needed:

     1. Paper out sets BUSY instead of NOT READY.
     2. Print operation in progress sets BUSY instead of NOT READY.
     3. Characters not in the print repertoire are replaced with blanks.
     4. The 81st and succeeding characters overprint the current line.

   A diagnostic mode is provided to simulate the installation of the 1251-0332
   loopback connecctor, modified to connect pins Z/22 to pins AA/23 as required
   by the General Purpose Register Diagnostic.  This ties the output data lines
   to the input data lines and the device command output to the device flag
   input.  Entering diagnostic mode also configures the jumpers correctly for
   the diagnostic.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Printer program constants */

#define CR                  '\r'                /* carriage return */
#define LF                  '\n'                /* line feed */
#define FF                  '\f'                /* form feed */

#define DATA_MASK           0177u               /* printer uses only 7 bits for data */

#define LPS_ZONECNT         20                  /* zone char count */
#define LPS_PAGECNT         80                  /* page char count */
#define LPS_PAGELNT         60                  /* page line length */
#define LPS_FORMLNT         66                  /* form line length */

/* Printer power states */

#define LPS_ON              0                   /* power is on */
#define LPS_OFF             1                   /* power is off */
#define LPS_TURNING_ON      2                   /* power is turning on */

#define LPS_BUSY            0000001             /* busy status */
#define LPS_NRDY            0100000             /* not ready status */
#define LPS_PWROFF          LPS_BUSY | LPS_NRDY /* power-off status */

#define UNIT_V_DIAG         (UNIT_V_UF + 0)     /* diagnostic mode */
#define UNIT_V_POWEROFF     (UNIT_V_UF + 1)     /* unit powered off */
#define UNIT_V_OFFLINE      (UNIT_V_UF + 2)     /* unit offline */

#define UNIT_DIAG           (1 << UNIT_V_DIAG)
#define UNIT_POWEROFF       (1 << UNIT_V_POWEROFF)
#define UNIT_OFFLINE        (1 << UNIT_V_OFFLINE)

static struct {
    FLIP_FLOP control;                          /* control flip-flop */
    FLIP_FLOP flag;                             /* flag flip-flop */
    FLIP_FLOP flag_buffer;                      /* flag buffer flip-flop */
    } lps = { CLEAR, CLEAR, CLEAR };

static int32  lps_ccnt = 0;                     /* character count */
static int32  lps_lcnt = 0;                     /* line count */
static int32  lps_sta = 0;                      /* printer status */
static t_bool lps_fast_timing = TRUE;           /* timing type */
static uint32 lps_power = LPS_ON;               /* power state */

/* Hardware timing:
   (based on 1580 instr/msec)             instr   msec   calc msec
                                          ------------------------
   - character transfer time   : ctime =      2    2 us
   - per-zone printing time    : ptime =  55300   35        40
   - per-line paper slew time  : stime =  17380   11        13
   - power-on ready delay time : rtime = 158000  100

  NOTE: the printer acknowledges before the print motion has stopped to allow
        for continuous slew, so the set times are a bit less than the calculated
        operation time from the manual.

  NOTE: the 2767 diagnostic checks completion times, so the realistic timing
  must be used.  Because simulator timing is in instructions, and because the
  diagnostic uses the TIMER instruction (~1580 executions per millisecond) when
  running on a 1000-E/F but a software timing loop (~400-600 executions per
  millisecond) when running on anything else, realistic timings are decreased by
  three-fourths when not executing on an E/F.
*/

static int32 lps_ctime = 0;                     /* char xfer time */
static int32 lps_ptime = 0;                     /* zone printing time */
static int32 lps_stime = 0;                     /* paper slew time */
static int32 lps_rtime = 0;                     /* power-on ready time */

typedef int32 TIMESET[4];                       /* set of controller times */

static int32 * const lps_timers[] = { &lps_ctime, &lps_ptime, &lps_stime, &lps_rtime };

static const TIMESET lps_times[2] = {
    { 2, 55300, 17380, 158000 },                /* REALTIME */
    { 2,  1000,   1000,  1000 }                 /* FASTTIME */
    };

static INTERFACE lps_interface;

static t_stat lps_svc (UNIT *uptr);
static t_stat lps_reset (DEVICE *dptr);
static t_stat lps_restart (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lps_poweroff (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lps_poweron (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lps_attach (UNIT *uptr, CONST char *cptr);
static t_stat lps_set_timing (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat lps_show_timing (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* LPS data structures

   lps_dev      LPS device descriptor
   lps_unit     LPS unit descriptor
   lps_reg      LPS register list
*/

static DIB lps_dib = {
    &lps_interface,                             /* the device's I/O interface function pointer */
    LPS,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12653A Line Printer Interface",            /* the card description */
    NULL };                                     /* the ROM description */

static UNIT lps_unit = {
    UDATA (&lps_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_DISABLE+UNIT_TEXT, 0)
    };

static REG lps_reg[] = {
/*    Macro   Name    Location               Width    Offset   Flags  */
/*    ------  ------  -------------------  ---------  ------  ------- */
    { ORDATA (BUF,    lps_unit.buf,           16),            REG_X   },
    { ORDATA (STA,    lps_sta,                16)                     },
    { ORDATA (POWER,  lps_power,               2),            REG_RO  },
    { FLDATA (CTL,    lps.control,                      0)            },
    { FLDATA (FLG,    lps.flag,                         0)            },
    { FLDATA (FBF,    lps.flag_buffer,                  0)            },
    { DRDATA (CCNT,   lps_ccnt,                7),            PV_LEFT },
    { DRDATA (LCNT,   lps_lcnt,                7),            PV_LEFT },
    { DRDATA (POS,    lps_unit.pos,        T_ADDR_W),         PV_LEFT },
    { DRDATA (CTIME,  lps_ctime,              24),            PV_LEFT },
    { DRDATA (PTIME,  lps_ptime,              24),            PV_LEFT },
    { DRDATA (STIME,  lps_stime,              24),            PV_LEFT },
    { DRDATA (RTIME,  lps_rtime,              24),            PV_LEFT },
    { FLDATA (TIMING, lps_fast_timing,                  0),   REG_HRO },

      DIB_REGS (lps_dib),

    { NULL }
    };

static MTAB lps_mod[] = {
/*    Mask Value     Match Value    Print String       Match String   Validation          Display  Descriptor */
/*    -------------  -------------  -----------------  -------------  ------------------  -------  ---------- */
    { UNIT_DIAG,     UNIT_DIAG,     "diagnostic mode", "DIAGNOSTIC",  NULL,               NULL,    NULL       },
    { UNIT_DIAG,     0,             "printer mode",    "PRINTER",     NULL,               NULL,    NULL       },

    { UNIT_OFFLINE,  UNIT_OFFLINE,  "offline",         "OFFLINE",     NULL,               NULL,    NULL       },
    { UNIT_OFFLINE,  0,             "online",          "ONLINE",      &lps_restart,       NULL,    NULL       },

    { UNIT_POWEROFF, UNIT_POWEROFF, "power off",       "POWEROFF",    &lps_poweroff,      NULL,    NULL       },
    { UNIT_POWEROFF, 0,             "power on",        "POWERON",     &lps_poweron,       NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation        Display            Descriptor        */
/*    -------------------  -----  ------------  ------------  ----------------  -----------------  ----------------- */
    { MTAB_XDV,              1,   NULL,         "FASTTIME",   &lps_set_timing,  NULL,              NULL              },
    { MTAB_XDV,              0,   NULL,         "REALTIME",   &lps_set_timing,  NULL,              NULL              },
    { MTAB_XDV,              0,   "TIMING",     NULL,         NULL,             &lps_show_timing,  NULL              },

    { MTAB_XDV,              1u,  "SC",         "SC",         &hp_set_dib,      &hp_show_dib,      (void *) &lps_dib },
    { MTAB_XDV | MTAB_NMO,  ~1u,  "DEVNO",      "DEVNO",      &hp_set_dib,      &hp_show_dib,      (void *) &lps_dib },
    { 0 }
    };

static DEBTAB lps_deb [] = {
    { "CMDS",  DEB_CMDS    },
    { "CPU",   DEB_CPU     },
    { "XFER",  DEB_XFER    },
    { "STATE", TRACE_STATE },
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };

DEVICE lps_dev = {
    "LPS",                                      /* device name */
    &lps_unit,                                  /* unit array */
    lps_reg,                                    /* register array */
    lps_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &lps_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &lps_attach,                                /* attach routine */
    NULL,                                       /* detach routine */
    &lps_dib,                                   /* device information block */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    lps_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* I/O signal handler */

static SIGNALS_VALUE lps_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;
int32          current_line, current_char;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            lps.flag_buffer = CLEAR;                    /* reset the flag buffer */
            lps.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            lps.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (lps.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                lps.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (lps.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (lps.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                             /* I/O Data Input */
            if ((lps_unit.flags & UNIT_DIAG) == 0) {            /* real lpt? */
                if (lps_power == LPS_ON) {                      /* power on? */
                    if (((lps_unit.flags & UNIT_ATT) == 0) ||   /* paper out? */
                        (lps_unit.flags & UNIT_OFFLINE) ||      /* offline? */
                        sim_is_active (&lps_unit)) lps_sta = LPS_BUSY;

                    else
                        lps_sta = 0;
                    }

                else
                    lps_sta = LPS_PWROFF;
                }

            outbound.value = lps_sta;

            tprintf (lps_dev, DEB_CPU, "Status %06o returned\n", lps_sta);
            break;


        case ioIOO:                                     /* I/O Data Output */
            lps_unit.buf = inbound_value;

            tprintf (lps_dev, DEB_CPU, "Control %06o (%s) output\n",
                     lps_unit.buf, fmt_char (lps_unit.buf & DATA_MASK));
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            lps.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            lps_unit.buf = 0;                           /*   and clear the output register */
            break;


        case ioCRS:                                     /* Control Reset */
            lps.control = CLEAR;                        /* clear the control flip-flop */
            sim_cancel (&lps_unit);                     /*   and cancel any printing in progress */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            lps.control = CLEAR;                        /* clear the control flip-flop */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            lps.control = SET;                          /* set the control flip-flop */

            if (lps_unit.flags & UNIT_DIAG) {           /* diagnostic? */
                lps_sta = lps_unit.buf;                 /* loop back data */
                sim_activate_abs (&lps_unit, 1);        /*   and set the flag after the next instruction */
                }

            else {                                      /* real lpt, sched */
                current_char = lps_ccnt + 1;
                current_line = lps_lcnt + 1;

                if ((lps_unit.buf != FF) &&
                    (lps_unit.buf != LF) &&
                    (lps_unit.buf != CR)) {             /* normal char */
                    lps_ccnt = lps_ccnt + 1;            /* incr char counter */
                    if (lps_ccnt % LPS_ZONECNT == 0)    /* end of zone? */
                        lps_unit.wait = lps_ptime;      /* print zone */
                    else
                        lps_unit.wait = lps_ctime;      /* xfer char */
                    }

                else {                                  /* print cmd */
                    if (lps_ccnt % LPS_ZONECNT == 0)    /* last zone printed? */
                        lps_unit.wait = lps_ctime;      /* yes, so just char time */
                    else
                        lps_unit.wait = lps_ptime;      /* no, so print needed */

                    lps_ccnt = 0;                       /* reset char counter */

                    if (lps_unit.buf == LF) {           /* line advance */
                        lps_lcnt = (lps_lcnt + 1) % LPS_PAGELNT;

                        if (lps_lcnt > 0)
                            lps_unit.wait += lps_stime;
                        else
                            lps_unit.wait +=            /* allow for perf skip */
                              lps_stime * (LPS_FORMLNT - LPS_PAGELNT);
                        }

                    else if (lps_unit.buf == FF) {      /* form advance */
                        lps_unit.wait += lps_stime * (LPS_FORMLNT - lps_lcnt);
                        lps_lcnt = 0;
                        }
                    }

                sim_activate (&lps_unit, lps_unit.wait);

                tprintf (lps_dev, DEB_CMDS, "Character %s scheduled for line %d, column %d, time = %d\n",
                         fmt_char (lps_unit.buf & DATA_MASK), current_line, current_char, lps_unit.wait);
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (lps.control & lps.flag)                 /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (lps.control & lps.flag & lps.flag_buffer)   /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;        /*   then conditionally assert IRQ */

            if (lps.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            lps.flag_buffer = CLEAR;                    /* clear the flag buffer flip-flop */
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
    }

return outbound;                                        /* return the outbound signals and value */
}


/* Unit service.

   As a convenience to the user, the printer output file is flushed when a TOF
   operation is performed.
*/

static t_stat lps_svc (UNIT *uptr)
{
int32 c = uptr->buf & DATA_MASK;

if (lps_power == LPS_TURNING_ON) {                      /* printer warmed up? */
    lps_power = LPS_ON;                                 /* change state */
    lps_restart (uptr, 0, NULL, NULL);                  /* restart I/O if hung */
    tprintf (lps_dev, TRACE_STATE, "Power state is ON\n");
    return SCPE_OK;                                     /* done */
    }

if (uptr->flags & UNIT_DIAG) {                          /* diagnostic? */
    lps.flag_buffer = SET;
    io_assert (&lps_dev, ioa_ENF);                      /* set flag */
    return SCPE_OK;                                     /* done */
    }

if ((uptr->flags & (UNIT_ATT | UNIT_OFFLINE | UNIT_POWEROFF)) != UNIT_ATT)  /* not ready? */
    return SCPE_OK;

lps.flag_buffer = SET;
io_assert (&lps_dev, ioa_ENF);                          /* set flag */

if (((c < ' ') || (c > '_')) &&                         /* non-printing char? */
    (c != FF) && (c != LF) && (c != CR)) {
        tprintf (lps_dev, DEB_XFER, "Character %s erased\n", fmt_char (c));
        c = ' ';                                        /* replace with blank */
        }

if (lps_ccnt > LPS_PAGECNT) {                           /* 81st character? */
    fputc (CR, uptr->fileref);                          /* return to line start */
    uptr->pos = uptr->pos + 1;                          /* update pos */
    lps_ccnt = 1;                                       /* reset char counter */
    tprintf (lps_dev, DEB_XFER, "Line wraparound to column 1\n");
    }

fputc (c, uptr->fileref);                               /* "print" char */
uptr->pos = uptr->pos + 1;                              /* update pos */

tprintf (lps_dev, DEB_XFER, "Character %s printed\n", fmt_char (c));

if (lps_lcnt == 0) {                                    /* if the printer is at the TOF */
    fflush (uptr->fileref);                             /*   then flush the file buffer for inspection */

    if (c == LF) {                                      /* LF did TOF? */
        fputc (FF, uptr->fileref);                      /* do perf skip */
        uptr->pos = uptr->pos + 1;                      /* update pos */
        tprintf (lps_dev, DEB_XFER, "Perforation skip to TOF\n");
        }
    }

if (ferror (uptr->fileref)) {                           /* if a host file I/O error occurred */
    cprintf ("%s simulator printer I/O error: %s\n",    /*   then report the error to the console */
             sim_name, strerror (errno));

    clearerr (uptr->fileref);                           /* clear the error */

    lps_unit.flags |= UNIT_OFFLINE;                     /* set offline */
    return SCPE_IOERR;
    }

return SCPE_OK;
}


/* Reset routine */

static t_stat lps_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* power-on reset? */
    lps_power = LPS_ON;                                 /* power is on */
    lps_set_timing (NULL, lps_fast_timing, NULL, NULL); /* init timing set */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

lps_sta = 0;                                            /* clear status */
sim_cancel (&lps_unit);                                 /* deactivate unit */

return SCPE_OK;
}

/* Restart I/O routine

   If I/O is started via STC, and the printer is powered off, offline,
   or out of paper, the CTL and CMD flip-flops will set, a service event
   will be scheduled, and the service routine will be entered.  If
   STOP_IOE is not set, the I/O operation will "hang" at that point
   until the printer is powered on, set online, or paper is supplied
   (attached).

   If a pending operation is "hung" when this routine is called, it is
   restarted, which clears CTL and sets FBF and FLG, completing the
   original I/O request.
 */

static t_stat lps_restart (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (lps.control && !sim_is_active (uptr))
    sim_activate (uptr, 1);                             /* reschedule I/O */
return SCPE_OK;
}

/* Printer power off */

static t_stat lps_poweroff (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
lps_power = LPS_OFF;                                    /* change state */
tprintf (lps_dev, TRACE_STATE, "Power state is OFF\n");
return SCPE_OK;
}

/* Printer power on */

static t_stat lps_poweron (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (lps_unit.flags & UNIT_DIAG) {                       /* diag mode? */
    lps_power = LPS_ON;                                 /* no delay */
    tprintf (lps_dev, TRACE_STATE, "Power state is ON\n");
    }
else {
    lps_power = LPS_TURNING_ON;                         /* change state */
    lps_unit.flags |= UNIT_OFFLINE;                     /* set offline */
    sim_activate (&lps_unit, lps_rtime);                /* schedule ready */
    tprintf (lps_dev, TRACE_STATE, "Power state is TURNING ON, scheduled time = %d\n",
             lps_rtime);
    }
return SCPE_OK;
}

/* Attach the printer image file.

   The specified file is attached to the indicated unit.  This is the simulation
   equivalent of loading paper into the printer and pressing the ONLINE button.
   The transition from offline to online typically generates an interrupt.

   A new image file may be requested by giving the "-N" switch to the ATTACH
   command.  If an existing file is specified with "-N", it will be cleared; if
   specified without "-N", printer output will be appended to the end of the
   existing file content.  In all cases, the paper is positioned at the top of
   the form.


   Implementation notes:

    1. If we are called during a RESTORE command to reattach a file previously
       attached when the simulation was SAVEd, the device status and file
       position are not altered.  This is because SIMH 4.x restores the register
       contents before reattaching, while 3.x reattaches before restoring the
       registers.
*/

static t_stat lps_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = hp_attach (uptr, cptr);                        /* attach the specified printer image file for appending */

if (result == SCPE_OK                                   /* if the attach was successful */
  && (sim_switches & SIM_SW_REST) == 0) {               /*   and we are not being called during a RESTORE command */
    lps_ccnt = 0;                                       /*     then clear the character counter */
    lps_lcnt = 0;                                       /*       and set top of form */

    lps_restart (uptr, 0, NULL, NULL);                  /* restart I/O if hung */
    }

return result;
}

/* Set printer timing

   Realistic timing is factored, depending on CPU model, to account for the
   timing method employed by the diagnostic.  In realistic timing mode, the
   diagnostic executes fewer instructions per interval if the CPU is not a 1000
   E or F series machine.
*/

static t_stat lps_set_timing (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i, factor;

lps_fast_timing = (val != 0);                           /* determine choice */

if (lps_fast_timing                                     /* if optimized timing is used */
  || cpu_configuration & (CPU_1000_E | CPU_1000_F))     /*   or this is a 1000 E or F CPU */
    factor = 1;                                         /*     then no time correction is needed */
else                                                    /* otherwise */
    factor = 4;                                         /*   the times will be slower */

for (i = 0; i < (sizeof (lps_timers) / sizeof (lps_timers [0])); i++)
    *lps_timers [i] = lps_times [lps_fast_timing] [i] / factor; /* assign times */
return SCPE_OK;
}

/* Show printer timing */

static t_stat lps_show_timing (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (lps_fast_timing)
    fputs ("fast timing", st);
else
    fputs ("realistic timing", st);
return SCPE_OK;
}
