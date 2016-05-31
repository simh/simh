/* hp2100_stddev.c: HP2100 standard devices simulator

   Copyright (c) 1993-2016, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   PTR          12597A-002 paper tape reader interface
   PTP          12597A-005 paper tape punch interface
   TTY          12531C buffered teleprinter interface
   TBG          12539C time base generator

   13-May-16    JDB     Modified for revised SCP API function parameter types
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Added casts for explicit downward conversions
   28-Dec-12    JDB     Allocate the TBG logical name during power-on reset
   18-Dec-12    MP      Now calls sim_activate_time to get remaining poll time
   09-May-12    JDB     Separated assignments from conditional expressions
   12-Feb-12    JDB     Add TBG as a logical name for the CLK device
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   25-Apr-08    JDB     Changed TTY output wait from 100 to 200 for MSU BASIC
   18-Apr-08    JDB     Removed redundant control char handling definitions
   14-Apr-08    JDB     Changed TTY console poll to 10 msec. real time
                        Synchronized CLK with TTY if set for 10 msec.
                        Added UNIT_IDLE to TTY and CLK
   09-Jan-08    JDB     Fixed PTR trailing null counter for tape re-read
   31-Dec-07    JDB     Added IPTICK register to CLK to display CPU instr/tick
                        Corrected and verified ioCRS actions
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   22-Nov-05    RMS     Revised for new terminal processing routines
   13-Sep-04    JDB     Added paper tape loop mode, DIAG/READER modifiers to PTR
                        Added PV_LEFT to PTR TRLLIM register
                        Modified CLK to permit disable
   15-Aug-04    RMS     Added tab to control char set (from Dave Bryan)
   14-Jul-04    RMS     Generalized handling of control char echoing
                        (from Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed SR setting in IBL
                        Fixed input behavior during typeout for RTE-IV
                        Suppressed nulls on TTY output for RTE-IV
                        Implemented DMA SRQ (follows FLG)
   29-Mar-03    RMS     Added support for console backpressure
   25-Apr-03    RMS     Added extended file support
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Revised BOOT command for IBL ROMs
                        Fixed bug in TTY reset, TTY starts in input mode
                        Fixed bug in TTY mode OTA, stores data as well
                        Fixed clock to add calibration, proper start/stop
                        Added UC option to TTY output
   30-May-02    RMS     Widened POS to 32b
   22-Mar-02    RMS     Revised for dynamically allocated memory
   03-Nov-01    RMS     Changed DEVNO to use extended SET/SHOW
   29-Nov-01    RMS     Added read only unit support
   24-Nov-01    RMS     Changed TIME to an array
   07-Sep-01    RMS     Moved function prototypes
   21-Nov-00    RMS     Fixed flag, buffer power up state
                        Added status input for ptp, tty
   15-Oct-00    RMS     Added dynamic device number support

   References:
   - 2748B Tape Reader Operating and Service Manual (02748-90041, Oct-1977)
   - 12597A 8-Bit Duplex Register Interface Kit Operating and Service Manual
            (12597-9002, Sep-1974)
   - 12531C Buffered Teleprinter Interface Kit Operating and Service Manual
            (12531-90033, Nov-1972)
   - 12539C Time Base Generator Interface Kit Operating and Service Manual
            (12539-90008, Jan-1975)


   The reader and punch, like most HP devices, have a command flop.  The
   teleprinter and clock do not.

   Reader diagnostic mode simulates a tape loop by rewinding the tape image file
   upon EOF.  Normal mode EOF action is to supply TRLLIM nulls and then either
   return SCPE_IOERR or SCPE_OK without setting the device flag.

   To support CPU idling, the teleprinter interface (which doubles as the
   simulator console) polls for input using a calibrated timer with a ten
   millisecond period.  Other polled-keyboard input devices (multiplexers and
   the BACI card) synchronize with the console poll to ensure maximum available
   idle time.  The console poll is guaranteed to run, as the TTY device cannot
   be disabled.

   The clock (time base generator) autocalibrates.  If the TBG is set to a ten
   millisecond period (e.g., as under RTE), it is synchronized to the console
   poll.  Otherwise (e.g., as under DOS or TSB, which use 100 millisecond
   periods), it runs asynchronously.  If the specified clock frequency is below
   10Hz, the clock service routine runs at 10Hz and counts down a repeat counter
   before generating an interrupt.  Autocalibration will not work if the clock
   is running at 1Hz or less.

   Clock diagnostic mode corresponds to inserting jumper W2 on the 12539C.
   This turns off autocalibration and divides the longest time intervals down
   by 10**3.  The clk_time values were chosen to allow the diagnostic to
   pass its clock calibration test.
*/

#include "hp2100_defs.h"

#define TTY_OUT_WAIT    200                             /* TTY output wait */

#define UNIT_V_DIAG     (TTUF_V_UF + 0)                 /* diag mode */
#define UNIT_V_AUTOLF   (TTUF_V_UF + 1)                 /* auto linefeed */
#define UNIT_DIAG       (1 << UNIT_V_DIAG)
#define UNIT_AUTOLF     (1 << UNIT_V_AUTOLF)

#define PTP_LOW         0000040                         /* low tape */
#define TM_MODE         0100000                         /* mode change */
#define TM_KBD          0040000                         /* enable keyboard */
#define TM_PRI          0020000                         /* enable printer */
#define TM_PUN          0010000                         /* enable punch */
#define TP_BUSY         0100000                         /* busy */

#define CLK_V_ERROR     4                               /* clock overrun */
#define CLK_ERROR       (1 << CLK_V_ERROR)

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } ptr = { CLEAR, CLEAR, CLEAR };

int32 ptr_stopioe = 0;                                  /* stop on error */
int32 ptr_trlcnt = 0;                                   /* trailer counter */
int32 ptr_trllim = 40;                                  /* trailer to add */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } ptp = { CLEAR, CLEAR, CLEAR };

int32 ptp_stopioe = 0;

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } tty = { CLEAR, CLEAR, CLEAR };

int32 ttp_stopioe = 0;
int32 tty_buf = 0;                                      /* tty buffer */
int32 tty_mode = 0;                                     /* tty mode */
int32 tty_shin = 0377;                                  /* tty shift in */
int32 tty_lf = 0;                                       /* lf flag */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } clk = { CLEAR, CLEAR, CLEAR };

int32 clk_select = 0;                                   /* clock time select */
int32 clk_error = 0;                                    /* clock error */
int32 clk_ctr = 0;                                      /* clock counter */
int32 clk_time[8] = {                                   /* clock intervals */
    155, 1550, 15500, 155000, 155000, 155000, 155000, 155000
    };
int32 clk_tps[8] = {                                    /* clock tps */
    10000, 1000, 100, 10, 10, 10, 10, 10
    };
int32 clk_rpt[8] = {                                    /* number of repeats */
    1, 1, 1, 1, 10, 100, 1000, 10000
    };
uint32 clk_tick = 0;                                    /* instructions per tick */

DEVICE ptr_dev, ptp_dev, tty_dev, clk_dev;

IOHANDLER ptrio;
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_attach (UNIT *uptr, CONST char *cptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);

IOHANDLER ptpio;
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);

IOHANDLER ttyio;
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_alf (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tto_out (int32 c);
t_stat ttp_out (int32 c);

IOHANDLER clkio;
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
int32 clk_delay (int32 flg);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_mod      PTR modifiers
   ptr_reg      PTR register list
*/

DIB ptr_dib = { &ptrio, PTR };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 8) },
    { FLDATA (CTL, ptr.control, 0) },
    { FLDATA (FLG, ptr.flag, 0) },
    { FLDATA (FBF, ptr.flagbuf, 0) },
    { DRDATA (TRLCTR, ptr_trlcnt, 8), REG_HRO },
    { DRDATA (TRLLIM, ptr_trllim, 8), PV_LEFT },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { ORDATA (SC, ptr_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, ptr_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB ptr_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
    { UNIT_DIAG, 0, "reader mode", "READER", NULL },
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &ptr_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &ptr_dev },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, &ptr_attach, NULL,
    &ptr_dib, DEV_DISABLE
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_mod      PTP modifiers
   ptp_reg      PTP register list
*/

DIB ptp_dib = { &ptpio, PTP };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { FLDATA (CTL, ptp.control, 0) },
    { FLDATA (FLG, ptp.flag, 0) },
    { FLDATA (FBF, ptp.flagbuf, 0) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { ORDATA (SC, ptp_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, ptp_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &ptp_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &ptp_dev },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    &ptp_dib, DEV_DISABLE
    };

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
   tty_mod      TTy modifiers list
*/

#define TTI     0
#define TTO     1
#define TTP     2

DIB tty_dib = { &ttyio, TTY };

UNIT tty_unit[] = {
    { UDATA (&tti_svc, UNIT_IDLE | TT_MODE_UC, 0), POLL_WAIT },
    { UDATA (&tto_svc, TT_MODE_UC, 0), TTY_OUT_WAIT },
    { UDATA (&tto_svc, UNIT_SEQ | UNIT_ATTABLE | TT_MODE_8B, 0), SERIAL_OUT_WAIT }
    };

REG tty_reg[] = {
    { ORDATA (BUF, tty_buf, 8) },
    { ORDATA (MODE, tty_mode, 16) },
    { ORDATA (SHIN, tty_shin, 8), REG_HRO },
    { FLDATA (CTL, tty.control, 0) },
    { FLDATA (FLG, tty.flag, 0) },
    { FLDATA (FBF, tty.flagbuf, 0) },
    { FLDATA (KLFP, tty_lf, 0), REG_HRO },
    { DRDATA (KPOS, tty_unit[TTI].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPOS, tty_unit[TTO].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TTIME, tty_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (PPOS, tty_unit[TTP].pos, T_ADDR_W), PV_LEFT },
    { FLDATA (STOP_IOE, ttp_stopioe, 0) },
    { ORDATA (SC, tty_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, tty_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB tty_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_opt },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_opt },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_opt },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &tty_set_opt },
    { UNIT_AUTOLF, UNIT_AUTOLF, "autolf", "AUTOLF", &tty_set_alf },
    { UNIT_AUTOLF, 0          , NULL, "NOAUTOLF", &tty_set_alf },
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &tty_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &tty_dev },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    3, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    &tty_dib, 0
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_mod      CLK modifiers
   clk_reg      CLK register list
*/

DIB clk_dib = { &clkio, CLK };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0) };

REG clk_reg[] = {
    { ORDATA (SEL, clk_select, 3) },
    { DRDATA (CTR, clk_ctr, 14) },
    { FLDATA (CTL, clk.control, 0) },
    { FLDATA (FLG, clk.flag, 0) },
    { FLDATA (FBF, clk.flagbuf, 0) },
    { FLDATA (ERR, clk_error, CLK_V_ERROR) },
    { BRDATA (TIME, clk_time, 10, 24, 8) },
    { DRDATA (IPTICK, clk_tick, 24), PV_RSPC | REG_RO },
    { ORDATA (SC, clk_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, clk_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
    { UNIT_DIAG, 0, "calibrated", "CALIBRATED", NULL },
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &clk_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &clk_dev },
    { 0 }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, DEV_DISABLE,
    0, NULL, NULL, NULL
    };


/* Paper tape reader I/O signal handler.

   Implementation notes:

    1. The 12597A duplex register card is used to interface the paper tape
       reader to the computer.  This card has a device command flip-flop, which
       supplies the READ signal to the tape reader.  Under simulation, this
       state is implied by the activation of the PTR unit.

    2. The POPIO signal clears the output buffer of the duplex card.  However,
       the buffer outputs are not used by the paper tape reader.  Under
       simulation, we omit the buffer clear.
*/

uint32 ptrio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            ptr.flag = ptr.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            ptr.flag = ptr.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (ptr);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (ptr);
            break;


        case ioIOI:                                         /* I/O data input */
            stat_data = IORETURN (SCPE_OK, ptr_unit.buf);   /* merge in return status */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            ptr.flag = ptr.flagbuf = SET;               /* set flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            ptr.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            ptr.control = SET;
            sim_activate (&ptr_unit, ptr_unit.wait);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (ptr);                            /* set standard PRL signal */
            setstdIRQ (ptr);                            /* set standard IRQ signal */
            setstdSRQ (ptr);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            ptr.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IOERROR (ptr_stopioe, SCPE_UNATT);
while ((temp = getc (ptr_unit.fileref)) == EOF) {       /* read byte, error? */
    if (feof (ptr_unit.fileref)) {                      /* end of file? */
        if ((ptr_unit.flags & UNIT_DIAG) && (ptr_unit.pos > 0)) {
            rewind (ptr_unit.fileref);                  /* rewind if loop mode */
            ptr_unit.pos = 0;
            }
        else {
            if (ptr_trlcnt >= ptr_trllim) {             /* added all trailer? */
                if (ptr_stopioe) {                      /* stop on error? */
                    printf ("PTR end of file\n");
                    return SCPE_IOERR;
                    }
                else return SCPE_OK;                    /* no, just hang */
                }
            ptr_trlcnt++;                               /* count trailer */
            temp = 0;                                   /* read a zero */
            break;
            }
        }
    else {                                              /* no, real error */
        perror ("PTR I/O error");
        clearerr (ptr_unit.fileref);
        return SCPE_IOERR;
        }
    }

ptrio (&ptr_dib, ioENF, 0);                             /* set flag */

ptr_unit.buf = temp & 0377;                             /* put byte in buf */
ptr_unit.pos = ftell (ptr_unit.fileref);

if (temp)                                               /* character non-null? */
    ptr_trlcnt = 0;                                     /* clear trailing null counter */

return SCPE_OK;
}


/* Attach routine - clear the trailer counter */

t_stat ptr_attach (UNIT *uptr, CONST char *cptr)
{
ptr_trlcnt = 0;
return attach_unit (uptr, cptr);
}


/* Reset routine - called from SCP */

t_stat ptr_reset (DEVICE *dptr)
{
IOPRESET (&ptr_dib);                                    /* PRESET device (does not use PON) */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}


/* Paper tape reader bootstrap routine (HP 12992K ROM) */

const BOOT_ROM ptr_rom = {
    0107700,                    /*ST CLC 0,C            ; intr off */
    0002401,                    /*   CLA,RSS            ; skip in */
    0063756,                    /*CN LDA M11            ; feed frame */
    0006700,                    /*   CLB,CCE            ; set E to rd byte */
    0017742,                    /*   JSB READ           ; get #char */
    0007306,                    /*   CMB,CCE,INB,SZB    ; 2's comp */
    0027713,                    /*   JMP *+5            ; non-zero byte */
    0002006,                    /*   INA,SZA            ; feed frame ctr */
    0027703,                    /*   JMP *-3 */
    0102077,                    /*   HLT 77B            ; stop */
    0027700,                    /*   JMP ST             ; next */
    0077754,                    /*   STA WC             ; word in rec */
    0017742,                    /*   JSB READ           ; get feed frame */
    0017742,                    /*   JSB READ           ; get address */
    0074000,                    /*   STB 0              ; init csum */
    0077755,                    /*   STB AD             ; save addr */
    0067755,                    /*CK LDB AD             ; check addr */
    0047777,                    /*   ADB MAXAD          ; below loader */
    0002040,                    /*   SEZ                ; E =0 => OK */
    0027740,                    /*   JMP H55 */
    0017742,                    /*   JSB READ           ; get word */
    0040001,                    /*   ADA 1              ; cont checksum */
    0177755,                    /*   STA AD,I           ; store word */
    0037755,                    /*   ISZ AD */
    0000040,                    /*   CLE                ; force wd read */
    0037754,                    /*   ISZ WC             ; block done? */
    0027720,                    /*   JMP CK             ; no */
    0017742,                    /*   JSB READ           ; get checksum */
    0054000,                    /*   CPB 0              ; ok? */
    0027702,                    /*   JMP CN             ; next block */
    0102011,                    /*   HLT 11             ; bad csum */
    0027700,                    /*   JMP ST             ; next */
    0102055,                    /*H55 HALT 55           ; bad address */
    0027700,                    /*   JMP ST             ; next */
    0000000,                    /*RD 0 */
    0006600,                    /*   CLB,CME            ; E reg byte ptr */
    0103710,                    /*   STC RDR,C          ; start reader */
    0102310,                    /*   SFS RDR            ; wait */
    0027745,                    /*   JMP *-1 */
    0106410,                    /*   MIB RDR            ; get byte */
    0002041,                    /*   SEZ,RSS            ; E set? */
    0127742,                    /*   JMP RD,I           ; no, done */
    0005767,                    /*   BLF,CLE,BLF        ; shift byte */
    0027744,                    /*   JMP RD+2           ; again */
    0000000,                    /*WC 000000             ; word count */
    0000000,                    /*AD 000000             ; address */
    0177765,                    /*M11 -11               ; feed count */
    0, 0, 0, 0, 0, 0, 0, 0,     /* unused */
    0, 0, 0, 0, 0, 0, 0,        /* unused */
    0000000                     /*MAXAD -ST             ; max addr */
    };

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
const int32 dev = ptr_dib.select_code;                  /* get device no */

if (ibl_copy (ptr_rom, dev, IBL_OPT,                    /* copy the boot ROM to memory and configure */
              IBL_PTR | IBL_SET_SC (dev)))              /*   the S register accordingly */
    return SCPE_IERR;                                   /* return an internal error if the copy failed */
else
    return SCPE_OK;
}


/* Paper tape punch I/O signal handler.

   Implementation notes:

    1. The 12597A duplex register card is used to interface the paper tape
       punch to the computer.  This card has a device command flip-flop, which
       supplies the PUNCH signal to the tape reader.  Under simulation, this
       state is implied by the activation of the PTP unit.
*/

uint32 ptpio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            ptp.flag = ptp.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            ptp.flag = ptp.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (ptp);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (ptp);
            break;


        case ioIOI:                                         /* I/O data input */
            if ((ptp_unit.flags & UNIT_ATT) == 0)           /* not attached? */
                stat_data = IORETURN (SCPE_OK, PTP_LOW);    /* report as out of tape */
            else
                stat_data = IORETURN (SCPE_OK, 0);
            break;


        case ioIOO:                                     /* I/O data output */
            ptp_unit.buf = IODATA (stat_data);          /* clear supplied status */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            ptp.flag = ptp.flagbuf = SET;               /* set flag and flag buffer */
            ptp_unit.buf = 0;                           /* clear output buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            ptp.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            ptp.control = SET;
            sim_activate (&ptp_unit, ptp_unit.wait);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (ptp);                            /* set standard PRL signal */
            setstdIRQ (ptp);                            /* set standard IRQ signal */
            setstdSRQ (ptp);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            ptp.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
ptpio (&ptp_dib, ioENF, 0);                             /* set flag */

if ((ptp_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IOERROR (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {     /* output byte */
    perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ftell (ptp_unit.fileref);                /* update position */
return SCPE_OK;
}


/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
IOPRESET (&ptp_dib);                                    /* PRESET device (does not use PON) */
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}


/* Terminal I/O signal handler */

uint32 ttyio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            tty.flag = tty.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            tty.flag = tty.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (tty);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (tty);
            break;


        case ioIOI:                                     /* I/O data input */
            data = (uint16) tty_buf;

            if (!(tty_mode & TM_KBD) && sim_is_active (&tty_unit[TTO]))
                data = data | TP_BUSY;

            stat_data = IORETURN (SCPE_OK, data);       /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            data = IODATA (stat_data);                  /* clear supplied status */

            if (data & TM_MODE)
                tty_mode = data & (TM_KBD|TM_PRI|TM_PUN);

            tty_buf = data & 0377;
            break;


        case ioCRS:                                     /* control reset */
            tty.control = CLEAR;                        /* clear control */
            tty.flag = tty.flagbuf = SET;               /* set flag and flag buffer */
            tty_mode = TM_KBD;                          /* set tty, clear print/punch */
            tty_shin = 0377;                            /* input inactive */
            tty_lf = 0;                                 /* no lf pending */
            break;


        case ioCLC:                                     /* clear control flip-flop */
            tty.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            tty.control = SET;

            if (!(tty_mode & TM_KBD))                   /* output? */
                sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (tty);                            /* set standard PRL signal */
            setstdIRQ (tty);                            /* set standard IRQ signal */
            setstdSRQ (tty);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            tty.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* TTY input service routine.

   The console input poll routine is scheduled with a ten millisecond period
   using a calibrated timer, which is the source of event timing for all of the
   keyboard polling routines.  Synchronizing other keyboard polls with the
   console poll ensures maximum idle time.

   Several HP operating systems require a CR and LF sequence for line
   termination.  This is awkward on a PC, as there is no LF key (CTRL+J is
   needed instead).  We provide an AUTOLF mode to add a LF automatically to each
   CR input.  When this mode is set, entering CR will set a flag, which will
   cause a LF to be supplied automatically at the next input poll.

   The 12531C teleprinter interface and the later 12880A CRT interface provide a
   clever mechanism to detect a keypress during output.  This is used by DOS and
   RTE to allow the user to interrupt lengthy output operations to enter system
   commands.

   Referring to the 12531C schematic, the terminal input enters on pin X
   ("DATA FROM EIA COMPATIBLE DEVICE").  The signal passes through four
   transistor inversions (Q8, Q1, Q2, and Q3) to appear on pin 12 of NAND gate
   U104C.  If the flag flip-flop is not set, the terminal input passes to the
   (inverted) output of U104C and thence to the D input of the first of the
   flip-flops forming the data register.

   In the idle condition (no key pressed), the terminal input line is marking
   (voltage negative), so in passing through a total of five inversions, a
   logic one is presented at the serial input of the data register.  During an
   output operation, the register is parallel loaded and serially shifted,
   sending the output data through the register to the device and -- this is
   the crux -- filling the register with logic ones from U104C.

   At the end of the output operation, the card flag is set, an interrupt
   occurs, and the RTE driver is entered.  The driver then does an LIA SC to
   read the contents of the data register.  If no key has been pressed during
   the output operation, the register will read as all ones (octal 377).  If,
   however, any key was struck, at least one zero bit will be present.  If the
   register value doesn't equal 377, the driver sets the system "operator
   attention" flag, which will cause DOS or RTE to output an asterisk prompt and
   initiate a terminal read when the current output line is completed.
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

uptr->wait = sim_rtcn_calb (POLL_RATE, TMR_POLL);       /* calibrate poll timer */
sim_activate (uptr, uptr->wait);                        /* continue poll */

tty_shin = 0377;                                        /* assume inactive */
if (tty_lf) {                                           /* auto lf pending? */
    c = 012;                                            /* force lf */
    tty_lf = 0;
    }
else {
    if ((c = sim_poll_kbd ()) < SCPE_KFLAG) return c;   /* no char or error? */
    if (c & SCPE_BREAK) c = 0;                          /* break? */
    else c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
    tty_lf = ((c & 0177) == 015) && (uptr->flags & UNIT_AUTOLF);
    }
if (tty_mode & TM_KBD) {                                /* keyboard enabled? */
    tty_buf = c;                                        /* put char in buf */
    uptr->pos = uptr->pos + 1;

    ttyio (&tty_dib, ioENF, 0);                         /* set flag */

    if (c) {
        tto_out (c);                                    /* echo? */
        return ttp_out (c);                             /* punch? */
        }
    }
else tty_shin = c;                                      /* no, char shifts in */
return SCPE_OK;
}


/* TTY output service routine */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = tty_buf;                                            /* get char */
tty_buf = tty_shin;                                     /* shift in */
tty_shin = 0377;                                        /* line inactive */
if ((r = tto_out (c)) != SCPE_OK) {                     /* output; error? */
    sim_activate (uptr, uptr->wait);                    /* retry */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }

ttyio (&tty_dib, ioENF, 0);                             /* set flag */

return ttp_out (c);                                     /* punch if enabled */
}


t_stat tto_out (int32 c)
{
t_stat r;

if (tty_mode & TM_PRI) {                                /* printing? */
    c = sim_tt_outcvt (c, TT_GET_MODE (tty_unit[TTO].flags));
    if (c >= 0) {                                       /* valid? */
        r = sim_putchar_s (c);                          /* output char */
        if (r != SCPE_OK)
            return r;
        tty_unit[TTO].pos = tty_unit[TTO].pos + 1;
        }
    }
return SCPE_OK;
}


t_stat ttp_out (int32 c)
{
if (tty_mode & TM_PUN) {                                /* punching? */
    if ((tty_unit[TTP].flags & UNIT_ATT) == 0)          /* attached? */
        return IOERROR (ttp_stopioe, SCPE_UNATT);
    if (putc (c, tty_unit[TTP].fileref) == EOF) {       /* output char */
        perror ("TTP I/O error");
        clearerr (tty_unit[TTP].fileref);
        return SCPE_IOERR;
        }
    tty_unit[TTP].pos = ftell (tty_unit[TTP].fileref);
    }
return SCPE_OK;
}


/* TTY reset routine */

t_stat tty_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P'))                        /* initialization reset? */
    tty_buf = 0;                                        /* clear buffer */

IOPRESET (&tty_dib);                                    /* PRESET device (does not use PON) */

tty_unit[TTI].wait = POLL_WAIT;                         /* reset initial poll */
sim_rtcn_init (tty_unit[TTI].wait, TMR_POLL);           /* init poll timer */
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);      /* activate poll */
sim_cancel (&tty_unit[TTO]);                            /* cancel output */
return SCPE_OK;
}


t_stat tty_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 u = uptr - tty_unit;

if (u > TTO) return SCPE_NOFNC;
if ((u == TTI) && (val == TT_MODE_7P))
    val = TT_MODE_7B;
tty_unit[u].flags = (tty_unit[u].flags & ~TT_MODE) | val;
return SCPE_OK;
}


t_stat tty_set_alf (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 u = uptr - tty_unit;

if (u != TTI) return SCPE_NOFNC;
return SCPE_OK;
}


/* Synchronize polling.

   Return an event time corresponding either with the amount of time remaining
   in the current poll (mode = INITIAL) or the amount of time in a full poll
   period (mode = SERVICE).  If the former call is made when the device service
   routine is started, then making the latter call during unit service will
   ensure that the polls remain synchronized.
 */

int32 sync_poll (POLLMODE poll_mode)
{
int32 poll_time;

    if (poll_mode == INITIAL) {
        poll_time = sim_activate_time (&tty_unit[TTI]);

        if (poll_time)
            return poll_time;
        else
            return POLL_WAIT;
        }
    else
        return tty_unit[TTI].wait;
}


/* Clock I/O signal handler.

   The time base generator (CLK) provides periodic interrupts from 100
   microseconds to 1000 seconds.  The CLK uses a calibrated timer to provide the
   time base.  For periods ranging from 1 to 1000 seconds, a 100 millisecond
   timer is used, and 10 to 10000 ticks are counted before setting the device
   flag to indicate that the period has expired.

   If the period is set to ten milliseconds, the console poll timer is used
   instead of an independent timer.  This is to maximize the idle period.

   In diagnostic mode, the clock period is set to the expected number of CPU
   instructions, rather than wall-clock time, so that the diagnostic executes as
   expected.
*/

uint32 clkio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            clk.flag = clk.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            clk.flag = clk.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (clk);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (clk);
            break;


        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, clk_error);  /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            clk_select = IODATA (stat_data) & 07;       /* save select */
            sim_cancel (&clk_unit);                     /* stop the clock */
            clk.control = CLEAR;                        /* clear control */
            working_set = working_set | ioSIR;          /* set interrupt request (IOO normally doesn't) */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            clk.flag = clk.flagbuf = SET;               /* set flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            clk.control = CLEAR;
            sim_cancel (&clk_unit);                     /* deactivate unit */
            break;


        case ioSTC:                                             /* set control flip-flop */
            clk.control = SET;
            if (clk_unit.flags & UNIT_DIAG)                     /* diag mode? */
                clk_unit.flags = clk_unit.flags & ~UNIT_IDLE;   /* not calibrated */
            else
                clk_unit.flags = clk_unit.flags | UNIT_IDLE;    /* is calibrated */

            if (!sim_is_active (&clk_unit)) {                   /* clock running? */
                clk_tick = clk_delay (0);                       /* get tick count */

                if ((clk_unit.flags & UNIT_DIAG) == 0)          /* calibrated? */
                    if (clk_select == 2)                        /* 10 msec. interval? */
                        clk_tick = sync_poll (INITIAL);         /* sync poll */
                    else
                        sim_rtcn_init (clk_tick, TMR_CLK);      /* initialize timer */

                sim_activate (&clk_unit, clk_tick);             /* start clock */
                clk_ctr = clk_delay (1);                        /* set repeat ctr */
                }
            clk_error = 0;                                      /* clear error */
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (clk);                            /* set standard PRL signal */
            setstdIRQ (clk);                            /* set standard IRQ signal */
            setstdSRQ (clk);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            clk.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* CLK unit service.

   As with the I/O handler, if the time base period is set to ten milliseconds,
   the console poll timer is used instead of an independent timer.
*/

t_stat clk_svc (UNIT *uptr)
{
if (!clk.control)                                       /* control clear? */
    return SCPE_OK;                                     /* done */

if (clk_unit.flags & UNIT_DIAG)                         /* diag mode? */
    clk_tick = clk_delay (0);                           /* get fixed delay */
else if (clk_select == 2)                               /* 10 msec period? */
    clk_tick = sync_poll (SERVICE);                     /* sync poll */
else
    clk_tick = sim_rtcn_calb (clk_tps[clk_select], TMR_CLK);    /* calibrate delay */

sim_activate (uptr, clk_tick);                          /* reactivate */
clk_ctr = clk_ctr - 1;                                  /* decrement counter */
if (clk_ctr <= 0) {                                     /* end of interval? */
    if (clk.flag)
        clk_error = CLK_ERROR;                          /* overrun? error */
    else
        clkio (&clk_dib, ioENF, 0);                     /* set flag */
    clk_ctr = clk_delay (1);                            /* reset counter */
    }
return SCPE_OK;
}


/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    clk_error = 0;                                      /* clear error */
    clk_select = 0;                                     /* clear select */
    clk_ctr = 0;                                        /* clear counter */

    if (clk_dev.lname == NULL)                          /* logical name unassigned? */
        clk_dev.lname = strdup ("TBG");                 /* allocate and initialize the name */
    }

IOPRESET (&clk_dib);                                    /* PRESET device (does not use PON) */

return SCPE_OK;
}


/* Clock delay routine */

int32 clk_delay (int32 flg)
{
int32 sel = clk_select;

if ((clk_unit.flags & UNIT_DIAG) && (sel >= 4)) sel = sel - 3;
if (flg) return clk_rpt[sel];
else return clk_time[sel];
}
