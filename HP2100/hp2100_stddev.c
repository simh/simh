/* hp2100_stddev.c: HP2100 standard devices simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017       J. David Bryan

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

   PTR          12597A-002 paper tape reader interface
   PTP          12597A-005 paper tape punch interface
   TTY          12531C buffered teleprinter interface
   TBG          12539C time base generator

   03-Aug-17    JDB     PTP and TTY now append to existing file data
   18-Jul-17    JDB     The PTR device now handles the IOERR simulation stop
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   01-May-17    JDB     Deleted ttp_stopioe, as a detached punch is no longer an error
   08-Mar-17    JDB     Added REALTIME, W1A, W1B, W2A, and W2B options to the TBG
                        Replaced IPTICK with a CPU speed calculation
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   23-Feb-17    JDB     Modified ptr_boot to use IBL_S_CLR to clear the S register
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   30-Dec-16    JDB     Modified the TTY to print if the punch is not attached
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
     - 2748B Tape Reader Operating and Service Manual
         (02748-90041, October 1977)
     - 12597A 8-Bit Duplex Register Interface Kit Operating and Service Manual
         (12597-9002, September 1974)
     - 12531C Buffered Teleprinter Interface Kit Operating and Service Manual
         (12531-90033, November 1972)
     - 12539C Time Base Generator Interface Kit Operating and Service Manual
         (12539-90008, January 1975)


   The HP 2748B Paper Tape Reader connects to the CPU via the 12597A 8-Bit
   Duplex Register.  The interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   An IOO signal clocks the lower eight bits into the output register, but the
   output lines are not connected to the tape reader.


   Input Data Word format (LIA and LIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - |           tape data           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The presence of a feed hole clocks the data byte into the input register.  An
   IOI signal enables the input register to the I/O Data Bus.


   Boot Loader ROM S-Register format (12992K):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   0 |      select code      | 0   0   0   0   0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The tape format must be absolute binary.  Loader execution ends with one of
   the following instructions:

     HLT 11B - checksum error (A = calculated, B = expected)
     HLT 55B - load address >= ROM loader address
     HLT 77B - end of tape with successful read

   Reader diagnostic mode simulates a tape loop by rewinding the tape image file
   upon EOF.  Normal mode EOF action is to supply TRLLIM nulls and then either
   return SCPE_IOERR or SCPE_OK without setting the device flag.



   The HP 2895B Paper Tape Punch connects to the CPU via the 12597A 8-Bit Duplex
   Register.  The interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - |           tape data           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   An IOO signal clocks the lower eight bits into the output register.  The data
   is punched when the STC signal sets the command flip-flop, which asserts the
   PUNCH signal to the tape punch.


   Input Data Word format (LIA and LIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   - | L | -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     L = Tape Supply is Low

   Pin 21 of the interface connector is grounded, so the input register is
   transparent, and bit 5 reflects the current state of the the tape low signal.
   An IOI signal enables the input register to the I/O Data Bus.



   The HP 2752A and 2754A Teleprinters are connected to the CPU via the HP
   12531C Teleprinter interface.  The interface responds to I/O instructions as
   follows:

   Output Data Word format (OTA and OTB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | I | P | N | -   -   -   -   -   -   -   -   -   -   -   - | control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   - |       output character        | data
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = set the interface to output/input mode (0/1)
     P = enable the printer for output
     N = enable the punch for output


   Input Data Word format (LIA and LIB):

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | B | -   -   -   -   -   -   - |        input character        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = interface is idle/busy (0/1)

   To support CPU idling, the teleprinter interface (which doubles as the
   simulator console) polls for input using a calibrated timer with a ten
   millisecond period.  Other polled-keyboard input devices (multiplexers and
   the BACI card) synchronize with the console poll to ensure maximum available
   idle time.  The console poll is guaranteed to run, as the TTY device cannot
   be disabled.



   The time base generator interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   - | tick rate |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Tick Rate Selection:

     000 = 100 microseconds
     001 = 1 millisecond
     010 = 10 milliseconds
     011 = 100 milliseconds
     100 = 1 second
     101 = 10 seconds
     110 = 100 seconds
     111 = 1000 seconds

   If jumper W2 is in position B, the last four rates are divided by 1000,
   producing rates of 1, 10, 100, and 1000 milliseconds, respectively.


   Input Data Word format (LIA, LIB, MIA, and MIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   - | E | -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     E = At least one tick has been lost

   If jumper W1 is in position B, bit 5 also indicates a lost tick.


   In hardware, the two configuration jumpers perform these functions:

     Jumper  Interpretation in position A  Interpretation in position B
     ------  ----------------------------  ---------------------------------
       W1    Input bit 5 is always zero    Input bit 5 indicates a lost tick

       W2    Last four rates are seconds   Last four rates are milliseconds

   The time base generator autocalibrates.  If the TBG is set to a ten
   millisecond period (e.g., as under RTE), it is synchronized to the console
   poll.  Otherwise (e.g., as under DOS or TSB, which use 100 millisecond
   periods), it runs asynchronously.  If the specified clock frequency is below
   10Hz, the clock service routine runs at 10Hz and counts down a repeat counter
   before generating an interrupt.  Autocalibration will not work if the clock
   is running at 1Hz or less.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"



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

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } ptr = { CLEAR, CLEAR, CLEAR };

int32 ptr_trlcnt = 0;                                   /* trailer counter */
int32 ptr_trllim = 40;                                  /* trailer to add */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } ptp = { CLEAR, CLEAR, CLEAR };

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } tty = { CLEAR, CLEAR, CLEAR };

int32 tty_buf = 0;                                      /* tty buffer */
int32 tty_mode = 0;                                     /* tty mode */
int32 tty_shin = 0377;                                  /* tty shift in */
int32 tty_lf = 0;                                       /* lf flag */

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
    { ORDATA (SC, ptr_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, ptr_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB ptr_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
    { UNIT_DIAG, 0, "reader mode", "READER", NULL },
    { MTAB_XTD | MTAB_VDV,             1u, "SC",    "SC",    &hp_set_dib, &hp_show_dib, (void *) &ptr_dib },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, ~1u, "DEVNO", "DEVNO", &hp_set_dib, &hp_show_dib, (void *) &ptr_dib },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR",                                      /* device name */
    &ptr_unit,                                  /* unit array */
    ptr_reg,                                    /* register array */
    ptr_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ptr_reset,                                 /* reset routine */
    &ptr_boot,                                  /* boot routine */
    &ptr_attach,                                /* attach routine */
    NULL,                                       /* detach routine */
    &ptr_dib,                                   /* device information block pointer */
    DEV_DISABLE,                                /* device flags */
    0,                                          /* debug control flags */
    NULL,                                       /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
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
    { ORDATA (SC, ptp_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, ptp_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD | MTAB_VDV,             1u, "SC",    "SC",    &hp_set_dib, &hp_show_dib, (void *) &ptp_dib },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, ~1u, "DEVNO", "DEVNO", &hp_set_dib, &hp_show_dib, (void *) &ptp_dib },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP",                                      /* device name */
    &ptp_unit,                                  /* unit array */
    ptp_reg,                                    /* register array */
    ptp_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ptp_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &hp_attach,                                 /* attach routine */
    NULL,                                       /* detach routine */
    &ptp_dib,                                   /* device information block pointer */
    DEV_DISABLE,                                /* device flags */
    0,                                          /* debug control flags */
    NULL,                                       /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
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
    { UDATA (&tti_svc, UNIT_IDLE | TT_MODE_UC, 0), POLL_PERIOD },
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
    { MTAB_XTD | MTAB_VDV,             1u, "SC",    "SC",    &hp_set_dib, &hp_show_dib, (void *) &tty_dib },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, ~1u, "DEVNO", "DEVNO", &hp_set_dib, &hp_show_dib, (void *) &tty_dib },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY",                                      /* device name */
    tty_unit,                                   /* unit array */
    tty_reg,                                    /* register array */
    tty_mod,                                    /* modifier array */
    3,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &tty_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &hp_attach,                                 /* attach routine */
    NULL,                                       /* detach routine */
    &tty_dib,                                   /* device information block pointer */
    0,                                          /* device flags */
    0,                                          /* debug control flags */
    NULL,                                       /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
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
int byte;

if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* if the reader is not attached */
    if (cpu_ss_ioerr != SCPE_OK) {                      /*   then if the I/O error stop is enabled */
        sim_activate (uptr, uptr->wait);                /*     then reschedule the operation */

        cpu_ioerr_uptr = uptr;                          /* save the failing unit */
        return STOP_NOTAPE;                             /*   and report that the tape isn't loaded */
        }

    else                                                /* otherwise no tape in the reader */
        return SCPE_OK;                                 /*   just hangs the input operation */

byte = fgetc (uptr->fileref);                           /* get the next byte from the paper tape file */

if (feof (uptr->fileref))                               /* if the file is positioned at the EOF */
    if (uptr->flags & UNIT_DIAG && uptr->pos > 0) {     /*   then if DIAG mode is enabled and the tape isn't empty */
        rewind (uptr->fileref);                         /*     then rewind the tape */
        uptr->pos = 0;                                  /*       to simulate loop mode */

        byte = fgetc (uptr->fileref);                   /* get the first byte from the tape */
        }

    else                                                /* otherwise READER mode is enabled or the tape is empty */
        if (ptr_trlcnt < ptr_trllim) {                  /*   so if trailer remains to be added */
            ptr_trlcnt++;                               /*     then count the trailer byte */
            byte = 0;                                   /*       and return a NUL */
            }

        else if (cpu_ss_ioerr != SCPE_OK) {             /* otherwise trailer is complete; if the I/O stop is enabled */
            sim_activate (uptr, uptr->wait);            /*   then reschedule the operation */

            cpu_ioerr_uptr = uptr;                      /* save the failing unit */
            return STOP_EOT;                            /*   and report that the tape is at EOF */
            }

        else                                            /* otherwise tape exhaustion */
            return SCPE_OK;                             /*   just hangs the input operation */

if (ferror (uptr->fileref)) {                                   /* if a host file I/O error occurred */
    cprintf ("%s simulator paper tape reader I/O error: %s\n",  /*   then report the error to the console */
             sim_name, strerror (errno));

    clearerr (uptr->fileref);                           /* clear the error */
    return SCPE_IOERR;                                  /*   and stop the simulator */
    }

else {                                                  /* otherwise the read was successful */
    uptr->buf = LOWER_BYTE (byte);                      /*   so put the byte in the buffer */
    uptr->pos = ftell (uptr->fileref);                  /*     and update the file position */

    if (byte != 0)                                      /* if the byte is not a NUL */
        ptr_trlcnt = 0;                                 /*   then clear the trailing NUL counter */

    ptrio (&ptr_dib, ioENF, 0);                         /* set the device flag */
    return SCPE_OK;                                     /*   and return success */
    }
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

cpu_ibl (ptr_rom, dev, IBL_S_CLR,                       /* copy the boot ROM to memory and configure */
         IBL_PTR | IBL_SET_SC (dev));                   /*   the S register accordingly */

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
if (uptr->flags & UNIT_ATT)                                         /* if the punch is attached */
    if (fputc (uptr->buf, uptr->fileref) == EOF) {                  /*   then write the byte; if the write fails */
        cprintf ("%s simulator paper tape punch I/O error: %s\n",   /*     then report the error to the console */
                 sim_name, strerror (errno));

        clearerr (uptr->fileref);                       /* clear the error */
        return SCPE_IOERR;                              /*   and stop the simulator */
        }

    else {                                              /* otherwise the write succeeds */
        uptr->pos = ftell (uptr->fileref);              /*   so update the file position */
        ptpio (&ptp_dib, ioENF, 0);                     /*     and set the device flag */
        }

return SCPE_OK;
}


/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
IOPRESET (&ptp_dib);                                    /* PRESET device (does not use PON) */
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}


/* Terminal I/O signal handler.

   Output Word Format:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | I | P | N | -   -   -   -   -   -   -   -   -   -   -   - | control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   - |       output character        | data
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = set the interface to output/input mode (0/1)
     P = enable the printer for output
     N = enable the punch for output


   Input Word Format:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | B | -   -   -   -   -   -   - |        input character        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = interface is idle/busy (0/1)

*/

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


   Implementation notes:

    1. The current CPU speed, expressed as a multiple of the hardware speed, is
       calculated for each service entry.  It may be displayed at the SCP prompt
       with the SHOW CPU SPEED command.  The speed is only representative when
       the CPU is not idling.
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

uptr->wait = sim_rtcn_calb (POLL_RATE, TMR_POLL);       /* calibrate poll timer */
sim_activate (uptr, uptr->wait);                        /* continue poll */

cpu_speed = uptr->wait / POLL_PERIOD;                   /* calculate the current CPU speed multiplier */

tty_shin = 0377;                                        /* assume inactive */

if (tty_lf) {                                           /* auto lf pending? */
    c = 012;                                            /* force lf */
    tty_lf = 0;
    }

else {
    c = sim_poll_kbd ();

    if (c < SCPE_KFLAG)                                 /* no char or error? */
        return c;

    if (c & SCPE_BREAK)                                 /* break? */
        c = 0;
    else
        c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));

    tty_lf = ((c & 0177) == 015) && (uptr->flags & UNIT_AUTOLF);
    }

if (tty_mode & TM_KBD) {                                /* keyboard enabled? */
    tty_buf = c;                                        /* put char in buf */
    uptr->pos = uptr->pos + 1;

    ttyio (&tty_dib, ioENF, 0);                         /* set flag */

    if (c)
        tto_out (c);                                    /* echo? */
    }

else                                                    /* no, char shifts in */
    tty_shin = c;

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

r = tto_out (c);                                        /* output the character */

if (r != SCPE_OK) {                                     /* if an error occurred */
    sim_activate (uptr, uptr->wait);                    /*   then schedule a retry */
    return (r == SCPE_STALL ? SCPE_OK : r);             /* report a stall as success */
    }

ttyio (&tty_dib, ioENF, 0);                             /* set flag */

return SCPE_OK;
}


/* TTY output routine.

   The 12531C Buffered Teleprinter Interface connects current-loop devices, such
   as the HP 2752A (ASR33) and 2754A (ASR35) teleprinters, as well as EIA RS-232
   devices, such as the HP 2749A (ASR33) teleprinter and HP 2600 terminal.  For
   output, the control word sent to the interface may set the print flip-flop,
   the punch flip-flop, or both flip-flops.  These flip-flops generate the PRINT
   COMMAND and PUNCH COMMAND output signals, respectively.  Setting either one
   enables data transmission.

   Only the 2754A responds to the PRINT and PUNCH COMMAND signals.  All of the
   other devices ignore these signals and respond only to the serial data out
   signal.  (The paper tape punches on the 2749A and 2752A teleprinters must be
   enabled manually at the console and operate concurrently with the printers.)

   This routine simulates a 2754A if the punch unit (TTY unit 2) is attached and
   a generic terminal when the unit is detached.  With the punch unit attached,
   the punch flip-flop must be set to punch, and the print flip-flop must be set
   to print.  These flip-flops, and therefore their respective operations, are
   independent.  When the punch unit is detached, printing will occur if either
   the print or punch flip-flop is set.  If neither flip-flop is set, no output
   occurs.  Therefore, the logic is:

     if punch-flip-flop and punch-attached
       then punch character

     if print-flip-flop or punch-flip-flop and not punch-attached
       then print character

   Certain HP programs, e.g., HP 2000F BASIC FOR DOS-M/DOS III, depend on the
   2752A et. al. behavior.  The DOS and RTE teleprinter drivers support text and
   binary output modes.  Text mode sets the print flip-flop, and binary mode
   sets the punch flip-flop.  These programs use binary mode to write single
   characters to the teleprinter and expect that they will be printed.  The
   simulator follows this behavior.
*/

t_stat tto_out (int32 c)
{
t_stat r = SCPE_OK;

if (tty_mode & TM_PUN                                               /* if punching is enabled */
  && tty_unit [TTP].flags & UNIT_ATT)                               /*   and the punch is attached */
    if (fputc (c, tty_unit [TTP].fileref) == EOF) {                 /*     then write the byte; if the write fails */
        cprintf ("%s simulator teleprinter punch I/O error: %s\n",  /*       then report the error to the console */
                 sim_name, strerror (errno));

        clearerr (tty_unit [TTP].fileref);                      /* clear the error */
        r = SCPE_IOERR;                                         /*   and stop the simulator */
        }

    else                                                        /* otherwise the output succeeded */
        tty_unit [TTP].pos = ftell (tty_unit [TTP].fileref);    /*   so update the file position */

if (tty_mode & TM_PRI                                           /* if printing is enabled */
  || tty_mode & TM_PUN                                          /*   or punching is enabled */
  && (tty_unit [TTP].flags & UNIT_ATT) == 0) {                  /*     and the punch is not attached */
    c = sim_tt_outcvt (c, TT_GET_MODE (tty_unit [TTO].flags));  /*       then convert the character */

    if (c >= 0) {                                           /* if the character is valid */
        r = sim_putchar_s (c);                              /*   then output it to the console */

        if (r == SCPE_OK)                                   /* if the output succeeded */
            tty_unit [TTO].pos = tty_unit [TTO].pos + 1;    /*   then update the file position */
        }
    }

return r;                                               /* return the result */
}


/* TTY reset routine */

t_stat tty_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P'))                        /* initialization reset? */
    tty_buf = 0;                                        /* clear buffer */

IOPRESET (&tty_dib);                                    /* PRESET device (does not use PON) */

tty_unit[TTI].wait = POLL_PERIOD;                       /* reset initial poll */
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
            return POLL_PERIOD;
        }
    else
        return tty_unit[TTI].wait;
}



/* 12539C Time Base Generator *************************************************/


/* Program constants */

static const int32 delay [8] = {                /* clock delays, in event ticks per interval */
    uS (100),                                   /*   000 = 100 microseconds */
    mS (1),                                     /*   001 = 1 millisecond */
    mS (10),                                    /*   010 = 10 milliseconds */
    mS (100),                                   /*   011 = 100 milliseconds */
    S (1),                                      /*   100 = 1 second */
    S (10),                                     /*   101 = 10 seconds */
    S (100),                                    /*   110 = 100 seconds */
    S (1000)                                    /*   111 = 1000 seconds */
    };

static const int32 ticks [8] = {                /* clock ticks per second */
    10000,                                      /*   000 = 100 microseconds */
    1000,                                       /*   001 = 1 millisecond */
    100,                                        /*   010 = 10 milliseconds */
    10,                                         /*   011 = 100 milliseconds */
    10,                                         /*   100 = 1 second */
    10,                                         /*   101 = 10 seconds */
    10,                                         /*   110 = 100 seconds */
    10                                          /*   111 = 1000 seconds */
    };

static const int32 scale [8] = {                /* prescaler counts per clock tick */
    1,                                          /*   000 = 100 microseconds */
    1,                                          /*   001 = 1 millisecond */
    1,                                          /*   010 = 10 milliseconds */
    1,                                          /*   011 = 100 milliseconds */
    10,                                         /*   100 = 1 second */
    100,                                        /*   101 = 10 seconds */
    1000,                                       /*   110 = 100 seconds */
    10000                                       /*   111 = 1000 seconds */
    };


/* Unit flags */

#define UNIT_CALTIME_SHIFT  (UNIT_V_UF + 0)     /* calibrated timing mode */
#define UNIT_W1B_SHIFT      (UNIT_V_UF + 1)     /* jumper W1 in position B */
#define UNIT_W2B_SHIFT      (UNIT_V_UF + 2)     /* jumper W2 in position B */

#define UNIT_CALTIME        (1u << UNIT_CALTIME_SHIFT)
#define UNIT_W1B            (1u << UNIT_W1B_SHIFT)
#define UNIT_W2B            (1u << UNIT_W2B_SHIFT)


/* Control word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   -   -   - | tick rate |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_RATE_MASK        0000007u            /* clock rate selector mask */

#define CN_RATE_SHIFT       0                   /* clock rate alignment shift */

#define CN_RATE(c)          (((c) & CN_RATE_MASK) >> CN_RATE_SHIFT)

static const char *const rate_name [8] = {      /* clock rate selector names */
    "100 microsecond",                          /*   000 = 100 microseconds */
    "1 millisecond",                            /*   001 = 1 millisecond */
    "10 millisecond",                           /*   010 = 10 milliseconds */
    "100 millisecond",                          /*   011 = 100 milliseconds */
    "1 second",                                 /*   100 = 1 second */
    "10 second",                                /*   101 = 10 seconds */
    "100 second",                               /*   110 = 100 seconds */
    "1000 second"                               /*   111 = 1000 seconds */
    };


/* Status word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   -   -   -   - | E | -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_ERROR            0000020u            /* lost tick error */
#define ST_ERROR_W1B        0000040u            /* lost tick error if W1 in position B */

static const BITSET_NAME status_names [] = {    /* Status word names */
    "lost tick"                                 /*   bit  4 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 4, msb_first, no_alt, no_bar) };


/* Time Base Generator state */

static struct {
    FLIP_FLOP control;                          /* control flip-flop */
    FLIP_FLOP flag;                             /* flag flip-flop */
    FLIP_FLOP flagbuf;                          /* flag buffer flip-flop */
    } clk = { CLEAR, CLEAR, CLEAR };

static int32     clk_select = 0;                /* clock time select */
static int32     clk_ctr    = 0;                /* clock counter */
static FLIP_FLOP lost_tick  = CLEAR;            /* lost tick error flip-flop */


/* Time Base Generator local SCP support routines */

static IOHANDLER clk_interface;
static t_stat    clk_service (UNIT *uptr);
static t_stat    clk_reset   (DEVICE *dptr);


/* Time Base Generator local utility routines */

static int32 clk_delay (int32 flg);


/* Time Base Generator SCP interface data structures */


/* Device information block */

DIB clk_dib = {
    &clk_interface,                             /* device interface */
    CLK,                                        /* select code */
    0                                           /* card index */
    };

/* Unit list */

static UNIT clk_unit [] = {
    { UDATA (&clk_service, UNIT_IDLE | UNIT_CALTIME, 0) }
    };

/* Register list */

static REG clk_reg [] = {
/*    Macro   Name    Location             Width  Offset  Flags   */
/*    ------  ------  -------------------  -----  ------  ------- */
    { ORDATA (SEL,    clk_select,            3)                   },
    { DRDATA (CTR,    clk_ctr,              14)                   },
    { FLDATA (CTL,    clk.control,                  0)            },
    { FLDATA (FLG,    clk.flag,                     0)            },
    { FLDATA (FBF,    clk.flagbuf,                  0)            },
    { FLDATA (ERR,    lost_tick,                    0)            },
    { ORDATA (SC,     clk_dib.select_code,   6),          REG_HRO },
    { ORDATA (DEVNO,  clk_dib.select_code,   6),          REG_HRO },
    { NULL }
    };

/* Modifier list */

static MTAB clk_mod [] = {
/*    Mask Value    Match Value   Print String         Match String  Validation  Display  Descriptor */
/*    ------------  ------------  -------------------  ------------  ----------  -------  ---------- */
    { UNIT_CALTIME, UNIT_CALTIME, "calibrated timing", "CALTIME",    NULL,       NULL,    NULL       },
    { UNIT_CALTIME, 0,            "realistic timing",  "REALTIME",   NULL,       NULL,    NULL       },
    { UNIT_W1B,     UNIT_W1B,     "W1 position B",     "W1B",        NULL,       NULL,    NULL       },
    { UNIT_W1B,     0,            "W1 position A",     "W1A",        NULL,       NULL,    NULL       },
    { UNIT_W2B,     UNIT_W2B,     "W2 position B",     "W2B",        NULL,       NULL,    NULL       },
    { UNIT_W2B,     0,            "W2 position A",     "W2A",        NULL,       NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation    Display        Descriptor        */
/*    -------------------  -----  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,             1u,    "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &clk_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,    "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &clk_dib },

    { 0 }
    };

/* Debugging trace list */

static DEBTAB clk_deb [] = {
    { "CSRW",  TRACE_CSRW  },                   /* interface control, status, read, and write actions */
    { "PSERV", TRACE_PSERV },                   /* clock unit service scheduling calls */
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };

/* Device descriptor */

DEVICE clk_dev = {
    "CLK",                                      /* device name */
    clk_unit,                                   /* unit array */
    clk_reg,                                    /* register array */
    clk_mod,                                    /* modifier array */
    1,                                          /* number of units */
    0,                                          /* address radix */
    0,                                          /* address width */
    0,                                          /* address increment */
    0,                                          /* data radix */
    0,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &clk_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &clk_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    clk_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Time Base Generator local SCP support routines */


/* Time Base Generator interface.

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

static uint32 clk_interface (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16   status;
int32    tick_count;
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
            if (lost_tick == SET) {                     /* if the lost-tick flip-flop is set */
                status = ST_ERROR;                      /*   then indicate an error */

                if (clk_unit [0].flags & UNIT_W1B)      /* if W1 is in position B */
                    status |= ST_ERROR_W1B;             /*   then set the status in bit 5 as well */
                }

            else                                        /* otherwise the error flip-flop is clear */
                status = 0;                             /*   so clear the error status */

            stat_data = IORETURN (SCPE_OK, status);     /* merge in the return status */

            tprintf (clk_dev, TRACE_CSRW, "Status is %s\n",
                     fmt_bitset (status, status_format));
            break;


        case ioIOO:                                     /* I/O data output */
            clk_select = CN_RATE (IODATA (stat_data));  /* save select */
            sim_cancel (&clk_unit [0]);                 /* stop the clock */
            clk.control = CLEAR;                        /* clear control */
            working_set = working_set | ioSIR;          /* set interrupt request (IOO normally doesn't) */

            tprintf (clk_dev, TRACE_CSRW, "Control is %s rate\n",
                     rate_name [clk_select]);
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            clk.flag = clk.flagbuf = SET;               /* set flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            clk.control = CLEAR;
            sim_cancel (&clk_unit [0]);                 /* deactivate unit */
            break;


        case ioSTC:                                     /* set control flip-flop */
            clk.control = SET;

            if (!sim_is_active (&clk_unit [0])) {               /* clock running? */
                tick_count = clk_delay (0);                     /* get tick count */

                if (clk_unit [0].flags & UNIT_CALTIME)          /* calibrated? */
                    if (clk_select == 2)                        /* 10 msec. interval? */
                        tick_count = sync_poll (INITIAL);       /* sync poll */
                    else
                        sim_rtcn_init (tick_count, TMR_CLK);    /* initialize timer */

                tprintf (clk_dev, TRACE_PSERV, "Rate %s delay %d service rescheduled\n",
                         rate_name [clk_select], tick_count);

                sim_activate (&clk_unit [0], tick_count);       /* start clock */
                clk_ctr = clk_delay (1);                        /* set repeat ctr */
                }

            lost_tick = CLEAR;                                  /* clear error */
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


   Implementation notes:

    1. If the TBG is calibrated, it is synchronized with the TTY keyboard poll
       service to permit idling.
*/

static t_stat clk_service (UNIT *uptr)
{
int32 tick_count;

tprintf (clk_dev, TRACE_PSERV, "Service entered with prescaler %d\n",
         clk_ctr);

if (clk.control == CLEAR)                               /* control clear? */
    return SCPE_OK;                                     /* done */

if (clk_unit [0].flags & UNIT_CALTIME)                  /* cal mode? */
    if (clk_select == 2)                                /* 10 msec period? */
        tick_count = sync_poll (SERVICE);               /* sync poll */
    else
        tick_count = sim_rtcn_calb (ticks [clk_select], /* calibrate delay */
                                    TMR_CLK);

else                                                    /* otherwise the TBG is in real-time mode */
    tick_count = clk_delay (0);                         /* get fixed delay */

clk_ctr = clk_ctr - 1;                                  /* decrement counter */

if (clk_ctr <= 0) {                                     /* end of interval? */
    if (clk.flag) {
        lost_tick = SET;                                /* overrun? error */

        tprintf (clk_dev, TRACE_PSERV, "Clock tick lost\n");
        }

    else
        clk_interface (&clk_dib, ioENF, 0);             /* set flag */

    clk_ctr = clk_delay (1);                            /* reset counter */
    }

tprintf (clk_dev, TRACE_PSERV, "Rate %s delay %d service %s\n",
         rate_name [clk_select], tick_count,
         (clk_select == 2 ? "coscheduled" : "scheduled"));

return sim_activate (uptr, tick_count);                 /* reactivate */
}


/* Reset routine */

static t_stat clk_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    lost_tick = CLEAR;                                  /* clear error */
    clk_select = 0;                                     /* clear select */
    clk_ctr = 0;                                        /* clear counter */

    if (clk_dev.lname == NULL)                          /* logical name unassigned? */
        clk_dev.lname = strdup ("TBG");                 /* allocate and initialize the name */
    }

IOPRESET (&clk_dib);                                    /* PRESET device (does not use PON) */

return SCPE_OK;
}



/* Time Base Generator local utility routines */


/* Clock delay routine */

static int32 clk_delay (int32 flg)
{
int32 sel;

if (clk_unit [0].flags & UNIT_W2B && clk_select >= 4)   /* if jumper W2 is in position B */
    sel = clk_select - 3;                               /*   then rates 4-7 rescale to 1-4 */
else                                                    /* otherwise */
    sel = clk_select;                                   /*   the rate selector is used as is */

if (flg)                                                /* if the prescaler value is wanted */
    return scale [sel];                                 /*   then return it */
else                                                    /* otherwise */
    return delay [sel];                                 /*   return the tick delay count */
}
