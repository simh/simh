/* hp3000_lp.c: HP 3000 30209A Line Printer Interface simulator

   Copyright (c) 2016-2018, J. David Bryan

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

   LP           HP 30209A Line Printer Interface

   27-Dec-18    JDB     Revised fall through comments to comply with gcc 7
   07-Sep-17    JDB     Changed PCHR and UPCHR registers to PUNCHR and UNPCHR
                        Changed PRTBUF, OVPCHR, PUNCHR, and UNPCHR to REG_A
   05-Sep-17    JDB     Changed REG_A (permit any symbolic override) to REG_X
   20-Jul-17    JDB     Added a forced detach option (-F switch) to "lp_detach"
   24-Jun-17    JDB     Added "report_error", fixed "lp_set_model" times bug
   22-Jun-17    JDB     Moved deferred offline/detach cancel to SET ONLINE
   26-Apr-17    JDB     Fixed "lp_service" return for VFU channel not punched
                        Restricted auto-print on buffer full to the 2607
                        Paper fault is now delayed until the TOF for the 2607
                        Changed "activate_unit" to schedule zero-length delays
   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   03-Sep-16    JDB     Added power-fail detection
   08-Jul-16    JDB     Added REG entry to save the transfer unit wait field
                        Extended "lp_show_vfu" to show the VFU channel definitions
   01-Jul-16    JDB     First release version
   27-Apr-16    JDB     Passes the On-Line HP Line Printers Verification (D466A)
   19-Apr-16    JDB     Passes the universal interface diagnostic (D435A)
   24-Mar-16    JDB     Created

   References:
     - 30051A Universal Interface (Differential) Maintenance Manual
         (30051-90001, May 1976)
     - Installation and Service Manual for Line Printer Subsystems
         (30209-90006, May 1976)
     - Line Printer Operating and Programming Manual
         (30209-90008, June 1976)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 30118A, 30127A, 30128A, and 30133A Line Printer Subsystems connect the
   2607A, 2613A, 2618A, and 2617A printers, respectively, to the HP 3000.  Each
   subsystem consists of a 30209A Line Printer Controller, employing a 30051A
   Universal Interface (Differential) and interconnecting cable, and an HP
   2607A (200 lines per minute), HP 2613 (300 lpm), HP 2617 (600 lpm), or HP
   2618 (1250 lpm) line printer.  These subsystems employ the Multiplexer
   Channel to achieve a 360 KB/second transfer rate from the CPU.

   This module simulates three hardware devices:

     - the HP 30051A Universal Interface (Differential)
     - the HP 30049C Diagnostic Hardware Assembly
     - the HP 2607A/13A/17A/18A line printer and 30209-60004 printer cable

   Available with either differential or TTL I/O logic levels, the Universal
   Interface (UI) provides a 16-bit bidirectional parallel connection between a
   device and the HP 3000 system.  Both direct and programmed I/O via the
   Multiplexer Channel are supported, word or byte transfers may be selected,
   and byte packing and unpacking is available.  In addition to the 16-bit data
   path, a five-bit control word is supplied to the device, and an eight-bit
   status word is returned.  Flexible configuration of interface operation is
   provided via ten jumpers, and eight different interrupt sources are
   available.  The Universal Interface is also used to connect the paper tape
   reader, punch, and card reader to the HP 3000.

   The Diagnostic Hardware Assembly (DHA) connects to the UI device connectors
   and provides a programmable loopback and configuration capability.  Five LEDs
   continuously display the device control word, and test points are provided to
   monitor the state of the 16-bit data path, the ten programmable jumper
   settings, and the Device Command, Device Flag, and Device End signals.
   Enabling the diagnostic mode simulates the installation of the DHA in place
   of the printer device cable.

   The interface supports a single line printer.  The supported printers are
   configured with Option 001, which provides a 128 (2607) or 96 (2613/17/18)
   character set.  Two output modes are provided: an expanded mode that is
   suitable for retaining printer output as a text file, and a compact mode that
   is suitable for sending the printer output to a host-connected physical
   printer.  An 8-channel (2607) or 12-channel (2613/17/18) Vertical Format Unit
   is supported, and custom VFU tape images may be loaded from properly
   formatted host-system text files.

   The printer supports realistic and optimized (fast) timing modes.  Realistic
   timing attempts to model the print buffer load and print-and-space operation
   delays inherent in the physical hardware.  For example, in REALTIME mode,
   output of longer lines takes more time than output of shorter lines, and
   spacing six lines takes approximately six times longer than spacing one line.
   In FASTTIME mode, all timings are reduced to be "just long enough" to satisfy
   MPE software driver requirements.


   In hardware, the ten UI configuration jumpers perform these functions:

     Jumper  Interpretation when removed      Interpretation when installed
     ------  -------------------------------  ------------------------------
       W1    SR set by PCONTSTB               SR set by Device Status bit 11

       W2    Flag asserts on leading edge     Flag asserts on trailing edge

       W3    Command uses response mode       Command uses pulse mode

       W4    inhibit IRQ on Device Status     enable IRQ on Device Status
               bit 8 leading edge               bit 8 leading edge

       W5    DATA IN latched on Flag          DATA IN always transparent

       W6    Flag denies on trailing edge     Flag denies on leading edge

       W7    normal byte-mode write transfer  test byte-mode write transfer

       W8    inhibit IRQ on Device Status     enable IRQ on Device Status
               bit 9 leading edge               bit 9 leading edge

       W9    inhibit IRQ on Device Status     enable IRQ on Device Status
               bit 10 trailing edge             bit 10 trailing edge

       W10   DEV CMD polarity is normal       DEV CMD polarity is inverted

   The line printer cable is wired with this configuration:

     Interface Connection                      Printer Connection
     ----------------------------------------  ------------------
     Data Out bit 15                           DATA 1
     Data Out bit 14                           DATA 2
     Data Out bit 13                           DATA 3
     Data Out bit 12                           DATA 4
     Data Out bit 11                           DATA 5
     Data Out bit 10                           DATA 6
     Data Out bit  9                           DATA 7
     Device Command                            STROBE
     Device Flag                               ~DEMAND
     Control Word bit 10                       PAPER INSTRUCTION
     Device Status bit 9                       ONLINE
     Device Status bit 10                      ONLINE
     Device Status bit 11                      ~READY
     Device Status bit 12                      VFU CHANNEL 12
     Device Status bit 13                      VFU CHANNEL 9
     Device End                                ~ONLINE
     Set Transfer Error Flip-Flop              (no connection)
     Master Clear                              MASTER CLEAR

     Internal Connection                       Action
     ----------------------------------------  --------------------------------
     300 pF across the Write Delay One-Shot    sets 1.2 uS pulse width
     1500 pF across the Master Clear One-Shot  sets 5.1 uS pulse width
     jumper W4 shorted                         none (Status 8 is not connected)
     jumper W8 shorted                         enables IRQ when ONLINE asserts
     jumper W9 shorted                         enables IRQ when ONLINE denies

   DEMAND is wired inversely to Device Flag, so DEMAND assertion is Device Flag
   denial and vice versa.  DEMAND dropping after STROBE assertion corresponds
   with Device Flag asserting after Device Command asserts, and DEMAND asserting
   after the printer is ready corresponds to Device Flag denying.

   Similarly, ONLINE is wired inversely to Device End, so the printer going
   offline asserts Device End, and READY is wired inversely to Device Status bit
   11, so bit 11 is asserted when the printer is not ready (either powered off
   or out of paper).

   The READY and ONLINE signals indicate the current state of the printer.
   READY asserts when printer power is on, no alarm condition (paper out, tape
   format error) exits, and the VFU has been initialized.  ONLINE asserts when
   READY is asserted and the Online button is pressed.  Therefore:

     ~ONLINE * ~READY = paper out or VFU error
     ~ONLINE *  READY = paper loaded and offline
      ONLINE * ~READY = (prohibited)
      ONLINE *  READY = paper loaded and online

   The printer DEMAND signal asserts when the printer is ready for data and
   denies when it is printing or slewing.  It also denies when the printer goes
   offline.  DEMAND is cross-connected to the Device Flag differential input,
   so that DEV FLAG is the complement of DEMAND, i.e., it asserts when the
   printer is busy and denies when the printer is available.

   The normal sequence starts with DEMAND asserted (i.e., DEV FLAG denied).  The
   interface asserts STROBE (DEV CMD), the printer denies DEMAND (asserts DEV
   FLAG), the interface denies STROBE (DEV CMD), and the printer then asserts
   DEMAND (denies DEV FLAG) when the character data is accepted or the print
   operation is complete.

   When the ON/OFFLINE button on the printer is pressed, the printer will not go
   offline (i.e., deny the ONLINE signal) if there are characters in the print
   buffer.  Instead, the offline condition is held off until an internal "allow
   offline" signal asserts.  This occurs when the print buffer is empty and the
   print cycle is inactive.  When ONLINE denies, DEMAND is inhibited, so the
   interface waits at the end of the handshake sequence for DEV FLAG to deny.
   Note that this holds off SR to the Multiplexer Channel, so the channel
   program waits.  When the printer is put back online, DEMAND asserts, so DEV
   FLAG denies, the handshake completes, SR asserts, and the interface returns
   to the idle condition to await the next command.

   This has implications for the SET OFFLINE and DETACH commands if they are
   issued while the print buffer contains data or the printer unit is busy
   executing a print action.

   The SET LP OFFLINE and DETACH LP commands check for data in the print buffer
   or a print operation in progress.  If either condition is true, they set
   their respective deferred-action flags and display "Command not completed."
   A SHOW LP will show that the device is still online and attached.  Once
   simulation is resumed and the print operation completes, the printer is set
   offline or detached as requested.  No console message reports this, as it is
   assumed that the executing program will detect the condition and report
   accordingly.  A subsequent SHOW LP will indicate the new status.

   A SET LP ONLINE command when a deferred-action flag is set simply clears the
   flag, which cancels the pending offline or detach condition.

   A RESET LP command also clears the deferred-action flags and so clears any
   pending offline or detach.  However, it also clears the print buffer and
   terminates any print action in progress, so a SET LP OFFLINE or DETACH LP
   will succeed if issued subsequently.


   The Universal Interface responds to both direct I/O and programmed I/O from
   the Multiplexer Channel, as follows:

   Control Word Format (CIO and SIO Control word 2):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | A |  device control   | X | S | B | I | T | device
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | A |   function    | E | X | S | B | I | T | DHA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | A | -   -   -   - | F | X | S | B | I | T | printer
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = programmed master clear
     R = reset interrupts
     A = acquire data from device
     E = enable diagnostic hardware assembly function
     F = printer output character/format (0/1) code
     X = enable data transfer interrupt
     S = interrupt/device (0/1) status
     B = word/byte (0/1) transfer
     I = enable interrupts
     T = enable transfer timer

   IRQ Reset:

     000 = none
     001 = transfer timer and transfer error
     010 = I/O system
     011 = clear interface
     100 = data transfer completion
     101 = line ready (device status bit 8)
     110 = ready (device status bit 9)
     111 = not ready (device status bit 10)

   DHA Function:

     0000 = clear configuration registers (installs jumpers)
     0001 = remove jumper J2W2
     0010 = assert DEV END
     0011 = remove jumper J2W8
     0100 = set Transfer Error flip-flop
     0101 = remove jumper J2W4
     0110 = remove jumper J2W10
     0111 = remove jumper J2W6
     1000 = DEV FLAG follows DEV CMD or Control 6 (0/1)
     1001 = remove jumper J2W5
     1010 = assert CLEAR INTERFACE
     1011 = remove jumper J2W9
     1100 = Status 8-10 follow Control 6-8
              or master clear, power on, and power fail (0/1)
     1101 = remove jumper J2W1
     1110 = remove jumper J2W3
     1111 = remove jumper J2W7

   Bits 6-10 are the device control bits.  For the DHA, control bit 10 enables
   the function decoder.  The decoder is combinatorial and the registers are
   "ones-catching," so the function field must be set and then maintained while
   bit 10 is asserted and then denied.  For the line printer, control bit 10
   indicates whether character data (0) or format commands (1) will be output.
   Programmed control word 1 (IOCW) is not used.

   Setting control bit 15 starts (or restarts) the five-second transfer timer.
   Issuing a Reset Transfer Timer and Transfer Error Interrupts, a Master Reset,
   or a Reset Interrupts command stops the timer.  If the timer expires, a
   Transfer Timer interrupt occurs.


   Status Word Format (TIO and SIO Status):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 0 | 0 |  dev irq  | X | C | Y | E | T | interrupt
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 1 | 0 |         device status         | device
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 1 | 0 | - | L | L | N | V | U | - | - | printer
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK
     D = direct I/O OK
     I = interrupt pending
     F = device flag
     X = data transfer interrupt
     C = clear interface interrupt
     Y = I/O system interrupt
     E = transfer error interrupt
     T = transfer timer interrupt
     L = online
     N = not ready
     V = VFU channel 12
     U = VFU channel 9

   Sequence Counter:

     00 = idle
     10 = request to device issued for word or 1st byte
     11 = device operation started
     01 = request to device issued for 2nd byte

   Device Interrupt Request Bits:

      8 = device status bit 8 interrupt (not used by the printer)
      9 = device status bit 9 interrupt (printer went online)
     10 = device status bit 10 interrupt (printer went offline)

   Control word bit 12 determines whether the interrupt status word (0) or the
   device status word (1) is returned.

   A transfer error occurs when the channel asserts XFERERROR to abort a
   transfer for a parity error or memory address out of bounds.  Device status
   bits assume the logic 1 state with the inputs disconnected (e.g., power off).


   Output Data Word Format (WIO and SIO Write):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - |    1st ASCII character    | - |    2nd ASCII character    | byte mode
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - | - |      ASCII character      | word mode
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | -   -   -   -   -   -   -   - | - |        format word        | format
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The printer only uses seven data bits, so the MSB of each byte is ignored.
   If the printer's line length is exceeded during write operations, the
   buffered line will be printed, the paper will be advanced one line, and the
   buffer will be cleared to accept the character causing the overflow.


   Implementation notes:

    1. The Clear Interface Logic (CLRIL) signal inhibits SIO OK status.  The
       Card Reader/Punch Interface version of the UI does not assert the CLRIL
       signal in response to external CLEAR INTERFACE assertion; only a transfer
       error does.  Therefore, the SIO OK signal is not inhibited while the
       clear interface interrupt is present.  For this version, the external
       Clear Interface signal sets the CLR INF flip-flop, which sets the (C) bit
       in the status register and generates an interrupt, but otherwise has no
       effect on the interface logic.

       The standard version of the UI asserts CLRIL, and therefore inhibits SIO
       OK, for both the clear interface and transfer error conditions.

    2. Because the interface uses differential interface logic, the external
       sense of a signal may be inverted by exchanging the + and - connections.
       To accommodate this in simulation, separate variables are used for the
       internal and external states.  For example, "device_command" represents
       the internal state, while "device_command_out" represents the external
       state (which may be inverted from the internal state if jumper J2W10 is
       installed).

    3. The Universal Interface supports terminating channel transfers by
       asserting DEVEND, and the line printer cable connects the ONLINE output
       inversely to the Device End input, so that it is asserted when the
       printer is offline.  However, when the printer goes offline, it holds its
       DEMAND line denied, which keeps Device Flag asserted.  This hangs the
       transfer handshake in the Device_Flag_1/2 state until the printer goes
       online again.  As the interface recognizes Device End only in the
       Device_Command_1/2 state, DEVEND will never be asserted to terminate a
       channel transfer.

    4. In hardware, a paper-out condition is noted, but the line printer does
       not go offline until the top of the next form is reached.  This ensures
       that the current page is completed first.  By contrast, a torn-paper
       condition causes the printer to go offline at the completion of the
       current line.  In simulation, a DETACH is handled as a torn-paper
       condition.

    5. Slewing in expanded mode is performed by appending CR LF pairs to the
       character buffer and then writing the combined buffer to the printer
       output file.  The size of the buffer must accommodate the largest print
       line (136 characters) plus the largest possible slew (144 lines * 2
       characters per line).
*/



#include "hp3000_defs.h"
#include "hp3000_io.h"



/* Interface program constants */

#define PULSE_TIME          uS (8)              /* device command pulse = 8 microseconds */
#define XFER_TIME           S (5)               /* transfer timeout = 5 seconds */

/* Printer program constants */

#define CR                  '\r'                /* carriage return */
#define LF                  '\n'                /* line feed */
#define FF                  '\f'                /* form feed */
#define DEL                 '\177'              /* delete */

#define DATA_MASK           0177u               /* printer uses only 7 bits for data */
#define FORMAT_VFU          0100u               /* printer VFU selector */
#define FORMAT_MASK         0117u               /* printer format command mask for 12-channel VFU */
#define FORMAT_VFU_8_MASK   0107u               /* printer format command mask for 8-channel VFU */

#define FORMAT_SUPPRESS     0000u               /* format code to slew 0 lines */
#define FORMAT_VFU_CHAN_1   0100u               /* format code to slew to VFU channel 1 */
#define FORMAT_VFU_BIAS     0077u               /* bias converting from format code to channel number */

#define VFU_MAX             144                 /* maximum number of VFU form lines */
#define VFU_SIZE            (VFU_MAX + 1)       /* size of the VFU array */
#define LINE_SIZE           256                 /* size of the character array used to read the VFU file */

#define VFU_WIDTH           12                  /* maximum number of VFU channels */

#define VFU_CHANNEL_1       04000u              /* top of form */
#define VFU_CHANNEL_2       02000u              /* bottom of form */
#define VFU_CHANNEL_3       01000u              /* single space */
#define VFU_CHANNEL_4       00400u              /* double space */
#define VFU_CHANNEL_5       00200u              /* triple space */
#define VFU_CHANNEL_6       00100u              /* half page */
#define VFU_CHANNEL_7       00040u              /* quarter page */
#define VFU_CHANNEL_8       00020u              /* sixth page */
#define VFU_CHANNEL_9       00010u              /* bottom of form */
#define VFU_CHANNEL_10      00004u              /* (unassigned) */
#define VFU_CHANNEL_11      00002u              /* (unassigned) */
#define VFU_CHANNEL_12      00001u              /* (unassigned) */

#define CHARS_MAX           136                 /* maximum number of characters buffered by the printers */

#define BUFFER_SIZE         (CHARS_MAX + VFU_MAX * 2)   /* max chars + max VFU * 2 (CR LF) */

#define PRINTER_JUMPERS     (W4 | W8 | W9)      /* jumpers J2W4, J2W8, and J2W9 are installed */


/* Debug flags */

#define DEB_CMD             (1u << 0)           /* trace controller commands */
#define DEB_CSRW            (1u << 1)           /* trace command initiations and completions */
#define DEB_STATE           (1u << 2)           /* trace device handshake state changes */
#define DEB_SERV            (1u << 3)           /* trace channel service scheduling calls */
#define DEB_XFER            (1u << 4)           /* trace data transmissions */
#define DEB_IOB             (1u << 5)           /* trace I/O bus signals and data words */


/* Device flags */

#define DEV_DIAG_SHIFT      (DEV_V_UF + 0)              /* Diagnostic Hardware Assembly is installed */
#define DEV_REALTIME_SHIFT  (DEV_V_UF + 1)              /* timing mode is realistic */

#define DEV_DIAG            (1u << DEV_DIAG_SHIFT)      /* diagnostic mode flag */
#define DEV_REALTIME        (1u << DEV_REALTIME_SHIFT)  /* realistic timing flag */


/* Printer unit flags.

   UNIT_V_UF +  7   6   5   4   3   2   1   0
              +---+---+---+---+---+---+---+---+
              | - | - | - | O | E |   model   |
              +---+---+---+---+---+---+---+---+

   Where:

     O = offline
     E = expanded output
*/

#define UNIT_MODEL_SHIFT    (UNIT_V_UF + 0)     /* printer model ID */
#define UNIT_EXPAND_SHIFT   (UNIT_V_UF + 3)     /* printer uses expanded output */
#define UNIT_OFFLINE_SHIFT  (UNIT_V_UF + 4)     /* printer is offline */

#define UNIT_MODEL_MASK     0000007u            /* model ID mask */

#define UNIT_MODEL          (UNIT_MODEL_MASK << UNIT_MODEL_SHIFT)
#define UNIT_EXPAND         (1u << UNIT_EXPAND_SHIFT)
#define UNIT_OFFLINE        (1u << UNIT_OFFLINE_SHIFT)
#define UNIT_ONLINE         0

#define UNIT_2607           (HP_2607 << UNIT_MODEL_SHIFT)
#define UNIT_2613           (HP_2613 << UNIT_MODEL_SHIFT)
#define UNIT_2617           (HP_2617 << UNIT_MODEL_SHIFT)
#define UNIT_2618           (HP_2618 << UNIT_MODEL_SHIFT)


/* Unit flags accessor */

#define GET_MODEL(f)        ((PRINTER_TYPE) (((f) >> UNIT_MODEL_SHIFT) & UNIT_MODEL_MASK))


/* Unit references */

#define xfer_unit           lp_unit [0]         /* transfer handshake unit */

#define xfer_uptr           (&lp_unit [0])      /* transfer handshake unit pointer */
#define pulse_uptr          (&lp_unit [1])      /* pulse timer unit pointer */
#define timer_uptr          (&lp_unit [2])      /* transfer timer unit pointer */

static const char *const unit_name [] = {       /* unit names, indexed by unit number */
    "Transfer",
    "Pulse",
    "Watchdog"
    };


/* Printer types */

typedef enum {
    HP_2607,                                    /* HP 2607A */
    HP_2613,                                    /* HP 2613A */
    HP_2617,                                    /* HP 2617A */
    HP_2618                                     /* HP 2618A */
    } PRINTER_TYPE;


/* Printer locality states */

typedef enum {
    Offline,                                    /* printer is going offline */
    Online                                      /* printer is going online */
    } LOCALITY;


/* Printer properties table.

   This table contains the characteristics that vary between printer models.
   The "char_set" field values reflect printer Option 001, 96/128-character set.
   The "not_ready" field indicates whether a paper fault sets a separate
   not-ready status or simply takes the printer offline.  The "fault_at_eol"
   field indicates whether a paper fault is reported at the end of any line or
   only at the top of the next form.
*/

typedef struct {
    uint32  line_length;                        /* the maximum number of print positions */
    uint32  char_set;                           /* the size of the character set */
    uint32  vfu_channels;                       /* the number of VFU channels */
    t_bool  not_ready;                          /* TRUE if the printer reports a separate not ready status */
    t_bool  overprints;                         /* TRUE if the printer supports overprinting */
    t_bool  autoprints;                         /* TRUE if the printer automatically prints on buffer overflow */
    t_bool  fault_at_eol;                       /* TRUE if a paper fault is reported at the end of any line */
    } PRINTER_PROPS;

static const PRINTER_PROPS print_props [] = {   /* printer properties, indexed by PRINTER_TYPE */
/*     line    char    VFU      not     over    auto   fault  */
/*    length   set   channels  ready   prints  prints  at EOL */
/*    ------  -----  --------  ------  ------  ------  ------ */
    {  132,   128,      8,     FALSE,  FALSE,  TRUE,   FALSE  },    /* HP_2607 */
    {  136,    96,     12,     TRUE,   TRUE,   FALSE,  TRUE   },    /* HP_2613 */
    {  136,    96,     12,     TRUE,   TRUE,   FALSE,  TRUE   },    /* HP_2617 */
    {  132,    96,     12,     TRUE,   TRUE,   FALSE,  TRUE   }     /* HP_2618 */
    };


/* Delay properties table.

   To support the realistic timing mode, the delay properties table contains
   timing specifications for the supported printers.  The times represent the
   delays for mechanical and electronic operations.  Delay values are in event
   tick counts; macros are used to convert from times to ticks.


   Implementation notes:

    1. Although all of the printers operate more slowly with a 96/128-character
       set installed than with a 64-character set, the times reflect the smaller
       set size.  Also, some models provide different print rates, depending on
       how many and/or which characters are printed.  These variations are not
       simulated.
*/

typedef struct {
    int32  buffer_load;                         /* per-character transfer time */
    int32  print;                               /* print time */
    int32  advance;                             /* paper advance time per line */
    } DELAY_PROPS;

static const DELAY_PROPS real_times [] = {      /* real-time delays, indexed by PRINTER_TYPE */
   /*  buffer                paper   */
   /*   load      print     advance  */
   /* ---------  --------  --------- */
    { uS (12.6), mS (260), mS (40.1) },         /* HP_2607  200 lines per minute */
    { uS (1.75), mS (183), mS (8.33) },         /* HP_2613  300 lines per minute */
    { uS (1.75), mS ( 86), mS (6.67) },         /* HP_2617  600 lines per minute */
    { uS (1.75), mS ( 38), mS (4.76) }          /* HP_2618 1250 lines per minute */
    };

#define LP_BUFFER_LOAD      uS (1)              /* fast per-character transfer time */
#define LP_PRINT            mS (1)              /* fast print time */
#define LP_ADVANCE          uS (50)             /* fast paper advance time per line */

static DELAY_PROPS fast_times =                 /* FASTTIME delays */
    { LP_BUFFER_LOAD,
      LP_PRINT,
      LP_ADVANCE
    };


/* Data transfer handshake sequencer.

   The sequencer controls the handshake that transfers data between the
   interface and the device.
*/

typedef enum {
    Idle,                                       /* the device is idle */
    Device_Command_1,                           /* device command is asserted for a word or first byte */
    Device_Flag_1,                              /* device flag is asserted for a word or first byte */
    Device_Command_2,                           /* device command is asserted for the second byte */
    Device_Flag_2                               /* device flag is asserted for the second byte */
    } SEQ_STATE;

static const char *const state_name [] = {      /* sequencer state names, indexed by SEQ_STATE */
    "Idle",
    "Device Command 1",
    "Device Flag 1",
    "Device Command 2",
    "Device Flag 2"
    };


/* Configuration jumpers.

   Various aspects of interface operation are configured by installing or
   removing jumpers contained within the connector hood of the device
   interconnection cable.  Jumpers are simulated by bits in the "jumper_set"
   word, with a 1 value representing "installed" and a 0 value representing
   "removed" (although in hardware installing a jumper pulls the corresponding
   signal down to 0).

   The Diagnostic Hardware Assembly provides programmatic configuration of the
   jumpers.  All jumpers are installed by executing a "clear registers" command,
   and then individual jumpers may be removed by executing the corresponding
   "remove jumper J2Wn" commands.  This is simulated by setting all of the bits
   in the "jumper_set" word and then selectively ANDing the word with
   complemented constants from the "jumper_map" table, thereby clearing
   individual bits.


   Implementation notes:

    1. In simulation, jumper W5 is not used.  The DATA IN signals are always
       latched when DEV FLAG asserts.  Always-transparent operation is not
       provided.

    2. In hardware, DHA control word bits 6-9 are wired to decoder input bits
       0-3.  As the 3000 uses decreasing bit-number significance, while the
       decoder chip uses increasing bit-number significance, the order of the
       functions in the "jumper_map" table reflect the reversed bit order of the
       index.  For example, index 0001 contains the function for decoder output
       8 (1000).
*/

#define W1                  (1u << 0)           /* SR set by PCONTSTB/Device Status bit 11 */
#define W2                  (1u << 1)           /* +/- edge of Device Flag advances sequence counter from 1 to 2 */
#define W3                  (1u << 2)           /* Device Command operates in response/pulse mode */
#define W4                  (1u << 3)           /* inhibit/enable interrupt on STAT8 + edge */
#define W5                  (1u << 4)           /* Data In latched on sequence count 1 and 3/always transparent */
#define W6                  (1u << 5)           /* -/+ edge of Device Flag advances sequence counter from 2 to 3 */
#define W7                  (1u << 6)           /* normal/test write transfer */
#define W8                  (1u << 7)           /* inhibit/enable interrupt on STAT9 + edge */
#define W9                  (1u << 8)           /* inhibit/enable interrupt on STAT10 - edge */
#define W10                 (1u << 9)           /* Device Command same/inverted polarity as Data Out */

#define J2W1_INSTALLED      ((jumper_set & W1)  != 0)
#define J2W2_INSTALLED      ((jumper_set & W2)  != 0)
#define J2W3_INSTALLED      ((jumper_set & W3)  != 0)
#define J2W4_INSTALLED      ((jumper_set & W4)  != 0)
#define J2W5_INSTALLED      ((jumper_set & W5)  != 0)
#define J2W6_INSTALLED      ((jumper_set & W6)  != 0)
#define J2W7_INSTALLED      ((jumper_set & W7)  != 0)
#define J2W8_INSTALLED      ((jumper_set & W8)  != 0)
#define J2W9_INSTALLED      ((jumper_set & W9)  != 0)
#define J2W10_INSTALLED     ((jumper_set & W10) != 0)

static const uint32 jumper_map [16] = {         /* jumper removal map, indexed by CN_DHA_FN */
     0,                                         /*   0000 = (unaffected) */
    ~W2,                                        /*   0001 = remove jumper J2W2 */
     0,                                         /*   0010 = (unaffected) */
    ~W8,                                        /*   0011 = remove jumper J2W8 */
     0,                                         /*   0100 = (unaffected) */
    ~W4,                                        /*   0101 = remove jumper J2W4 */
    ~W10,                                       /*   0110 = remove jumper J2W10 */
    ~W6,                                        /*   0111 = remove jumper J2W6 */
     0,                                         /*   1000 = (unaffected) */
    ~W5,                                        /*   1001 = remove jumper J2W5 */
     0,                                         /*   1010 = (unaffected) */
    ~W9,                                        /*   1011 = remove jumper J2W9 */
     0,                                         /*   1100 = (unaffected) */
    ~W1,                                        /*   1101 = remove jumper J2W1 */
    ~W3,                                        /*   1110 = remove jumper J2W3 */
    ~W7                                         /*   1111 = remove jumper J2W7 */
    };


/* Diagnostic Hardware Assembly control register.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | F | S | -   -   - |          jumpers J2W10-J2W1           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset has occurred
     F = device flag follows device command/control 6 (0/1)
     S = status 8-10 follow control 6-8/master clear-power on-power fail (0/1)


   Implementation notes:

    1. Jumper bits are defined as 0 = removed and 1 = installed.  This is
       the opposite of the DHA hardware, for which a zero output "installs" a
       jumper.

    2. Jumpers J2W10-J2W1, which are stored in the "jumpers" array, are mirrored
       in the jumper control register to allow the diagnostic to test the full
       set of jumpers with single assertions.  Otherwise, ten assertions would
       be necessary for each test.
*/

#define DHA_MR              0100000u            /* (M) a master reset has occurred */
#define DHA_FLAG_SEL        0040000u            /* (F) device flag follows control 6 */
#define DHA_STAT_SEL        0020000u            /* (S) status 8-10 follow master clear-power on-power fail */
#define DHA_JUMPER_MASK     0001777u            /* J2Wx jumpers mask */
#define DHA_CLEAR           0001777u            /* control register clear value (all jumpers installed) */


/* Interface control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | A |  device control   | X | S | B | I | T | device
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | A |   function    | E | X | S | B | I | T | DHA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* (M) master reset */
#define CN_RIN              0040000u            /* (R) reset interrupt */
#define CN_RIN_MASK         0034000u            /* reset interrupt request selector mask */
#define CN_RIN_XFR_TMR      0004000u            /* reset watchdog timer and transfer error interrupts */
#define CN_ACQUIRE          0002000u            /* (A) acquire data from device */
#define CN_DHA_FN_MASK      0001700u            /* diagnostic hardware assembly function mask */
#define CN_DHA_ST_MASK      0001600u            /* diagnostic hardware assembly status mask */
#define CN_DHA_FLAG         0001000u            /* diagnostic hardware assembly device flag value */
#define CN_DHA_FN_ENABLE    0000040u            /* (E) enable diagnostic hardware assembly function */
#define CN_XFR_IRQ_ENABLE   0000020u            /* (X) enable data transfer interrupt */
#define CN_DEVSTAT          0000010u            /* (S) interrupt/device (0/1) status */
#define CN_BYTE_XFER        0000004u            /* (B) word/byte (0/1) transfer */
#define CN_IRQ_ENABLE       0000002u            /* (I) enable interrupts */
#define CN_XFR_TMR_ENABLE   0000001u            /* (T) enable data transfer timer */

#define CN_RIN_SHIFT        11                  /* reset interrupt request alignment shift */
#define CN_DHA_ST_SHIFT      7                  /* diagnostic hardware assembly status alignment shift */
#define CN_DHA_FN_SHIFT      6                  /* diagnostic hardware assembly function alignment shift */

#define CN_RESET(c)         (((c) & CN_RIN_MASK)    >> CN_RIN_SHIFT)
#define CN_DHA_ST(c)        (((c) & CN_DHA_ST_MASK) >> CN_DHA_ST_SHIFT)
#define CN_DHA_FN(c)        (((c) & CN_DHA_FN_MASK) >> CN_DHA_FN_SHIFT)

static const char *const dha_fn_name [16] = {   /* DHA function names, indexed by CN_DHA_FN */
    "clear registers",                          /*   0000 = clear registers (installs jumpers) */
    "remove J2W2",                              /*   0001 = remove jumper J2W2 */
    "assert DEVEND",                            /*   0010 = assert Device End */
    "remove J2W8",                              /*   0011 = remove jumper J2W8 */
    "set transfer error",                       /*   0100 = set Transfer Error flip-flop */
    "remove J2W4",                              /*   0101 = remove jumper J2W4 */
    "remove J2W10",                             /*   0110 = remove jumper J2W10 */
    "remove J2W6",                              /*   0111 = remove jumper J2W6 */
    "control 6 drives device flag",             /*   1000 = connect device flag to control bit 6 */
    "remove J2W5",                              /*   1001 = remove jumper J2W5 */
    "assert CLRIF",                             /*   1010 = assert Clear Interface */
    "remove J2W9",                              /*   1011 = remove jumper J2W9 */
    "CLR/PON/PF drive status 8-10",             /*   1100 = connect status 8-10 to master clear/power on/power fail */
    "remove J2W1",                              /*   1101 = remove jumper J2W1 */
    "remove J2W3",                              /*   1110 = remove jumper J2W3 */
    "remove J2W7"                               /*   1111 = remove jumper J2W7 */
    };

static const char *const reset_irq_name [8] = { /* reset interrupt request names, indexed by CN_RESET */
    "",                                         /*   000 = none */
    " | reset timer/xfer error irq",            /*   001 = watchdog timer and transfer error */
    " | reset I/O system irq",                  /*   010 = I/O system */
    " | reset clear interface irq",             /*   011 = clear interface */
    " | reset data xfer irq",                   /*   100 = data transfer completion */
    " | reset status 8 irq",                    /*   101 = device status 8 */
    " | reset status 9 irq",                    /*   110 = device status 9 */
    " | reset status 10 irq"                    /*   111 = device status 10 */
    };

static const BITSET_NAME dha_control_names [] = {       /* DHA control word names */
    "master clear",                                     /*   bit  0 */
    "clear interrupts",                                 /*   bit  1 */
    NULL,                                               /*   bit  2 */
    NULL,                                               /*   bit  3 */
    NULL,                                               /*   bit  4 */
    "acquire data",                                     /*   bit  5 */
    "DC6",                                              /*   bit  6 */
    "DC7",                                              /*   bit  7 */
    "DC8",                                              /*   bit  8 */
    "DC9",                                              /*   bit  9 */
    "enable function",                                  /*   bit 10 */
    "enable data xfer interrupt",                       /*   bit 11 */
    "\1device status\0interrupt status",                /*   bit 12 */
    "\1byte xfer\0word xfer",                           /*   bit 13 */
    "enable interrupts",                                /*   bit 14 */
    "enable transfer timer"                             /*   bit 15 */
    };

static const BITSET_FORMAT dha_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (dha_control_names, 0, msb_first, has_alt, no_bar) };


/* Printer control word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | irq reset | -   -   -   -   - | F | X | S | B | I | T | printer
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_FORMAT           0000040u            /* printer output character/format (0/1) code */

static const BITSET_NAME prt_control_names [] = {       /* Printer control word names */
    "master clear",                                     /*   bit  0 */
    "clear interrupts",                                 /*   bit  1 */
    NULL,                                               /*   bit  2 */
    NULL,                                               /*   bit  3 */
    NULL,                                               /*   bit  4 */
    "acquire data",                                     /*   bit  5 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  8 */
    NULL,                                               /*   bit  9 */
    "\1format\0character",                              /*   bit 10 */
    "enable data xfer interrupt",                       /*   bit 11 */
    "\1device status\0interrupt status",                /*   bit 12 */
    "\1byte xfer\0word xfer",                           /*   bit 13 */
    "enable interrupts",                                /*   bit 14 */
    "enable transfer timer"                             /*   bit 15 */
    };

static const BITSET_FORMAT prt_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (prt_control_names, 0, msb_first, has_alt, no_bar) };


/* Interface status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 0 | 0 |  dev irq  | X | C | Y | E | T | interrupt
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 1 | 0 |         device status         | device
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The entry for bit 6 of the interrupt status names formatting array is
       given in the alternate form to print the "interrupt status" string when
       the bit is zero.
*/

#define ST_SIO_OK           0100000u            /* (S) SIO OK to use */
#define ST_DIO_OK           0040000u            /* (D) direct I/O OK to use */
#define ST_IRQ_PENDING      0020000u            /* (I) interrupt pending */
#define ST_SEQ_COUNT_0      0000000u            /* sequence count 0 (00) */
#define ST_SEQ_COUNT_1      0010000u            /* sequence count 1 (10) */
#define ST_SEQ_COUNT_2      0014000u            /* sequence count 2 (11) */
#define ST_SEQ_COUNT_3      0004000u            /* sequence count 3 (01) */
#define ST_DEVFLAG          0002000u            /* (F) device flag */
#define ST_DEVSTAT          0001000u            /* interrupt/device (0/1) status */
#define ST_DEVIRQ_MASK      0000340u            /* device interrupt request mask */
#define ST_ST8_IRQ          0000200u            /* device status 8 interrupt */
#define ST_DHA_MR           0000200u            /* diagnostic hardware assembly status 8 (master clear) */
#define ST_ST9_IRQ          0000100u            /* device status 9 interrupt */
#define ST_DHA_PON          0000100u            /* diagnostic hardware assembly status 9 (power on) */
#define ST_ST10_IRQ         0000040u            /* device status 10 interrupt */
#define ST_DHA_NOT_PF       0000040u            /* diagnostic hardware assembly status 10 (~power fail) */
#define ST_DHA_DEVSTAT_MASK 0000037u            /* diagnostic hardware assembly status 11-15 mask */
#define ST_XFR_IRQ          0000020u            /* (X) data transfer interrupt */
#define ST_ST11_SR          0000020u            /* device status 11 service request */
#define ST_CLRIF_IRQ        0000010u            /* (C) clear interface interrupt */
#define ST_IOSYS_IRQ        0000004u            /* (Y) I/O system interrupt */
#define ST_XFERERR_IRQ      0000002u            /* (E) transfer error interrupt */
#define ST_XFR_TMR_IRQ      0000001u            /* (T) transfer timer interrupt */

#define ST_DEVIRQ_SHIFT     5                   /* device status 8-10 interrupt request alignment shift */

#define ST_DEVIRQ(n)        ((n) << ST_DEVIRQ_SHIFT & ST_DEVIRQ_MASK)

#define ST_CLRIL            (ST_CLRIF_IRQ | ST_XFERERR_IRQ) /* conditions that assert the CLRIL signal */

static const uint32 sequence_counter [] = {     /* externally visible sequencer values, indexed by SEQ_STATE */
    ST_SEQ_COUNT_0,                             /*   00 = Idle */
    ST_SEQ_COUNT_1,                             /*   10 = Device_Command_1 */
    ST_SEQ_COUNT_2,                             /*   11 = Device_Flag_1 */
    ST_SEQ_COUNT_3,                             /*   01 = Device_Command_2 */
    ST_SEQ_COUNT_0                              /*   00 = Device_Flag_2 */
    };

static const uint32 reset_irq [8] = {           /* selective reset irq mask values, indexed by CN_RESET */
    ~0u,                                        /*   000 = none */
    ~(ST_XFR_TMR_IRQ | ST_XFERERR_IRQ),         /*   001 = watchdog timer and transfer error */
    ~ST_IOSYS_IRQ,                              /*   010 = I/O system */
    ~ST_CLRIF_IRQ,                              /*   011 = clear interface */
    ~ST_XFR_IRQ,                                /*   100 = data transfer completion */
    ~ST_ST8_IRQ,                                /*   101 = device status 8 */
    ~ST_ST9_IRQ,                                /*   110 = device status 9 */
    ~ST_ST10_IRQ                                /*   111 = device status 10 */
    };

static const BITSET_NAME int_status_names [] = {        /* Interrupt status word names */
    "SIO OK",                                           /*   bit  0 */
    "DIO OK",                                           /*   bit  1 */
    "interrupt",                                        /*   bit  2 */
    "SEQ 1",                                            /*   bit  3 */
    "SEQ 2",                                            /*   bit  4 */
    "device flag",                                      /*   bit  5 */
    "\1\0interrupt status",                             /*   bit  6 */
    NULL,                                               /*   bit  7 */
    "status 8",                                         /*   bit  8 */
    "status 9",                                         /*   bit  9 */
    "status 10",                                        /*   bit 10 */
    "data xfer",                                        /*   bit 11 */
    "clear interface",                                  /*   bit 12 */
    "system",                                           /*   bit 13 */
    "transfer error",                                   /*   bit 14 */
    "transfer timeout"                                  /*   bit 15 */
    };

static const BITSET_NAME dev_status_names [] = {        /* Device status word names */
    "SIO OK",                                           /*   bit  0 */
    "DIO OK",                                           /*   bit  1 */
    "interrupt",                                        /*   bit  2 */
    "SEQ 1",                                            /*   bit  3 */
    "SEQ 2",                                            /*   bit  4 */
    "device flag",                                      /*   bit  5 */
    "device status",                                    /*   bit  6 */
    NULL,                                               /*   bit  7 */
    "DS8",                                              /*   bit  8 */
    "DS9",                                              /*   bit  9 */
    "DS10",                                             /*   bit 10 */
    "DS11",                                             /*   bit 11 */
    "DS12",                                             /*   bit 12 */
    "DS13",                                             /*   bit 13 */
    "DS14",                                             /*   bit 14 */
    "DS15"                                              /*   bit 15 */
    };

static const BITSET_FORMAT int_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (int_status_names, 0, msb_first, has_alt, no_bar) };

static const BITSET_FORMAT dev_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (dev_status_names, 0, msb_first, no_alt, no_bar) };


/* Printer status word.

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | I | seqct | F | 1 | 0 | - | L | L | N | V | U | - | - | printer
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_ONLINE           0000140u            /* online */
#define ST_NOT_READY        0000020u            /* not ready */
#define ST_VFU_12           0000010u            /* VFU channel 12 */
#define ST_VFU_9            0000004u            /* VFU channel 9 */

static const BITSET_NAME prt_status_names [] = {        /* Printer status word names */
    "SIO OK",                                           /*   bit  0 */
    "DIO OK",                                           /*   bit  1 */
    "interrupt",                                        /*   bit  2 */
    "SEQ 1",                                            /*   bit  3 */
    "SEQ 2",                                            /*   bit  4 */
    "device flag",                                      /*   bit  5 */
    "device status",                                    /*   bit  6 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  8 */
    "\1online\0offline",                                /*   bit  9 */
    NULL,                                               /*   bit 10 */
    "\1not ready\0ready",                               /*   bit 11 */
    "VFU 12",                                           /*   bit 12 */
    "VFU 9"                                             /*   bit 13 */
    };

static const BITSET_FORMAT prt_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (prt_status_names, 2, msb_first, has_alt, no_bar) };


/* Interface state */

static HP_WORD control_word     = 0;            /* control word */
static HP_WORD int_status_word  = 0;            /* interrupt status word (bits 8-15) */
static HP_WORD dev_status_word  = 0;            /* device status word (bits 8-15) */
static HP_WORD read_word        = 0;            /* read word */
static HP_WORD write_word       = 0;            /* write word */

static SEQ_STATE sequencer  = Idle;             /* data transfer handshake sequencer */
static uint32    jumper_set = PRINTER_JUMPERS;  /* set of configuration jumpers */

static FLIP_FLOP sio_busy       = CLEAR;        /* SIO busy flip-flop */
static FLIP_FLOP channel_sr     = CLEAR;        /* channel service request flip-flop */
static FLIP_FLOP device_sr      = CLEAR;        /* device service request flip-flop */
static FLIP_FLOP input_xfer     = CLEAR;        /* input transfer flip-flop */
static FLIP_FLOP output_xfer    = CLEAR;        /* output transfer flip-flop */
static FLIP_FLOP read_xfer      = CLEAR;        /* read transfer flip-flop */
static FLIP_FLOP write_xfer     = CLEAR;        /* write transfer flip-flop */
static FLIP_FLOP interrupt_mask = SET;          /* interrupt mask flip-flop */

static FLIP_FLOP device_command = CLEAR;        /* device command flip-flop */
static FLIP_FLOP device_flag    = CLEAR;        /* device flag flip-flop */
static FLIP_FLOP device_end     = CLEAR;        /* device end flip-flop */

static HP_WORD data_out           = 0;          /* external DATA OUT signal bus */
static t_bool  device_command_out = FALSE;      /* external DEV CMD signal state */

static HP_WORD data_in            = 0;          /* external DATA IN signal bus */
static t_bool  device_flag_in     = FALSE;      /* external DEV FLAG signal state */
static t_bool  device_end_in      = FALSE;      /* external DEV END signal state */


/* Diagnostic Hardware Assembly state */

static HP_WORD dha_control_word = 0;            /* Diagnostic Hardware Assembly control word */
static t_bool  power_warning    = FALSE;        /* PFWARN is not asserted to the DHA */


/* Printer state */

static t_bool paper_fault     = TRUE;           /* TRUE if the printer is out of paper */
static t_bool tape_fault      = FALSE;          /* TRUE if there is no punch in a commanded VFU channel */
static t_bool offline_pending = FALSE;          /* TRUE if an offline request is waiting for the printer to finish */
static uint32 overprint_char  = DEL;            /* character to use if overprinted */
static uint32 current_line    = 1;              /* current form line */
static uint32 buffer_index    = 0;              /* current index into the print buffer */

static uint32 form_length;                      /* form length in lines */
static uint8  buffer [BUFFER_SIZE];             /* character and paper advance buffer */
static uint16 VFU [VFU_SIZE];                   /* vertical format unit tape */
static char   vfu_title [LINE_SIZE];            /* descriptive title of the tape currently in the VFU */

static int32  punched_char   = 'O';             /* character to display if VFU channel is punched */
static int32  unpunched_char = '.';             /* character to display if VFU channel is not punched */

static const DELAY_PROPS *dlyptr = &fast_times; /* pointer to the event delay times to use */


/* Interface local SCP support routines */

static CNTLR_INTRF ui_interface;
static t_stat      xfer_service  (UNIT   *uptr);
static t_stat      pulse_service (UNIT   *uptr);
static t_stat      timer_service (UNIT   *uptr);
static t_stat      ui_reset      (DEVICE *dptr);


/* Interface local utility routines */

static t_stat       master_reset          (t_bool programmed_clear);
static void         clear_interface_logic (void);
static void         activate_unit         (UNIT   *uptr);
static void         report_error          (FILE   *stream);
static OUTBOUND_SET set_interrupt         (uint32 interrupt);
static OUTBOUND_SET set_device_status     (uint32 status_mask, uint32 new_status_word);
static OUTBOUND_SET handshake_xfer        (void);


/* Diagnostic Hardware Assembly local SCP support routines */

static t_stat diag_service (UNIT *uptr);


/* Diagnostic Hardware Assembly local utility routines */

static t_stat       diag_reset   (t_bool programmed_clear);
static OUTBOUND_SET diag_control (uint32 control_word);


/* Printer local SCP support routines */

static t_stat lp_service        (UNIT *uptr);
static t_stat lp_attach         (UNIT *uptr, CONST char *cptr);
static t_stat lp_detach         (UNIT *uptr);

static t_stat lp_set_mode       (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_model      (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_on_offline (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_vfu        (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_show_mode      (FILE *st,   UNIT *uptr,  int32 value,      CONST void *desc);
static t_stat lp_show_vfu       (FILE *st,   UNIT *uptr,  int32 value,      CONST void *desc);


/* Printer local utility routines */

static t_stat       lp_reset        (t_bool programmed_clear);
static OUTBOUND_SET lp_control      (uint32 control_word);
static t_bool       lp_set_alarm    (UNIT   *uptr);
static t_bool       lp_set_locality (UNIT   *uptr, LOCALITY printer_state);
static t_stat       lp_load_vfu     (UNIT   *uptr, FILE *vf);
static int32        lp_read_line    (FILE   *vf,   char *line, uint32 size);


/* Interface SCP data structures */


/* Device information block */

static DIB lp_dib = {
    &ui_interface,                              /* device interface */
    14,                                         /* device number */
    11,                                         /* service request number */
    18,                                         /* interrupt priority */
    INTMASK_E                                   /* interrupt mask */
    };


/* Unit list */

#define UNIT_FLAGS          (UNIT_ATTABLE | UNIT_SEQ | UNIT_EXPAND | UNIT_OFFLINE)

static UNIT lp_unit [] = {
    { UDATA (&xfer_service,  UNIT_FLAGS | UNIT_2617, 0), 0          },
    { UDATA (&pulse_service, UNIT_DIS,               0), PULSE_TIME },
    { UDATA (&timer_service, UNIT_DIS,               0), XFER_TIME  }
    };


/* Register list.

   The list consists of the interface registers followed by the Diagnostic
   Hardware Assembly registers and then the printer registers.


   Implementation notes:

    1. The DHA hardware buffers control word bits 6-10 to LEDs.  Inspection and
       user confirmation of the control word state is required by the interface
       diagnostic.  In simulation, bits 6-10 of the control word are presented
       as the CNLED register to allow an ASSERT command to test this subrange of
       bits with single commands.
*/

static REG lp_reg [] = {
/*    Macro   Name    Location                  Radix     Width      Offset      Depth            Flags        */
/*    ------  ------  ------------------------  -----  ------------  ------  -------------  ------------------ */
    { FLDATA (SIOBSY, sio_busy,                                        0)                                      },
    { FLDATA (CHANSR, channel_sr,                                      0)                                      },
    { FLDATA (DEVSR,  device_sr,                                       0)                                      },
    { FLDATA (INXFR,  input_xfer,                                      0)                                      },
    { FLDATA (OUTXFR, output_xfer,                                     0)                                      },
    { FLDATA (RDXFR,  read_xfer,                                       0)                                      },
    { FLDATA (WRXFR,  write_xfer,                                      0)                                      },
    { FLDATA (INTMSK, interrupt_mask,                                  0)                                      },

    { FLDATA (DEVCMD, device_command,                                  0)                                      },
    { FLDATA (DEVFLG, device_flag,                                     0)                                      },
    { FLDATA (DEVEND, device_end,                                      0)                                      },

    { DRDATA (SEQSTA, sequencer,                            8),                             PV_LEFT            },
    { ORDATA (CNTL,   control_word,                        16),                             PV_RZRO            },
    { ORDATA (ISTAT,  int_status_word,                     16),                             PV_RZRO            },
    { ORDATA (DSTAT,  dev_status_word,                     16),                             PV_RZRO            },
    { ORDATA (READ,   read_word,                           16),                             PV_RZRO | REG_X    },
    { ORDATA (WRITE,  write_word,                          16),                             PV_RZRO | REG_X    },
    { YRDATA (J2WX,   jumper_set,                          10,                              PV_RZRO)           },

    { ORDATA (DATOUT, data_out,                            16),                             PV_RZRO | REG_X    },
    { ORDATA (DATIN,  data_in,                             16),                             PV_RZRO | REG_X    },

    { FLDATA (DCOUT,  device_command_out,                              0)                                      },
    { FLDATA (DFIN,   device_flag_in,                                  0)                                      },
    { FLDATA (DENDIN, device_end_in,                                   0)                                      },

      DIB_REGS (lp_dib),

    { ORDATA (DIAGCN, dha_control_word,                    16),                             PV_RZRO            },
    { GRDATA (CNLED,  control_word,               2,        5,         5),                  PV_RZRO            },
    { FLDATA (PFWARN, power_warning,                                   0)                                      },

    { FLDATA (PFAULT, paper_fault,                                     0)                                      },
    { FLDATA (TFAULT, tape_fault,                                      0)                                      },
    { FLDATA (OLPEND, offline_pending,                                 0)                                      },

    { DRDATA (PRLINE, current_line,                         8),                             PV_LEFT            },
    { DRDATA (BUFIDX, buffer_index,                         8),                             PV_LEFT            },
    { BRDATA (PRTBUF, buffer,                     8,        8,               BUFFER_SIZE),  PV_RZRO | REG_A    },
    { ORDATA (OVPCHR, overprint_char,                       8),                             PV_RZRO | REG_A    },

    { DRDATA (FORMLN, form_length,                          8),                             PV_LEFT | REG_RO   },
    { BRDATA (TITLE,  vfu_title,                  8,        8,                LINE_SIZE),             REG_HRO  },
    { BRDATA (VFU,    VFU,                        2,    VFU_WIDTH,            VFU_SIZE),    PV_RZRO | REG_RO   },
    { ORDATA (PUNCHR, punched_char,                         8),                             PV_RZRO | REG_A    },
    { ORDATA (UNPCHR, unpunched_char,                       8),                             PV_RZRO | REG_A    },

    { DRDATA (BTIME,  fast_times.buffer_load,              24),                             PV_LEFT | REG_NZ   },
    { DRDATA (PTIME,  fast_times.print,                    24),                             PV_LEFT | REG_NZ   },
    { DRDATA (STIME,  fast_times.advance,                  24),                             PV_LEFT | REG_NZ   },
    { DRDATA (POS,    lp_unit [0].pos,                  T_ADDR_W),                          PV_LEFT            },
    { DRDATA (UWAIT,  lp_unit [0].wait,                    32),                             PV_LEFT | REG_HRO  },

    { NULL }
    };


/* Modifier list */

typedef enum {                                  /* Device modes */
    Fast_Time,                                  /*   use optimized timing */
    Real_Time,                                  /*   use realistic timing */
    Printer,                                    /*   connect to the printer */
    Diagnostic                                  /*   connect to the DHA */
    } DEVICE_MODES;

static MTAB lp_mod [] = {
/*    Mask Value    Match Value   Print String       Match String  Validation          Display  Descriptor */
/*    ------------  ------------  -----------------  ------------  ------------------  -------  ---------- */
    { UNIT_MODEL,   UNIT_2607,    "2607",            "2607",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,   UNIT_2613,    "2613",            "2613",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,   UNIT_2617,    "2617",            "2617",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,   UNIT_2618,    "2618",            "2618",       &lp_set_model,      NULL,    NULL       },

    { UNIT_OFFLINE, UNIT_OFFLINE, "offline",         "OFFLINE",    &lp_set_on_offline, NULL,    NULL       },
    { UNIT_OFFLINE, 0,            "online",          "ONLINE",     &lp_set_on_offline, NULL,    NULL,      },

    { UNIT_EXPAND,  UNIT_EXPAND,  "expanded output", "EXPAND",     NULL,               NULL,    NULL       },
    { UNIT_EXPAND,  0,            "compact output",  "COMPACT",    NULL,               NULL,    NULL,      },

/*    Entry Flags          Value        Print String  Match String  Validation    Display        Descriptor       */
/*    -------------------  -----------  ------------  ------------  ------------  -------------  ---------------- */
    { MTAB_XDV,            Fast_Time,   NULL,         "FASTTIME",   &lp_set_mode, NULL,          NULL             },
    { MTAB_XDV,            Real_Time,   NULL,         "REALTIME",   &lp_set_mode, NULL,          NULL             },
    { MTAB_XDV,            Printer,     NULL,         "PRINTER",    &lp_set_mode, NULL,          NULL             },
    { MTAB_XDV,            Diagnostic,  NULL,         "DIAGNOSTIC", &lp_set_mode, NULL,          NULL             },
    { MTAB_XDV,            0,           "MODES",      NULL,         NULL,         &lp_show_mode, NULL             },

    { MTAB_XDV,            VAL_DEVNO,   "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &lp_dib },
    { MTAB_XDV,            VAL_INTMASK, "INTMASK",    "INTMASK",    &hp_set_dib,  &hp_show_dib,  (void *) &lp_dib },
    { MTAB_XDV,            VAL_INTPRI,  "INTPRI",     "INTPRI",     &hp_set_dib,  &hp_show_dib,  (void *) &lp_dib },
    { MTAB_XDV,            VAL_SRNO,    "SRNO",       "SRNO",       &hp_set_dib,  &hp_show_dib,  (void *) &lp_dib },

    { MTAB_XDV | MTAB_NMO, 1,           "VFU",        NULL,         NULL,         &lp_show_vfu,  NULL             },
    { MTAB_XDV | MTAB_NC,  0,           "VFU",        "VFU",        &lp_set_vfu,  &lp_show_vfu,  NULL             },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB lp_deb [] = {
    { "CMD",   DEB_CMD   },                     /* controller commands */
    { "CSRW",  DEB_CSRW  },                     /* interface control, status, read, and write actions */
    { "SERV",  DEB_SERV  },                     /* controller unit service scheduling calls */
    { "XFER",  DEB_XFER  },                     /* controller data reads and writes */
    { "STATE", DEB_STATE },                     /* handshake execution state changes */
    { "IOBUS", DEB_IOB   },                     /* interface I/O bus signals and data words */
    { NULL,    0         }
    };


/* Device descriptor */

DEVICE lp_dev = {
    "LP",                                       /* device name */
    lp_unit,                                    /* unit array */
    lp_reg,                                     /* register array */
    lp_mod,                                     /* modifier array */
    3,                                          /* number of units */
    10,                                         /* address radix */
    32,                                         /* address width = 4 GB */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ui_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    &lp_attach,                                 /* attach routine */
    &lp_detach,                                 /* detach routine */
    &lp_dib,                                    /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    lp_deb,                                     /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Interface local SCP support routines */



/* Universal interface.

   The universal interface is installed on the IOP and Multiplexer Channel buses
   and receives direct and programmed I/O commands from the IOP and Multiplexer
   Channel, respectively.  In simulation, the asserted signals on the buses are
   represented as bits in the inbound_signals set.  Each signal is processed
   sequentially in numerical order, and a set of similar outbound_signals is
   assembled and returned to the caller, simulating assertion of the
   corresponding backplane signals.

   After setting the control mode to establish word or byte mode, SIO data
   transfer between the interface and the connected device is initiated by a
   PWRITESTB or READNEXTWD order.  For direct I/O, a DWRITESTB or a DCONTSTB
   with the "acquire" bit set initiates a transfer.

   A sequencer governs the generation of the device handshake signals.  The
   handshake begins with the assertion of the Device Control signal.  In
   response, the device asserts the Device Flag signal.  The interface then
   denies Device Control, and the device denies Device Flag.  For a byte
   transfer, this sequence repeats automatically for the second byte.  Byte
   packing and unpacking is provided by the interface.

   Eight interrupt sources are provided and may be individually set by their
   associated conditions.  A master interrupt enable is provided by setting the
   appropriate control word bit, and the requesting sources may be cleared
   independently.  An interrupt acknowledgement from the IOP clears the master
   interrupt enable to prevent multiple sources from interrupting
   simultaneously.

   The status word returned by a DSTATSTB or PSTATSTB signal consists of
   interface status in the upper byte and either interrupt or device status in
   the lower byte, as selected by a control word bit.


   Implementation notes:

    1. In a hardware transfer abort, READNEXTWD or PWRITESTB causes the
       sequencer to transition to the Device_Command_1 state and set the Device
       End flip-flop, which asserts DEVEND to the multiplexer channel, and then
       the Device End flip-flop is cleared by ACKSR.  In simulation, ACKSR
       occurs before the PREADSTB or PWRITESTB that asserts DEVEND, so the state
       of the Device End flip-flop is saved in the ACKSR handler and is then
       checked in a subsequent PREADSTB or PWRITESTB to assert DEVEND.

    2. In hardware, the SETJMP signal is ignored, and the JMPMET signal is
       asserted continuously when enabled by CHANSO.

    3. In hardware, a power fail warning (PFWARN) is asserted continuously from
       detection until power is lost.  In simulation, the "power_warning" flag
       is set by a PFWARN assertion and is cleared by a power-on reset.  PFWARN
       is used only by the DHA.
*/

static SIGNALS_DATA ui_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;
t_bool         abort_transfer   = FALSE;

dprintf (lp_dev, DEB_IOB, "Received data %06o with signals %s\n",
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

                control_word &= ~(CN_DEVSTAT | CN_IRQ_ENABLE);  /* clear the device status and IRQ enable flip-flops */
                }

            else                                        /* otherwise the request has been reset */
                outbound_signals |= INTPOLLOUT;         /*   so let the IOP know to cancel it */
            break;


        case SETINT:
        case DSETINT:
            outbound_signals |= set_interrupt (ST_IOSYS_IRQ);   /* set the I/O system interrupt flip-flop */
            break;


        case DRESETINT:
            dibptr->interrupt_active = CLEAR;           /* reset the interrupt active flip-flop */
            outbound_signals |= set_interrupt (0);      /*   and check whether another IRQ is pending */
            break;


        case DSETMASK:
            if (dibptr->interrupt_mask == INTMASK_E)            /* if the mask is always enabled */
                interrupt_mask = SET;                           /*   then set the mask flip-flop */
            else                                                /* otherwise */
                interrupt_mask = D_FF (dibptr->interrupt_mask   /*   set the mask flip-flop if the mask bit */
                                       & inbound_value);        /*     is present in the mask value */

            outbound_signals |= set_interrupt (0);              /* check whether an IRQ is pending */
            break;


        case PCONTSTB:
            if (! J2W1_INSTALLED)                       /* if W1 (SR set by Device Status) is not installed */
                device_sr = SET;                        /*   then set the device service request flip-flop */

        /* fall through into the DCONTSTB case */

        case DCONTSTB:
            dprintf (lp_dev, DEB_CSRW,
                     (lp_dev.flags & DEV_DIAG && inbound_value & CN_DHA_FN_ENABLE
                       ? "Control is %s%s | %s\n"
                       : "Control is %s%s\n"),
                     fmt_bitset (inbound_value,
                                 (lp_dev.flags & DEV_DIAG
                                   ? dha_control_format
                                   : prt_control_format)),
                     reset_irq_name [CN_RESET (inbound_value)],
                     dha_fn_name [CN_DHA_FN (inbound_value)]);

            if (inbound_value & CN_MR)                  /* if the programmed master reset bit is set */
                master_reset (TRUE);                    /*   then reset the interface and the control word */

            else if (inbound_value & CN_RIN) {          /* otherwise if the reset interrupt bit is set */
                dibptr->interrupt_request = CLEAR;      /*   then clear the interrupt request */
                int_status_word = 0;                    /*     and all interrupt sources */

                sim_cancel (timer_uptr);                /* cancel the transfer timer */
                control_word = inbound_value;           /*   and set the control word */
                }

            else {                                                          /* otherwise */
                int_status_word &= reset_irq [CN_RESET (inbound_value)];    /*   clear the specified IRQ source */

                if ((inbound_value & CN_RIN_MASK) == CN_RIN_XFR_TMR)    /* if the timer interrupt was cleared */
                    sim_cancel (timer_uptr);                            /*   then stop the timer too */

                else if (CN_XFR_TMR_ENABLE                      /* otherwise if the transfer timer */
                           & ~control_word & inbound_value) {   /*   is enabled with a 0-to-1 transition */
                    sim_cancel (timer_uptr);                    /*     then retrigger */
                    activate_unit (timer_uptr);                 /*       the timer */
                    }

                control_word = inbound_value;                   /* set the control word */
                }

            if (control_word & CN_ACQUIRE) {            /* if the next word is requested */
                device_command = SET;                   /*   then set the device command flip-flop */
                read_xfer      = SET;                   /*     and the read transfer flip-flop */
                outbound_signals |= handshake_xfer ();  /*       and start the device handshake */
                }

            if (lp_dev.flags & DEV_DIAG)                            /* if the DHA is installed */
                outbound_signals |= diag_control (control_word);    /*   then process the DHA-specific controls */
            else                                                    /* otherwise */
                outbound_signals |= lp_control (control_word);      /*   process the device-specific controls */

            break;


        case PSTATSTB:
        case DSTATSTB:
            outbound_value = sequence_counter [sequencer];  /* start with the sequence counter value */

            if (sio_busy == CLEAR                       /* if the interface is inactive */
              && (int_status_word & ST_CLRIL) == 0)     /*   and the clear interface logic IRQ is denied */
                outbound_value |= ST_SIO_OK;            /*     then programmed I/O is enabled */

            if (sequencer == Idle)                      /* if the device is inactive */
                outbound_value |= ST_DIO_OK;            /*   then direct I/O is enabled */

            if (int_status_word)                        /* if any interrupt requests are pending */
                outbound_value |= ST_IRQ_PENDING;       /*   then set the status bit */

            if (device_flag_in)                         /* if the device flag is asserted */
                outbound_value |= ST_DEVFLAG;           /*   then set the status bit */

            if (control_word & CN_DEVSTAT)                      /* if the device status flip-flop is set */
                outbound_value |= ST_DEVSTAT | dev_status_word; /*   then return the device status */
            else                                                /* otherwise */
                outbound_value |= int_status_word;              /*   return the interrupt status */

            dprintf (lp_dev, DEB_CSRW, "Status is %s\n",
                     fmt_bitset (outbound_value,
                                 (control_word & CN_DEVSTAT
                                   ? (lp_dev.flags & DEV_DIAG
                                       ? dev_status_format
                                       : prt_status_format)
                                   : int_status_format)));
            break;


        case DREADSTB:
            outbound_value = (HP_WORD) read_word;       /* return the data input register value */
            break;


        case DWRITESTB:
            write_word = (uint32) inbound_value;        /* store the value in the data output register */

            device_command = SET;                       /* set the device command flip-flop */
            write_xfer     = SET;                       /*   and the write transfer flip-flop */
            outbound_signals |= handshake_xfer ();      /*     and start the device handshake */
            break;


        case DSTARTIO:
            dprintf (lp_dev, DEB_CSRW, "Channel program started\n");

            sio_busy = SET;                             /* set the SIO busy flip-flop */

            mpx_assert_REQ (dibptr);                    /* request the channel */

            channel_sr = SET;                           /* set the service request flip-flop */
            outbound_signals |= SRn;                    /*   and assert a service request */
            break;


        case ACKSR:
            device_sr = CLEAR;                          /* acknowledge the service request */

            abort_transfer = (t_bool) device_end;       /* TRUE if the transfer is to be aborted */
            device_end = CLEAR;                         /* clear the device end flip-flop */
            break;


        case TOGGLESR:
            TOGGLE (channel_sr);                        /* set or clear the channel service request flip-flop */
            break;


        case TOGGLESIOOK:
            TOGGLE (sio_busy);                          /* set or clear the SIO busy flip-flop */

            if (sio_busy == CLEAR)
                dprintf (lp_dev, DEB_CSRW, "Channel program ended\n");
            break;


        case TOGGLEINXFER:
            TOGGLE (input_xfer);                        /* set or clear the input transfer flip-flop */

            device_end_in = FALSE;                      /* clear the external device end condition */
            break;


        case TOGGLEOUTXFER:
            TOGGLE (output_xfer);                       /* set or clear the output transfer flip-flop */

            if (output_xfer == SET)                     /* if starting an output transfer */
                device_sr = SET;                        /*   request the first word to write */

            device_end_in = FALSE;                      /* clear the external device end condition */
            break;


        case PCMD1:
            device_sr = SET;                            /* request the second control word */
            break;


        case READNEXTWD:
            device_command = SET;                       /* set the device command flip-flop */
            read_xfer      = SET;                       /*   and the read transfer flip-flop */
            outbound_signals |= handshake_xfer ();      /*     and start the device handshake */
            break;


        case PREADSTB:
            if (abort_transfer) {                           /* if the transfer has been aborted */
                outbound_value = dibptr->device_number * 4; /*   then return the DRT address */
                outbound_signals |= DEVEND;                 /*     and indicate a device abort */
                }

            else                                            /* otherwise the transfer continues */
                outbound_value = (HP_WORD) read_word;       /*   so return the data input register value */
            break;


        case PWRITESTB:
            if (abort_transfer) {                           /* if the transfer has been aborted */
                outbound_value = dibptr->device_number * 4; /*   then return the DRT address */
                outbound_signals |= DEVEND;                 /*     and indicate a device abort */
                }

            else {                                          /* otherwise the transfer continues */
                write_word = (uint32) inbound_value;        /*   so store the value in the data output register */

                device_command = SET;                       /* set the device command flip-flop */
                write_xfer     = SET;                       /*   and the write transfer flip-flop */
                outbound_signals |= handshake_xfer ();      /*     and start the device handshake */
                }
            break;


        case DEVNODB:
            outbound_value = dibptr->device_number * 4;     /* return the DRT address */
            break;


        case XFERERROR:
            dprintf (lp_dev, DEB_CSRW, "Channel program aborted\n");

            clear_interface_logic ();                   /* clear the interface to abort the transfer */

            outbound_signals |= set_interrupt (ST_XFERERR_IRQ); /* set the transfer error interrupt flip-flop */
            break;


        case CHANSO:
            if (channel_sr | device_sr)                 /* if the interface has requested service */
                outbound_signals |= SRn;                /*   then assert SRn to the channel */

            outbound_signals |= JMPMET;                 /* JMPMET is tied active on this interface */
            break;


        case EOT:
            if (inbound_signals & PREADSTB)             /* if this is the end of a read transfer */
                device_sr = SET;                        /*   then request channel service */
            break;


        case PFWARN:
            power_warning = TRUE;                       /* system power is in the process of failing */
            break;


        case SETJMP:                                    /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }


dprintf (lp_dev, DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Service the transfer handshake.

   This service routine is called once for each state of the device transfer
   handshake.  The handshake sequencer schedules the transfer events with the
   appropriate delays.

   Jumper W10 determines the output polarity of the DEV CMD signal to the
   device, and jumpers W2 and W6 determine the input edges of the DEV FLAG
   signal from the device used to assert and deny the Device Flag, as follows:

     Jumper  Interpretation when removed    Interpretation when installed
     ------  -----------------------------  -----------------------------
       W10   DEV CMD polarity is normal     DEV CMD polarity is inverted

       W2    Flag asserts on leading edge   Flag asserts on trailing edge

       W6    Flag denies on trailing edge   Flag denies on leading edge

   Note that if jumpers W2 and W6 are not installed or removed in pairs, the
   Device Flag asserts and denies on the same edge of the DEV FLAG signal.  In
   this case, the service routine sets the flag on the first call and clears the
   flag on the second call without requiring a change in the incoming signal.


   Implementation notes:

    1. The "device_command_out" and "device_flag_in" variables represent the
       states of the DEV CMD and DEV FLAG signal lines.  Edge detection for the
       Device Flag is accomplished by comparing the current state to the prior
       state.

    2. As the routine was entered by an event timer expiration, the handshake
       sequencer must be called explicitly, and any returned backplane signals
       must be asserted explicitly.

    3. This routine may be called with a NULL "uptr" parameter to update the
       saved last state of the "device_flag_in" variable.  The NULL value
       indicates that this is not part of the normal handshake sequence.
*/

static t_stat xfer_service (UNIT *uptr)
{
static t_bool device_flag_last = FALSE;
t_stat        result;
OUTBOUND_SET  signals;

device_command_out = device_command ^ J2W10_INSTALLED;  /* set device command out; invert if W10 is installed */

if (lp_dev.flags & DEV_DIAG)                            /* if the DHA is connected */
    result = diag_service (uptr);                       /*   then service the diagnostic hardware */
else                                                    /* otherwise */
    result = lp_service (uptr);                         /*   service the connected device */

if (sequencer == Device_Command_1                       /* if Device Command */
  || sequencer == Device_Command_2) {                   /*   is asserted */
    if (device_flag_last != device_flag_in              /*     then if the flag input has changed */
      && J2W2_INSTALLED ^ device_flag_in)               /*     and jumper W2 is in and 1 -> 0 or W2 is out and 0 -> 1 */
        device_flag = SET;                              /*       then Device Flag sets */
    }

else                                                    /* otherwise Device Command is denied */
    if (J2W2_INSTALLED != J2W6_INSTALLED                /*   so if W2 installation differs from W6 installation */
      || (device_flag_last != device_flag_in            /*     or if the flag input has changed */
      && J2W6_INSTALLED ^ device_flag_last))            /*     and jumper W6 is in and 0 -> 1 or W6 is out and 1 -> 0 */
        device_flag = CLEAR;                            /*       then Device Flag clears */

device_flag_last = device_flag_in;                      /* save the current state of the flag */

signals = handshake_xfer ();                            /* continue the handshake */

if (signals & INTREQ)                                   /* if an interrupt request was generated */
    iop_assert_INTREQ (&lp_dib);                        /*   then assert the INTREQ signal */

if (signals & SRn)                                      /* if a service request was generated */
    mpx_assert_SRn (&lp_dib);                           /*   then assert the SRn signal */

return result;                                          /* return the result of the service call */
}


/* Service the device command pulse timer.

   In pulse mode, the DEV CMD signal asserts for 8 microseconds.  This service
   routine is entered to deny DEV CMD.  The transfer service is called directly
   to notify it of Device Command clearing, and the handshake sequencer is then
   called in case the transfer service altered the Device Flag in response.
*/

static t_stat pulse_service (UNIT *uptr)
{
t_stat status;

dprintf (lp_dev, DEB_SERV, "Pulse service entered\n");

device_command = CLEAR;                                 /* clear the device command flip-flop */

status = xfer_service (xfer_uptr);                      /* let the device know that command has denied */
handshake_xfer ();                                      /*   and continue the handshake */

return status;
}


/* Service the transfer timer.

   Setting the appropriate bit in the control word starts the five-second
   transfer timer.  If it expires, this routine is entered.  The transfer timer
   interrupt is set, and if interrupts are enabled, INTREQ is asserted to the
   IOP.  As a convenience to the user, the file attached to the device unit is
   flushed.
*/

static t_stat timer_service (UNIT *uptr)
{
dprintf (lp_dev, DEB_SERV, "Watchdog service entered\n");

if (set_interrupt (ST_XFR_TMR_IRQ) == INTREQ)           /* set the transfer timer interrupt flip-flop */
    iop_assert_INTREQ (&lp_dib);                        /*   and assert the INTREQ signal if enabled */

if (xfer_unit.flags & UNIT_ATT)                         /* if the transfer unit is attached */
    fflush (xfer_unit.fileref);                         /*   then flush any partial output */

return SCPE_OK;                                         /* return success */
}


/* Device reset routine.

   This routine is called for a RESET or RESET LP command.  It is the simulation
   equivalent of the IORESET signal, which is asserted by the front panel LOAD
   and DUMP switches.

   For this interface, IORESET is identical to the Programmed Master Clear
   invoked by setting bit 0 of the control word.


   Implementation notes:

    1. Calling "master_reset" with a FALSE parameter indicates that this is a
       commanded reset.  This allows the connected device-specific reset
       routines to distinguish from a Programmed Master Clear.
*/

static t_stat ui_reset (DEVICE *dptr)
{
return master_reset (FALSE);                            /* perform a non-programmed master reset */
}



/* Interface local utility routines */



/* Master reset.

   A master reset is generated either by an I/O Reset signal or a Programmed
   Master Clear (CIO bit 0).  It sets the interrupt mask, clears any pending or
   active interrupt, clears all interrupt sources, clears the control word,
   clears the read and write registers, resets the handshake sequencer to its
   idle state, clears the interface logic flip-flops, and cancels all active
   event timers.  It also calls pulses the MASTER CLEAR signal line to the
   device for a preset time.


   Implementation notes:

    1. Calling the reset routine for the connected device simulates asserting
       the MASTER CLEAR signal.
*/

static t_stat master_reset (t_bool programmed_clear)
{
interrupt_mask = SET;                                   /* set the interrupt mask flip-flop */

lp_dib.interrupt_request = CLEAR;                       /* clear any current */
lp_dib.interrupt_active  = CLEAR;                       /*   interrupt request */

int_status_word = 0;                                    /* clear all interrupt request sources */

control_word = 0;                                       /* clear the control word */
write_word   = 0;                                       /*   and the output data register */
read_word    = 0;                                       /*     and the input data register */

sequencer = Idle;                                       /* clear the handshake sequencer to the idle state */

read_xfer  = CLEAR;                                     /* clear the read transfer */
write_xfer = CLEAR;                                     /*   and write transfer flip-flops */

device_command = CLEAR;                                 /* clear the device command */
device_flag    = CLEAR;                                 /*   and device flag flip-flops */

data_out      = 0;                                      /* clear the external state */
data_in       = 0;                                      /*   of the I/O lines */
device_end_in = FALSE;                                  /*     and the external device end line */

clear_interface_logic ();                               /* clear the interface to abort any transfer in progress */

sim_cancel (xfer_uptr);                                 /* cancel */
sim_cancel (pulse_uptr);                                /*   any pending */
sim_cancel (timer_uptr);                                /*     event timers */

if (lp_dev.flags & DEV_DIAG)                            /* if the DHA is installed */
    return diag_reset (programmed_clear);               /*   then reset the diagnostic hardware */
else                                                    /* otherwise */
    return lp_reset (programmed_clear);                 /*   reset the device */
}


/* Clear the interface logic.

   The clear interface logic signal is asserted when the channel indicates a
   transfer failure by asserting XFERERROR, or when the device asserts the CLEAR
   INTERFACE signal.  It clears the SIO Busy, Channel and Device Service
   Request, Input Transfer, Output Transfer, and Device End flip-flops.
*/

static void clear_interface_logic (void)
{
sio_busy    = CLEAR;                                    /* clear the SIO busy flip-flop */
channel_sr  = CLEAR;                                    /*   and the channel service request flip-flop */
device_sr   = CLEAR;                                    /*   and the device service request flip-flop */
input_xfer  = CLEAR;                                    /*   and the input transfer flip-flop */
output_xfer = CLEAR;                                    /*   and the output transfer flip-flop */
device_end  = CLEAR;                                    /*   and the device end flip-flop */

return;
}


/* Activate the unit.

   The specified unit is activated using the unit's "wait" time.  If tracing
   is enabled, the activation is logged to the debug file.


   Implementation notes:

    1. A zero-length delay is scheduled, rather than calling the service routine
       directly, so that the status return value from the event service routine
       is correctly passed back to SCP.
*/

static void activate_unit (UNIT *uptr)
{
dprintf (lp_dev, DEB_SERV, "%s delay %u service scheduled\n",
         unit_name [uptr - lp_unit], uptr->wait);

sim_activate (uptr, uptr->wait);                    /* activate the unit */

return;
}


/* Report a stream I/O error to the console.

   If a stream I/O error has been detected, this routine will print an error
   message to the simulation console and clear the stream's error indicator.
*/

static void report_error (FILE *stream)
{
cprintf ("%s simulator printer I/O error: %s\n",        /* report the error to the console */
         sim_name, strerror (errno));

clearerr (stream);                                      /* clear the error */

return;
}


/* Set an interrupt.

   The interrupt bit specified is set in the interrupt status word.  If enabled,
   INTREQ is returned to request an interrupt.

   The routine is also called with a zero "interrupt" parameter value to check
   whether an interrupt should be requested.
*/

static OUTBOUND_SET set_interrupt (uint32 interrupt)
{
int_status_word |= interrupt;                           /* set the specified interrupt flip-flop */

if (int_status_word                                     /* if an interrupt request is present */
  && control_word & CN_IRQ_ENABLE                       /*   and the IRQ enable flip-flop is set */
  && lp_dib.interrupt_active == CLEAR                   /*   and no interrupt is currently active */
  && interrupt_mask == SET) {                           /*   and the interrupt mask is satisfied */
    lp_dib.interrupt_request = SET;                     /*     then request an interrupt */
    return INTREQ;                                      /*       and assert the INTREQ signal */
    }

else                                                    /* otherwise an interrupt request */
    return NO_SIGNALS;                                  /*   cannot be made at this time */
}


/* Set the device status.

   The device status word is masked with the supplied "status_mask" and then the
   corresponding bits of the "new_status_word" are merged in.  If enabled by the
   associated jumpers and the required edge transitions, interrupts for status
   bits 8-10 may be generated.
*/

static OUTBOUND_SET set_device_status (uint32 status_mask, uint32 new_status_word)
{
OUTBOUND_SET outbound_signals = NO_SIGNALS;

if (status_mask & ST_DEVIRQ_MASK) {                         /* if a status interrupt is possible */
    if (J2W4_INSTALLED                                      /*   then if jumper J4 is installed to enable */
      && ~dev_status_word & new_status_word & ST_ST8_IRQ)   /*     and a 0 -> 1 transition occurred on status 8 */
        outbound_signals |= set_interrupt (ST_ST8_IRQ);     /*       then set the status 8 interrupt flip-flop */

    if (J2W8_INSTALLED                                      /* if jumper J8 is installed to enable */
      && ~dev_status_word & new_status_word & ST_ST9_IRQ)   /*   and a 0 -> 1 transition occurred on status 9 */
        outbound_signals |= set_interrupt (ST_ST9_IRQ);     /*     then set the status 9 interrupt flip-flop */

    if (J2W9_INSTALLED                                      /* if jumper J9 is installed to enable */
      && dev_status_word & ~new_status_word & ST_ST10_IRQ)  /*   and a 1 -> 0 transition occurred on status 10 */
        outbound_signals |= set_interrupt (ST_ST10_IRQ);    /*     then set the status 10 interrupt flip-flop */
    }

dev_status_word = dev_status_word & ~status_mask            /* clear the old device status */
                    | new_status_word & status_mask;        /*   and set the new status */

return outbound_signals;                                    /* return INTREQ if any interrupts were requested */
}


/* Start or continue the data transfer handshake.

   This routine implements the two-wire data transfer handshake with the device.
   For each word or byte transferred, the Device Command signal from the
   interface and the Device Flag signal from the device assume these states:

     Command     Flag         State          Next State
      State      State        Action         Transition
     --------  --------  ----------------  --------------
     denied    denied    device idle       Command sets
     asserted  denied    device started    Flag sets
     asserted  asserted  device completed  Command clears
     denied    asserted  interface idle    Flag clears

   In hardware, a two-bit gray counter implements a four-state sequencer, with
   three states assigned as follows for a word transfer:

                                Command     Flag
     State  State Action         State      State   Next State Transition
     -----  ------------------  --------  --------  ---------------------
      0 0   idle                denied    denied    read or write command
      1 0   word requested      asserted  denied    Flag sets
      1 1   word started        denied    asserted  Flag clears
      0 0   word completed      denied    denied    ---

   For a two-byte transfer, the states are:

                                Command     Flag
     State  State Action         State      State   Next State Transition
     -----  ------------------  --------  --------  ---------------------
      0 0   idle                denied    denied    read or write command
      1 0   1st byte requested  asserted  denied    Flag sets
      1 1   1st byte started    denied    asserted  Flag clears
      1 0   1st byte completed  asserted  denied    Flag sets
            2nd byte requested
      0 0   2nd byte started    denied    asserted  Flag clears
      0 0   2nd byte completed  denied    denied    ---

   The presence of the asserted Device Flag when the count is 00 differentiates
   between the "2nd byte started" and "operation completed" conditions.

   In simulation, these last two conditions are assigned to separate states, as
   follows:

     Hdwe      Simulation     Command     Flag
     State       State         State      State
     -----  ----------------  --------  --------
      0 0   Idle              denied    denied
      1 0   Device_Command_1  asserted  denied
      1 1   Device_Flag_1     denied    asserted
      1 0   Device_Command_2  asserted  denied
      0 0   Device_Flag_2     denied    asserted
      0 0   Idle              denied    denied

   To provide the proper values to appear in the Sequence Counter field of the
   status word, a mapping array is used to supply the value 00 for the
   Device_Flag_2 state.

   The device service is scheduled after each state transition, except the
   return to the idle state, to detect the change in the Device Command signal
   or to schedule the change in the Device Flag.  The device determines whether
   the service will be entered immediately (at the next poll) or after a delay
   time expires.

   For the diagnostic device, the service routine is entered immediately for all
   transitions.  For the printer device, the service routine is entered
   immediately for Device Flag assertions, but flag denials are scheduled with a
   delay corresponding to the printer operation time.  The operations are as
   follows:

                                         Diagnostic Service  Diagnostic Service
     State             Printer Service    Flag follows Cmd   Flag follows cont.6
     ----------------  ----------------  ------------------  -------------------
     Device_Command_1  set Flag          set Flag            wait for Control.6
     Device_Flag_1     wait for service  clear Flag          wait for Control.6
     Device_Command_2  set Flag          set Flag            wait for Control.6
     Device_Flag_2     wait for service  clear Flag          wait for Control.6

   If the device asserts the DEV END signal in response to Device Command, the
   Device End flip-flop is set, and the sequencer is reset back to the Idle
   state to abort the transfer.  DEV END assertion in any other state is ignored
   until Device Command is set.

   If jumper W3 is installed, DEV CMD is pulsed for 8 microseconds by asserting
   Device Command and scheduling the pulse timer to deny it when the event timer
   expires.

   A DWRITESTB or PWRITESTB signal stores a 16-bit value in the data output
   register.  In word mode, the value is presented continuously on the 16 DATA
   OUT lines.  In byte mode, the upper byte in the data output register is
   presented on both bytes of the DATA OUT lines until the Device Flag sets to
   indicate that the device has accepted the first byte, whereupon the full
   16-bit value is presented on the DATA OUT lines.  The result is that the
   upper byte and then the lower byte appears on the lower byte of the DATA OUT
   lines.

   During byte-mode read cycles, the previously stored full 16-bit output value
   is presented on the DATA OUT lines if J2W7 is removed.  If J2W7 is installed,
   the upper byte and then the lower byte appears on the lower byte.  In other
   words, a byte read with J2W7 installed causes the DATA OUT lines to assume
   the same values in sequence that occur during a byte write.  This is used by
   the diagnostic to test the DATA OUT multiplexer.

   A read is initiated by the READNEXTWD signal or by setting the Acquire bit in
   the control word.  Device Command sets in response.  While Device Command is
   set, the data input register is transparent and passes the value on the Data
   In lines through.  When Device Flag sets, the value on the DATA IN lines is
   latched in the register.  A DREADSTB or PREADSTB signal then enables the
   register onto the IOP Data bus.  With J2W5 installed, the data in register is
   always transparent, and a DREADSTB or PREADSTB signal presents the current
   value on the DATA IN lines to the IOP Data bus.

   In word mode with J2W5 removed, 16-bit data presented at the DATA IN lines is
   passed through the data input register while Device Command is set and is
   latched when the Device Flag sets.  In byte mode with J2W5 removed, the value
   presented on the lower byte of the DATA IN lines is presented to both bytes
   of the data input register, passed through while Device Command is set, and
   latched into both bytes of the register when the Device Flag sets to indicate
   that the device has supplied the first byte.  When Device Command sets for
   the second byte, the value presented on the lower byte of the DATA IN lines
   is presented to the lower byte of the data input register, passed through
   while Device Command is set, and latched into the lower byte when the Device
   Flag sets to indicate that the device has supplied the second byte.  The
   result is that the data input register presents the first byte in both bytes
   of the register and then the second byte presents as the lower byte of the
   register, resulting in a packed 16-bit value.


   Implementation notes:

    1. In hardware, the sequencer moves from state 2 through state 3 to state 0
       when the device flag denies at the end of a word transfer.  For a packed
       byte transfer, the sequencer moves from state 3 to state 0 when the
       device flag asserts for the second byte, with logic holding off the
       "operation done" signal until the flag denies.

       In simulation, the sequencer moves on flag denial directly from
       Device_Flag_1 to Idle for a word transfer and on flag assertion from
       Device_Command_2 to Device_Flag_2 and then on flag denial to Idle for a
       second byte transfer.  The sequence count reported in a status return is
       0 for Device_Flag_2, preserving the appearance of returning to state 0
       while the internal Device_Flag_2 state holds off the "operation done"
       signal.

    2. In hardware, a DEV END signal asserts the Q2 and Q3 qualifiers, enabling
       the sequence counter to proceed through the state sequence back to the
       idle state.  In simulation, the sequencer state is set directly back to
       Idle.

    3. In hardware, with jumper W5 out, the DATA IN latches are transparent in
       the Device_Command_1 and Device_Command_2 states and are latched
       otherwise, i.e., when Device Flag asserts.  With jumper W5 in, the
       latches are transparent always, and a read gets the real-time state of
       the DATA IN lines.  In simulation, the read register is set when Device
       Flag asserts; transparency is not simulated.

    4. The diagnostic tests the byte unpacking and packing multiplexers on the
       DATA OUT and DATA IN lines, so we must simulate the multiplexing
       accurately with respect to the intermediate values before the handshake
       is complete.

    5. The sequencer loop is used only during a device end assertion to move
       from Idle to Device_Command_1 and back to Idle.  All other transitions
       involve unit activation and so exit this routine after the sequence state
       is changed.
*/

static OUTBOUND_SET handshake_xfer (void)
{
const SEQ_STATE entry_state      = sequencer;           /* the state of the sequencer at entry */
t_bool          reset            = FALSE;               /* TRUE if the sequencer is reset */
OUTBOUND_SET    outbound_signals = NO_SIGNALS;
SEQ_STATE       last_state;

do {                                                    /* run the sequencer as long as it advances */
    last_state = sequencer;                             /* save the last state to see if it changes */

    if (sequencer < Device_Command_2                    /* if this is the first byte */
      && control_word & CN_BYTE_XFER                    /*   of a byte transfer */
      && (J2W7_INSTALLED || write_xfer))                /*     and W7 is installed or it's a write transfer */
        data_out = write_word & ~D8_MASK                /*       then the upper 8 bits appear */
                      | UPPER_BYTE (write_word);        /*         in both bytes */
    else                                                /* otherwise */
        data_out = write_word;                          /*   the full 16 bits appear */


    switch (sequencer) {                                /* dispatch the current state */

        case Idle:
            if (device_command == SET) {                /* if device command has been set */
                sequencer = Device_Command_1;           /*   then proceed to the next state */

                if (device_end_in                       /* if external device end asserts */
                  && (read_xfer || write_xfer))         /*   during a transfer */
                    device_command = CLEAR;             /*     then device command is inhibited */

                else {                                  /* otherwise */
                    if (J2W3_INSTALLED)                 /*   if jumper W3 (pulse mode) is installed */
                        activate_unit (pulse_uptr);     /*     then schedule device command denial */

                    activate_unit (xfer_uptr);          /* schedule device flag assertion */
                    }
                }
            break;


        case Device_Command_1:
            if (device_end_in)                          /* if external device end asserts */
                if (read_xfer || write_xfer) {          /*   then if a transfer is in progress */
                    device_end = SET;                   /*     then set the Device End flip-flop to abort */

                    device_command = CLEAR;             /* clear the device command */
                    read_xfer      = CLEAR;             /*   and read transfer  */
                    write_xfer     = CLEAR;             /*     and write transfer flip-flops */

                    sequencer = Idle;                   /* idle the sequencer */
                    reset     = TRUE;                   /*   and indicate that it was reset */

                    device_sr = SET;                    /* request channel service */
                    break;
                    }

                else                                    /* otherwise no transfer is in progress */
                    device_end_in = FALSE;              /*   so clear the signal */

            if (device_flag == SET) {                   /* if the device flag has been set */
                sequencer = Device_Flag_1;              /*   then proceed to the next state */
                device_command = CLEAR;                 /*     and deny device command */

                activate_unit (xfer_uptr);              /* schedule device flag denial */

                if (control_word & CN_BYTE_XFER)            /* if this is a byte transfer */
                    read_word = TO_WORD (data_in, data_in); /*   then the lower 8 bits appear in both bytes */
                else                                        /* otherwise */
                    read_word = data_in;                    /*   the full 16 bits appear */

                if (J2W1_INSTALLED && sio_busy          /* if jumper W1 (status drives SR) is installed */
                  && dev_status_word & ST_ST11_SR)      /*   and a transfer is in progress with status 11 set */
                    device_sr = SET;                    /*     then request channel service */
                }
            break;


        case Device_Flag_1:
            if (device_flag == CLEAR)                   /* if the device flag has been cleared */
                if (control_word & CN_BYTE_XFER) {      /*   then if this is a byte transfer */
                    sequencer = Device_Command_2;       /*     then proceed to the next state */
                    device_command = SET;               /*       and assert device command for the second byte */

                    data_out = write_word;              /* latch the output word */

                    activate_unit (xfer_uptr);          /* schedule device flag assertion */
                    }

                else {                                  /* otherwise the transfer is complete */
                    read_xfer  = CLEAR;                 /*   so clear the read transfer  */
                    write_xfer = CLEAR;                 /*     and write transfer flip-flops */

                    sequencer = Idle;                   /* idle the sequencer */
                    device_sr = SET;                    /*   and request channel service */

                    if (control_word & CN_XFR_IRQ_ENABLE)               /* if a transfer interrupt is requested */
                        outbound_signals |= set_interrupt (ST_XFR_IRQ); /*   then set the transfer interrupt flip-flop */
                    }
            break;


        case Device_Command_2:
            if (device_flag == SET || device_end_in) {  /* if the device flag or external device end has been set */
                sequencer = Device_Flag_2;              /*   then proceed to the next state */
                device_command = CLEAR;

                activate_unit (xfer_uptr);              /* schedule device flag denial */

                read_word &= ~D8_MASK;                  /* clear the lower byte */

                if (device_end_in == FALSE)             /* if the transfer succeeded */
                    read_word |= LOWER_BYTE (data_in);  /*   then merge the received lower byte */
                }
            break;


        case Device_Flag_2:
            if (device_flag == CLEAR) {                 /* if the device flag was cleared */
                read_xfer  = CLEAR;                     /*   then clear the read transfer  */
                write_xfer = CLEAR;                     /*     and write transfer flip-flops */

                sequencer = Idle;                       /* idle the sequencer */
                device_sr = SET;                        /*   and request channel service */

                if (control_word & CN_XFR_IRQ_ENABLE)               /* if a transfer interrupt is requested */
                    outbound_signals |= set_interrupt (ST_XFR_IRQ); /*   then set the transfer interrupt flip-flop */
                }
            break;
        }                                               /* end of state dispatching */
    }
while (sequencer != last_state);                        /* continue as long as the sequence is progressing */


if (DPRINTING (lp_dev, DEB_STATE))
    if (sequencer != entry_state)
        hp_debug (&lp_dev, DEB_STATE, "Sequencer transitioned from the %s state to the %s state\n",
                  state_name [entry_state], state_name [sequencer]);

    else if (reset && device_end)
        hp_debug (&lp_dev, DEB_STATE, "Sequencer reset by device end\n");

if (device_sr && sio_busy)                              /* if the interface has requested service */
    outbound_signals |= SRn;                            /*   then assert SRn to the channel */

return outbound_signals;                                /* return the accumulated signals */
}



/* Diagnostic Hardware Assembly local SCP support routines */



/* Service the transfer handshake for the Diagnostic Hardware Assembly.

   The DHA loops the data out lines back to the data in lines, with bits 11-15
   also connecting to bits 11-15 of the status in lines.  The DHA also may be
   configured to connect either the DEV CMD output or the CONT 6 output to the
   DEV FLAG input.


   Implementation notes:

    1. The DHA transfer service is called with a null pointer to update the
       potential change in the flag state.
*/

static t_stat diag_service (UNIT *uptr)
{
if (uptr)                                               /* trace only if this is a handshake entry */
    dprintf (lp_dev, DEB_SERV, "%s state transfer service entered\n",
             state_name [sequencer]);

if (dha_control_word & DHA_FLAG_SEL)                    /* if in "flag follows control 6" mode */
    device_flag_in = (control_word & CN_DHA_FLAG) != 0; /*   then set the flag from control word bit 6 */
else                                                    /* otherwise */
    device_flag_in = device_command_out;                /*   device flag is connected to device command */

data_in = data_out;                                     /* data in is connected to data out */

set_device_status (ST_DHA_DEVSTAT_MASK, data_out);      /* status bits 11-15 are connected to data out */

return SCPE_OK;
}



/* Diagnostic Hardware Assembly local utility routines */



/* Diagnostic hardware assembly reset.

   When the MASTER CLEAR signal is asserted to the DHA, the master reset bit in
   the DHA control word is set.  In addition, the status bits connected to the
   DATA OUT lines from the interface are cleared, as the interface has cleared
   its output register.

   If this reset was caused by a RESET or RESET LP command, the set of installed
   jumpers in the DHA control word is updated.  This picks up any jumper changes
   made at the user interface.


   Implementation notes:

    1. The DHA transfer service is called with a null pointer to update the
       potential change in the DEV FLAG state that may have occurred by a change
       to the DEV CMD state if the lines are connected.
*/

static t_stat diag_reset (t_bool programmed_clear)
{
if (programmed_clear) {                                 /* if this is a programmed master clear */
    dha_control_word |= DHA_MR;                         /*   then record the master reset */

    set_device_status (ST_DHA_DEVSTAT_MASK, data_out);  /* clear the status bits connected to data out */

    xfer_service (NULL);                                /* update the current device flag state */
    }

else                                                        /* otherwise this is a commanded reset */
    dha_control_word = dha_control_word & DHA_JUMPER_MASK   /*   so refresh the DHA control word */
                         | jumper_set;                      /*     from the jumpers */

return SCPE_OK;
}


/* Process the diagnostic hardware assembly control word.

   This routine is called when a DCONTSTB or PCONTSTB assertion indicates that
   the control word is to be set.  If bit 10 is set, then bits 6-9 represent an
   encoded action to be taken by the DHA.  Two of the actions potentially change
   the state of the device status lines, which may also generate an interrupt if
   properly configured and enabled.  In addition, the DEV FLAG signal may
   change, depending on the state of the "flag follows control bit 6" action,
   which may cause the handshake sequencer to change states.


   Implementation notes:

    1. The jumpers part of the DHA control word is "cleared" to all ones, which
       corresponds to installing all of the jumpers.

    2. The DHA transfer service is called with a null pointer to update the
       potential change in the flag state.

    3. Setting bit 2 of the DHA control word reflects the current state of the
       PON and ~PFWARN signals in status bits 9 and 10, respectively.  Status 9
       is always set, as PON is always active while the machine is operating.
       Status 10 is normally set to indicate that PFWARN is denied.  However, if
       the system power is failing, PFWARN is asserted from detection until
       power is lost.
*/

static OUTBOUND_SET diag_control (uint32 control_word)
{
uint32       new_status;
OUTBOUND_SET outbound_signals = NO_SIGNALS;

if (control_word & CN_DHA_FN_ENABLE)                    /* if the decoder is enabled */
    switch (CN_DHA_FN (control_word)) {                 /*   then decode the DHA command */

        case 0:                                         /* clear the registers */
            dha_control_word = DHA_CLEAR;               /* initialize the DHA control word */
            jumper_set = DHA_CLEAR & DHA_JUMPER_MASK;   /*   and install all of the jumpers */
            break;

        case 2:                                         /* assert the Device End signal */
            device_end_in = TRUE;                       /* set the external device end line */
            break;

        case 4:                                                 /* set the Transfer Error flip-flop */
            outbound_signals = set_interrupt (ST_XFERERR_IRQ);  /* set the transfer error interrupt flip-flop */
            break;

        case 8:                                         /* connect the device flag to control bit 6 */
            dha_control_word |= DHA_FLAG_SEL;           /* set the "flag follows control 6" bit */
            break;

        case 10:                                                /* assert the Clear Interface signal */
            clear_interface_logic ();                           /* clear the interface logic */
            outbound_signals = set_interrupt (ST_CLRIF_IRQ);    /*   and set the clear interface interrupt flip-flop */
            break;

        case 12:                                        /* connect status 8-10 to master clear/power on/power fail */
            dha_control_word |= DHA_STAT_SEL;           /* set the "status follows master clear-power on-power fail" bit */
            break;

        default:                                                        /* remove a jumper */
            dha_control_word &= jumper_map [CN_DHA_FN (control_word)];  /* clear the specified control register bit */
            jumper_set = dha_control_word & DHA_JUMPER_MASK;            /*   and remove the indicated jumper */
            break;
        }


if (dha_control_word & DHA_STAT_SEL) {                  /* if status follows master clear/power on/power fail */
    new_status = ST_DHA_PON;                            /*   then indicate that power is on */

    if (power_warning == FALSE)                         /* if we have seen a PFWARN signal */
        new_status |= ST_DHA_NOT_PF;                    /*   then indicate that power has not failed */

    if (dha_control_word & DHA_MR)                      /* if a master reset is requested */
        new_status |= ST_DHA_MR;                        /*   then indicate a master clear */
    }

else                                                    /* otherwise set the device status */
    new_status = ST_DEVIRQ (CN_DHA_ST (control_word));  /*   from the connected DHA control bits */

outbound_signals |= set_device_status (ST_DEVIRQ_MASK, new_status); /* set the status and test for IRQs */

xfer_service (NULL);                                    /* record the current device flag state */

outbound_signals |= handshake_xfer ();                  /* check for a device handshake transition */

return outbound_signals;                                /* return INTREQ if any interrupts were requested */
}



/* Printer local SCP support routines */



/* Service the transfer handshake for the printer.

   The printer transfer service is called to output a character to the printer
   buffer or to output a format command that causes the buffered line to be
   printed with specified paper movement.

   In hardware, the interface places a character or format code on the lower
   seven data out lines and asserts STROBE (DEV CMD) to the printer.  The
   printer responds by denying DEMAND (asserting DEV FLAG).  The interface then
   denies STROBE and waits for the printer to reassert DEMAND (deny DEV FLAG) to
   indicate that the buffer load or print operation is complete.

   In simulation, this service routine is called twice for each transfer.  It is
   called immediately with Device Command set and then after a variable delay
   with Device Command clear.  In response to the former call, the routine sets
   the Device Flag, loads the character buffer or prints the buffered line, and
   then sets up an event delay corresponding to the operation performed.  In
   response to the latter call, the routine clears the Device Flag and then
   clears the event delay time, so that the routine will be reentered
   immediately when Device Command sets again.

   If a SET LP OFFLINE command or a DETACH LP command simulating an out-of-paper
   condition is given, the printer will not honor the command immediately if
   data exists in the print buffer or the printer is currently printing a line.
   In this case, the action is deferred until the service routine is entered to
   complete a print operation.  At that point, the printer goes offline with
   DEMAND denied.  This leaves the transfer handshake incomplete.  When the
   printer is placed back online, this routine is called to assert DEMAND and
   conclude the handshake.

   Control word bit 10 determines whether the code on the data out lines is
   interpreted as a character (0) or a format command (1).  If there is room in
   the print buffer, the character is loaded.  If not, then depending on the
   model, the printer either discards the character or automatically prints the
   buffer contents, advances the paper one line, and stores the new character in
   the empty buffer.  If a control character is sent but the printer cannot
   print it, a space is loaded in its place.

   A format command causes the current buffer to be printed, and then the paper
   is advanced by a prescribed amount.  Two output modes are provided: compact
   and expanded.

   In compact mode, a printed line is terminated by a CR LF pair, but subsequent
   line spacing is performed by LFs alone.  Also, a top-of-form request will
   emit a FF character instead of the number of LFs required to reach the top of
   the next form, and overprinting is handled by emitting a lone CR at the end
   of the line.  This mode is used when the printer output file will be sent to
   a physical printer connected to the host.

   In expanded mode, paper advance is handled solely by emitting CR LF pairs.
   Overprinting is handled by merging characters in the buffer.  This mode is
   used where the printer output file will be saved or manipulated as a text
   file.

   The format commands recognized by the printer are:

     0 x x 0 0 0 0 -- slew 0 lines (suppress spacing) after printing
          ...
     0 x x 1 1 1 1 -- slew 15 lines after printing

   and:

     1 x x 0 0 0 0 -- slew to VFU channel 1 after printing
          ...
     1 x x 1 0 1 1 -- slew to VFU channel 12 after printing

   A command to slew to a VFU channel that is not punched or to a VFU channel
   other than those defined for the printer will cause a tape fault, and the
   printer will go offline; setting the printer back online will clear the
   fault.  Otherwise, LFs or a FF (compact mode) or CR LF pairs (expanded mode)
   will be added to the buffer to advance the paper the required number of
   lines.

   Not all printers can overprint.  A request to suppress spacing on a printer
   that cannot (e.g., the HP 2607) is treated as a request for single spacing.

   If the stream write fails, an error message is displayed on the simulation
   console, a printer alarm condition is set (which takes the printer offline),
   and SCPE_IOERR is returned to cause a simulation stop to give the user the
   opportunity to fix the problem.  Simulation may then be resumed, either with
   the printer set back online if the problem is fixed, or with the printer
   remaining offline if the problem is uncorrectable.


   Implementation notes:

    1. When a paper-out condition is detected, the 2607 printer goes offline
       only when the next top-of-form is reached.  The 2613/17/18 printers go
       offline as soon as the current line completes.

    2. Because attached files are opened in binary mode, newline translation
       (i.e., from LF to CR LF) is not performed by the host system.  Therefore,
       we write explicit CR LF pairs to end lines, even in compact mode, as
       required for fidelity to HP peripherals.  If bare LFs are used by the
       host system, the printer output file must be postprocessed to remove the
       CRs.

    3. Overprinting in expanded mode is simulated by merging the lines in the
       buffer.  A format command to suppress spacing resets the buffer index but
       saves the previous buffer length as a "high water mark" that will be
       extended if the overlaying line is longer.  This process may be repeated
       as many times as desired before issuing a format command that prints the
       buffer.

       When overlaying characters, if a space overlays a printing character, a
       printing character overlays a space, or a printing character overlays
       itself, then the printing character is retained.  Otherwise, an
       "overprint character" (which defaults to DEL, but can be changed by the
       user) replaces the character in the buffer.

    4. Printers that support 12-channel VFUs treat the VFU format command as
       modulo 16.  Printers that support 8-channel VFUs treat the command as
       modulo 8.

    5. As a convenience to the user, the printer output file is flushed when a
       TOF operation is performed.  This permits inspection of the output file
       from the SCP command prompt while output is ongoing.

    6. The user may examine the TFAULT and PFAULT registers to determine why the
       printer went offline.

    7. The transfer service may be called with a null pointer to update the
       potential change in the flag state.

    8. If printing is attempted with the printer offline, this routine will be
       called with STROBE asserted (device_command_in TRUE) and DEMAND denied
       (device_flag_in TRUE).  The printer ignores STROBE if DEMAND is not
       asserted, so we simply return in this case.  This will hang the handshake
       until the printer is set online, and we are reentered with DEMAND
       asserted.  As a consequence, explicit protection against "uptr->fileref"
       being NULL is not required.

    9. Explicit tests for lowercase and control characters are much faster and
       are used rather than calls to "islower" and "iscntrl", which must
       consider the current locale.
*/

static t_stat lp_service (UNIT *uptr)
{
const t_bool  printing = ((control_word & CN_FORMAT) != 0);    /* TRUE if a print command was received */
static uint32 overprint_index = 0;
PRINTER_TYPE  model;
uint8         data_byte, format_byte;
uint16        channel;
uint32        line_count, slew_count, vfu_status;

if (uptr == NULL)                                       /* if we're called for a state update */
    return SCPE_OK;                                     /*   then return with no other action */
else                                                    /* otherwise */
    model = GET_MODEL (uptr->flags);                    /*   get the printer type */

dprintf (lp_dev, DEB_SERV, "%s state printer service entered\n",
         state_name [sequencer]);

if (device_command_out == FALSE) {                      /* if STROBE has denied */
    if (printing) {                                     /*   then if printing occurred */
        buffer_index = 0;                               /*     then clear the buffer */

        if (paper_fault) {                              /* if an out-of-paper condition is pending */
            if (print_props [model].fault_at_eol        /*   then if the printer faults at the end of any line */
              || current_line == 1)                     /*     or the printer is at the top of the form */
                return lp_detach (uptr);                /*       then complete it now with the printer offline */
            }

        else if (tape_fault) {                          /* otherwise if a referenced VFU channel was not punched */
            dprintf (lp_dev, DEB_CMD, "Commanded VFU channel is not punched\n");
            lp_set_alarm (uptr);                        /*   then set an alarm condition that takes the printer offline */
            return SCPE_OK;
            }

        else if (offline_pending) {                     /* otherwise if a non-alarm offline request is pending */
            lp_set_locality (uptr, Offline);            /*   then take the printer offline now */
            return SCPE_OK;
            }
        }

    device_flag_in = FALSE;                             /* assert DEMAND to complete the handshake */
    uptr->wait = 0;                                     /*   and request direct entry when STROBE next asserts */
    }

else if (device_flag_in == FALSE) {                     /* otherwise if STROBE has asserted while DEMAND is asserted */
    device_flag_in = TRUE;                              /*   then deny DEMAND */

    data_byte = (uint8) (data_out & DATA_MASK);         /* only the lower 7 bits are connected */

    if (printing == FALSE) {                            /* if loading the print buffer */
        if (data_byte > '_'                             /*   then if the character is "lowercase" */
          && print_props [model].char_set == 64)        /*     but the printer doesn't support it */
            data_byte = data_byte - 040;                /*       then shift it to "uppercase" */

        if ((data_byte < ' ' || data_byte == DEL)       /* if the character is a control character */
          && print_props [model].char_set != 128)       /*   but the printer doesn't support it */
            data_byte = ' ';                            /*     then substitute a space */

        if (buffer_index < print_props [model].line_length) {   /* if there is room in the buffer */
            if (overprint_index == 0                            /*   then if not overprinting */
              || buffer_index >= overprint_index                /*     or past the current buffer limit */
              || buffer [buffer_index] == ' ')                  /*     or overprinting a blank */
                buffer [buffer_index] = data_byte;              /*       then store the character */

            else if (data_byte != ' '                           /* otherwise if we're overprinting a character */
              && data_byte != buffer [buffer_index])            /*   with a different character */
                buffer [buffer_index] = (uint8) overprint_char; /*     then substitute the overprint character */

            buffer_index++;                             /* increment the buffer index */

            uptr->wait = dlyptr->buffer_load;           /* schedule the buffer load delay */

            dprintf (lp_dev, DEB_XFER, "Character %s sent to printer\n",
                     fmt_char (data_byte));
            }

        else if (print_props [model].autoprints) {      /* otherwise if a buffer overflow auto-prints */
            dprintf (lp_dev, DEB_CMD, "Buffer overflow printed %u characters on line %u\n",
                     buffer_index, current_line);

            buffer [buffer_index++] = CR;               /* tie off */
            buffer [buffer_index++] = LF;               /*   the current buffer */

            fwrite (buffer, sizeof buffer [0],          /* write the buffer to the printer file */
                    buffer_index, uptr->fileref);

            uptr->pos = (t_addr) ftell (uptr->fileref); /* update the file position */

            current_line = current_line + 1;            /* move the paper one line */

            if (current_line > form_length)             /* if the current line is beyond the end of the form */
                current_line = 1;                       /*   then reset to the top of the next form */

            dprintf (lp_dev, DEB_CMD, "Printer advanced 1 line to line %u\n",
                     current_line);

            overprint_index = 0;                        /* clear any accumulated overprint index */

            buffer [0] = data_byte;                     /* store the character */
            buffer_index = 1;                           /*   in the empty buffer */

            uptr->wait = dlyptr->print                  /* schedule the print delay */
                           + dlyptr->advance            /*   plus the paper advance delay */
                           + dlyptr->buffer_load;       /*   plus the buffer load delay */

            dprintf (lp_dev, DEB_XFER, "Character %s sent to printer\n",
                     fmt_char (data_byte));
            }

        else {
            uptr->wait = dlyptr->buffer_load;           /* schedule the buffer load delay */

            dprintf (lp_dev, DEB_CMD, "Buffer overflow discards character %s\n",
                     fmt_char (data_byte));
            }
        }

    else {                                              /* otherwise this is a print format command */
        dprintf (lp_dev, DEB_XFER, "Format code %03o sent to printer\n",
                 data_byte);

        format_byte = data_byte & FORMAT_MASK;          /* format commands ignore bits 10-11 */

        if (overprint_index > buffer_index)             /* if the overprinted line is longer than the current line */
            buffer_index = overprint_index;             /*   then extend the current buffer index */

        if (buffer_index > 0 && format_byte != FORMAT_SUPPRESS) /* if printing will occur, then trace it */
            dprintf (lp_dev, DEB_CMD, "Printed %u character%s on line %u\n",
                     buffer_index, (buffer_index == 1 ? "" : "s"), current_line);

        if (format_byte == FORMAT_SUPPRESS              /* if this is a "suppress space" request */
          && print_props [model].overprints) {          /*   and the printer is capable of overprinting */
            slew_count = 0;                             /*     then do not slew after printing */

            if (uptr->flags & UNIT_EXPAND) {            /* if the printer is in expanded mode */
                if (buffer_index > overprint_index)     /*   then if the current line is longer than the overprinted line */
                    overprint_index = buffer_index;     /*     then extend the overprinted line */

                buffer_index = 0;                       /* reset the buffer index to overprint the next line */
                }

            else                                        /* otherwise the printer is in compact mode */
                buffer [buffer_index++] = CR;           /*   so overprint by emitting a CR without a LF */

            dprintf (lp_dev, DEB_CMD, "Printer commanded to suppress spacing on line %u\n",
                     current_line);
            }

        else if (format_byte & FORMAT_VFU) {            /* otherwise if this is a VFU command */
            if (print_props [model].vfu_channels == 8)  /*   then if it's an 8-channel VFU */
                format_byte &= FORMAT_VFU_8_MASK;       /*     then only three bits are significant */

            channel = VFU_CHANNEL_1 >> (format_byte - FORMAT_VFU_BIAS - 1); /* set the requested channel */

            dprintf (lp_dev, DEB_CMD, "Printer commanded to slew to VFU channel %u from line %u\n",
                     format_byte - FORMAT_VFU_BIAS, current_line);

            tape_fault = (channel & VFU [0]) == 0;      /* a tape fault occurs if there is no punch in this channel */

            slew_count = 0;                             /* initialize the slew counter */

            do {                                        /* the VFU always slews at least one line */
                slew_count++;                           /* increment the slew counter */
                current_line++;                         /*   and the line counter */

                if (current_line > form_length)         /* if the current line is beyond the end of the form */
                    current_line = 1;                   /*   then reset to the top of the next form */
                }
            while (!tape_fault && (channel & VFU [current_line]) == 0); /* continue until a punch is seen */
            }

        else {                                          /* otherwise it must be a slew command */
            slew_count = format_byte;                   /* get the number of lines to slew */

            if (format_byte == FORMAT_SUPPRESS)         /* if the printer cannot overprint */
                slew_count = 1;                         /*   then the paper advances after printing */

            dprintf (lp_dev, DEB_CMD, "Printer commanded to slew %u line%s from line %u\n",
                     slew_count, (slew_count == 1 ? "" : "s"), current_line);

            current_line = current_line + slew_count;   /* move the current line */

            if (current_line > form_length)                 /* if the current line is beyond the end of the form */
                current_line = current_line - form_length;  /*   then it extends onto the next form */
            }

        if (format_byte == FORMAT_VFU_CHAN_1            /* if a TOF was requested */
          && !(uptr->flags & UNIT_EXPAND)               /*   and the printer is in compact mode */
          && slew_count > 1) {                          /*     and more than one line is needed to reach the TOF */
            if (buffer_index > 0) {                     /*       then if the buffer not empty */
                buffer [buffer_index++] = CR;           /*         then print */
                buffer [buffer_index++] = LF;           /*           the current line */
                }

            buffer [buffer_index++] = FF;               /* emit a FF to move to the TOF */
            }

        else if (slew_count > 0) {                      /* otherwise a slew is needed */
            buffer [buffer_index++] = CR;               /*   then emit a CR LF */
            buffer [buffer_index++] = LF;               /*     to print the current line */

            line_count = slew_count;                    /* get the number of lines to slew */

            while (--line_count > 0) {                  /* while movement is needed */
                if (uptr->flags & UNIT_EXPAND)          /* if the printer is in expanded mode */
                    buffer [buffer_index++] = CR;       /*   then blank lines are CR LF pairs */

                buffer [buffer_index++] = LF;           /* otherwise just LFs are used */
                }
            }

        if (buffer_index > 0) {                         /* if the buffer is not empty */
            fwrite (buffer, sizeof buffer [0],          /*   then write it to the printer file */
                    buffer_index, uptr->fileref);

            overprint_index = 0;                        /* clear any existing overprint index */
            }

        vfu_status = 0;                                 /* assume no punches for channels 9 and 12 */

        if (print_props [model].vfu_channels > 8) {     /* if the printer VFU has more than 8 channels */
            if (VFU [current_line] & VFU_CHANNEL_9)     /*   then if channel 9 is punched for this line */
                vfu_status |= ST_VFU_9;                 /*     then report it in the device status */

            if (VFU [current_line] & VFU_CHANNEL_12)    /* if channel 12 is punched for this line */
                vfu_status |= ST_VFU_12;                /*   then report it in the device status */
            }

        set_device_status (ST_VFU_9 | ST_VFU_12, vfu_status);   /* set the VFU status */

        if (format_byte == FORMAT_VFU_CHAN_1)           /* if a TOF request was performed */
            fflush (uptr->fileref);                     /*   then flush the file buffer for inspection */

        uptr->wait = dlyptr->print                      /* schedule the print delay */
                       + slew_count * dlyptr->advance;  /*   plus the paper advance delay */

        uptr->pos = (t_addr) ftell (uptr->fileref);     /* update the file position */

        if (slew_count > 0)
            dprintf (lp_dev, DEB_CMD, "Printer advanced %u line%s to line %u\n",
                     slew_count, (slew_count == 1 ? "" : "s"), current_line);
        }

    if (ferror (uptr->fileref)) {                       /* if a host file system error occurred */
        report_error (uptr->fileref);                   /*   then report the error to the console */

        lp_set_alarm (uptr);                            /* set an alarm condition */
        return SCPE_IOERR;                              /*   and stop the simulator */
        }
    }

return SCPE_OK;                                         /* return event service success */
}


/* Attach the printer image file.

   The specified file is attached to the indicated unit.  This is the simulation
   equivalent of loading paper into the printer and pressing the ONLINE button.
   The transition from offline to online causes an interrupt.

   A new image file may be requested by giving the "-N" switch to the ATTACH
   command.  If an existing file is specified with "-N", it will be cleared; if
   specified without "-N", printer output will be appended to the end of the
   existing file content.  In all cases, the paper is positioned at the top of
   the form.


   Implementation notes:

    1. If we are called during a RESTORE command to reattach a file previously
       attached when the simulation was SAVEd, the device status and file
       position are not altered.

    2. The pointer to the appropriate event delay times is set in case we are
       being called during a RESTORE command (the assignment is redundant
       otherwise).
*/

static t_stat lp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = attach_unit (uptr, cptr);                      /* attach the specified printer image file */

if (result == SCPE_OK                                   /* if the attach was successful */
  && (sim_switches & SIM_SW_REST) == 0) {               /*   and we are not being called during a RESTORE command */
    set_device_status (ST_NOT_READY, 0);                /*     then clear not-ready status */

    current_line = 1;                                   /* reset the line counter to the top of the form */

    if (fseek (uptr->fileref, 0, SEEK_END) == 0) {      /* append by seeking to the end of the file */
        uptr->pos = (t_addr) ftell (uptr->fileref);     /*   and repositioning if the seek succeeded */

        dprintf (lp_dev, DEB_CMD, "Printer paper loaded\n");

        lp_set_locality (uptr, Online);                 /* set the printer online */
        }

    else {                                              /* otherwise a host file system error occurred */
        report_error (uptr->fileref);                   /*   so report the error to the console */

        lp_set_alarm (uptr);                            /* set an alarm condition */
        result = SCPE_IOERR;                            /*   and report that the attached failed */
        }
    }

paper_fault = FALSE;                                    /* clear any existing paper fault */

if (lp_dev.flags & DEV_REALTIME)                        /* if the printer is in real-time mode */
    dlyptr = &real_times [GET_MODEL (uptr->flags)];     /*   then point at the times for the current model */
else                                                    /* otherwise */
    dlyptr = &fast_times;                               /*   point at the fast times */

return result;                                          /* return the result of the attach */
}


/* Detach the printer image file.

   The specified file is detached from the indicated unit.  This is the
   simulation equivalent of running out of paper or unloading the paper from the
   printer.  The out-of-paper condition cause a paper fault alarm, and the
   printer goes offline.  The transition from online to offline causes an
   interrupt.

   When the printer runs out of paper, it will not go offline until characters
   present in the buffer are printed and paper motion stops.  In addition, the
   2607 printer waits until the paper reaches the top-of-form position before
   going offline.

   In simulation, entering a DETACH LP command while the printer is busy will
   defer the file detach until print operations reach the top of the next form
   (2607) or until the current print operation completes (2613/17/18).  An
   immediate detach may be forced by adding the -F switch to the DETACH command.
   This simulates physically removing the paper from the printer and succeeds
   regardless of the current printer state.


   Implementation notes:

    1. During simulator shutdown, this routine is called for all three units,
       not just the printer unit.  The printer must be detached, even if a
       detach has been deferred, to ensure that the file is closed properly.  We
       do this in response to a detach request with the SIM_SW_SHUT switch
       present.

    2. The DETACH ALL command will fail if any detach routine returns a status
       other than SCPE_OK.  Because a deferred detach is not fatal, we must
       return SCPE_OK, but we still want to print a warning to the user.

    3. Because the 2607 only paper faults at TOF, we must explicitly set the
       offline_pending flag, as lp_set_alarm may not have been called.
*/

static t_stat lp_detach (UNIT *uptr)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */

if (uptr->flags & UNIT_ATTABLE)                         /* if we're being called for the printer unit */
    if ((uptr->flags & UNIT_ATT) == 0)                  /*   then if the unit is not currently attached */
        return SCPE_UNATT;                              /*     then report it */

    else {
        if (sim_switches & (SWMASK ('F') | SIM_SW_SHUT)) {  /* if this is a forced detach or shut down request */
            current_line = 1;                               /*   then reset the printer to TOF to enable detaching */
            sim_cancel (uptr);                              /*     and terminate */
            device_command_out = FALSE;                     /*       any print action in progress */
            }

        if ((print_props [model].fault_at_eol           /* otherwise if the printer faults at the end of any line */
          || current_line == 1)                         /*   or the printer is at the top of the form */
          && lp_set_alarm (uptr)) {                     /*   and a paper alarm is accepted */
            paper_fault = TRUE;                         /*     then set the out-of-paper condition */

            dprintf (lp_dev, DEB_CMD, "Printer is out of paper\n");

            return detach_unit (uptr);                  /*       and detach the unit */
            }

        else {                                          /* otherwise the alarm was rejected at this time */
            paper_fault = TRUE;                         /*   so set the out-of-paper condition */
            offline_pending = TRUE;                     /*     but defer the detach */

            dprintf (lp_dev, DEB_CMD, "Paper out request deferred until print completes\n");

            cprintf ("%s\n", sim_error_text (SCPE_INCOMP)); /* report that the actual detach must be deferred */
            return SCPE_OK;                                 /*   until the buffer has been printed */
            }
        }

else                                                    /* otherwise */
    return SCPE_UNATT;                                  /*   we've been called for the wrong unit */
}


/* Set the device modes.

   This validation routine is entered with the "value" parameter set to one of
   the DEVICE_MODES values.  The device flag implied by the value is set or
   cleared.  The unit, character, and descriptor pointers are not used.


   Implementation notes:

    1. Switching between printer and diagnostic mode sets the configuration
       jumpers accordingly.

    2. Switching between printer and diagnostic mode clears the event delay.
       This is necessary in case the command was entered while an event was
       queued.
*/

static t_stat lp_set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
switch ((DEVICE_MODES) value) {                         /* dispatch the mode to set */

    case Fast_Time:                                     /* entering optimized timing mode */
        lp_dev.flags &= ~DEV_REALTIME;                  /*   so clear the real-time flag */
        dlyptr = &fast_times;                           /*     and point at the fast times */
        break;


    case Real_Time:                                     /* entering realistic timing mode */
        lp_dev.flags |= DEV_REALTIME;                   /*   so set the real-time flag */
        dlyptr = &real_times [GET_MODEL (uptr->flags)]; /*     and point at the times for the current model */
        break;


    case Printer:                                       /* entering printer mode */
        lp_dev.flags &= ~DEV_DIAG;                      /*   so clear the diagnostic flag */
        xfer_unit.wait = 0;                             /*     and clear any event delay that had been set */

        jumper_set = PRINTER_JUMPERS;                   /* set the jumpers for printer operation */
        break;


    case Diagnostic:                                    /* entering diagnostic mode */
        lp_dev.flags |= DEV_DIAG;                       /*   so set the diagnostic flag */
        xfer_unit.wait = 0;                             /*     and clear any event delay that had been set */

        jumper_set = dha_control_word & DHA_JUMPER_MASK;    /* set the jumpers for DHA operation */
        break;
    }

return SCPE_OK;                                         /* mode changes always succeed */
}


/* Set the printer model.

   This validation routine is called to set the model of the printer.  The
   "value" parameter is one of the UNIT_26nn constants that indicates the new
   model.  Validation isn't necessary, except to detect a model change and alter
   the real-time delays accordingly.
*/

static t_stat lp_set_model (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (lp_dev.flags & DEV_REALTIME)                        /* if the printer is in real-time mode */
    dlyptr = &real_times [GET_MODEL (value)];           /*   then use the times for the new model */

return SCPE_OK;                                         /* allow the reassignment to proceed */
}


/* Set the printer online or offline.

   This validation routine is called to set the printer online or offline.  The
   "value" parameter is UNIT_OFFLINE if the printer is going offline and is zero
   if the printer is going online.  This simulates pressing the ON/OFFLINE
   button on the printer.  The unit must be attached (i.e., paper must be
   loaded), before the printer may be set online or offline.

   If the printer is being taken offline, the buffer is checked to see if any
   characters are present.  If they are, or if the printer unit is currently
   scheduled (i.e., executing a print operation), the offline request is
   deferred until printing completes, and the routine returns "Command not
   complete" status to inform the user.  Otherwise, the unit is set offline,
   DEMAND is denied, and DEV END is asserted to indicate that the printer is not
   ready.

   If the printer is being put online and paper is present, the unit is set
   online, and any paper or tape fault present is cleared.  If the sequencer
   indicates an incomplete handshake, as would occur if paper ran out while
   printing, the transfer service is called to complete the handshake by
   asserting DEMAND.  Otherwise, DEMAND is asserted explicitly, and DEV END is
   denied.

   As a special case, a detach (out-of-paper condition) or offline request that
   has been deferred until printing completes may be cancelled by setting the
   printer online.  No other action is taken, because the printer has never
   transitioned to the offline state.

   Transitions between the offline and online state cause interrupts, and INTREQ
   is asserted to the IOP if a transition occurred (but not, e.g., for a SET LP
   OFFLINE command where the printer is already offline).


   Implementation notes:

    1. Although a deferred offline request is not fatal, we return SCPE_INCOMP
       to prevent "set_cmd" from setting the UNIT_OFFLINE bit in the unit flags
       before the printer actually goes offline.
*/

static t_stat lp_set_on_offline (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if ((uptr->flags & UNIT_ATT) == 0)                      /* if the printer is detached */
    return SCPE_UNATT;                                  /*   then it can't be set online or offline */

else if (value == UNIT_ONLINE)                          /* otherwise if this is an online request */
    if (paper_fault && offline_pending) {               /*   then if an out-of-paper condition is deferred */
        paper_fault = FALSE;                            /*     then cancel the request */
        offline_pending = FALSE;                        /*       leaving the file attached */
        }

    else                                                /*   otherwise it's a normal online request */
        lp_set_locality (uptr, Online);                 /*     so set the printer online */

else if (lp_set_locality (uptr, Offline) == FALSE) {    /* otherwise if it cannot be set offline now */
    dprintf (lp_dev, DEB_CMD, "Offline request deferred until print completes\n");
    return SCPE_INCOMP;                                 /*   then let the user know */
    }

return SCPE_OK;                                         /* return operation success */
}


/* Set the VFU tape.

   This validation routine is entered to set up the VFU on the printer.  It is
   invoked by one of two commands:

     SET LP VFU
     SET LP VFU=<filename>

   The first form loads the standard 66-line tape into the VFU.  The second form
   loads the VFU with the tape image specified by the filename.  The format of
   the tape image is described in the comments for the "lp_load_vfu" routine.

   On entry, "uptr" points at the printer unit, "cptr" points to the first
   character after the "VFU" keyword, and the "value" and "desc" parameters are
   unused.  If "cptr" is NULL, then the first command form was given, and the
   "lp_load_vfu" routine is called with a NULL file stream pointer to indicate
   that the standard VFU tape should be used.  Otherwise, the second command
   form was given, and "cptr" points to the supplied filename.  The file is
   opened, and the "lp_load_vfu" routine is called with the stream pointer to
   load the VFU tape image contained therein.
*/

static t_stat lp_set_vfu (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
FILE   *vfu_stream;
t_stat result;

if (cptr == NULL)                                       /* if a VFU reset is requested */
    result = lp_load_vfu (uptr, NULL);                  /*   then reload the standard VFU tape */

else if (*cptr == '\0')                                 /* otherwise if the filename was omitted */
    return SCPE_MISVAL;                                 /*   then report the missing argument */

else {                                                  /* otherwise the filename was specified */
    vfu_stream = fopen (cptr, "r");                     /*   so attempt to open it */

    if (vfu_stream == NULL)                             /* if the open failed */
        return SCPE_OPENERR;                            /*   then report the error */

    result = lp_load_vfu (uptr, vfu_stream);            /* load the VFU tape from the file */

    fclose (vfu_stream);                                /* close the file */
    }

return result;                                          /* return the result of the load */
}


/* Show the device modes.

   This display routine is called to show the device modes for the printer.  The
   output stream is passed in the "st" parameter, and the other parameters are
   ignored.  The timing mode and connection mode are printed.
*/

static t_stat lp_show_mode (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
fprintf (st, "%s timing, %s mode",                      /* print the timing and connection modes */
         (lp_dev.flags & DEV_REALTIME ? "realistic" : "fast"),
         (lp_dev.flags & DEV_DIAG ? "diagnostic" : "printer"));

return SCPE_OK;
}


/* Show the VFU tape.

   This display routine is called to show the content of the tape currently
   loaded in the printer's VFU.  The "value" parameter indicates how the routine
   was called.  It is 0 if a SHOW LP command was given and 1 if a SHOW LP VFU
   command was issued.  For the former, only the VFU title is displayed.  The
   latter displays the VFU title, followed by a header labelling each of the
   channel columns  and then one line for each line of the form consisting of
   punch and no-punch characters, according to the VFU definition.

   The output stream is passed in the "st" parameter, and the "uptr" and "desc"
   parameters are ignored.


   Implementation notes:

    1. Setting the string precision for the header lines trims them to the
       appropriate number of channels.
*/

static t_stat lp_show_vfu (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
static const char header_1 [] = " Ch 1 Ch 2 Ch 3 Ch 4 Ch 5 Ch 6 Ch 7 Ch 8 Ch 9 Ch10 Ch11 Ch12";
static const char header_2 [] = " ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----";

const PRINTER_TYPE model = GET_MODEL (uptr->flags);             /* the printer model number */
const uint32 channel_count = print_props [model].vfu_channels;  /* the count of VFU channels */
uint32 chan, line, current_channel;

if (value == 0)                                         /* if we're called for a summary display */
    fputs (vfu_title, st);                              /*   then output only the VFU title */

else {                                                      /* otherwise the full VFU definition is requested */
    fprintf (st, "\n%s tape is loaded.\n\n", vfu_title);    /*   so start by displaying the VFU title */

    fprintf (st, "Line %.*s\n", channel_count * 5, header_1);   /* display the */
    fprintf (st, "---- %.*s\n", channel_count * 5, header_2);   /*   channel headers */

    for (line = 1; line <= form_length; line++) {           /* loop through the VFU array */
        fprintf (st, "%3d ", line);                         /* display the current form line number */

        current_channel = VFU_CHANNEL_1;                    /* start with channel 1 */

        for (chan = 1; chan <= channel_count; chan++) {     /* loop through the defined channels */
            fputs ("    ", st);                             /* add some space */

            if (VFU [line] & current_channel)               /* if the current channel is punched for this line */
                fputc (punched_char, st);                   /*   then display a punched location */
            else                                            /* otherwise */
                fputc (unpunched_char, st);                 /*   display an unpunched location */

            current_channel = current_channel >> 1;         /* move to the next channel */
            }

        fputc ('\n', st);                                   /* end the line */
        }
    }

return SCPE_OK;
}



/* Printer local utility routines */



/* Printer reset.

   This routine is called when the MASTER CLEAR signal is asserted to the
   printer.  The "programmed_clear" parameter is TRUE if the routine is called
   for a Programmed Master Clear or IORESET assertion, and FALSE if the routine
   is called for a RESET or RESET LP command.  In the latter case, the presence
   of the "-P" switch indicates that this is a power-on reset.  In either case,
   the interface reset has already been performed; this routine is responsible
   for resetting the printer only.

   In hardware, asserting MASTER CLEAR:

     - clears the input buffer without printing
     - finishes printing the current line and performs any stored VFU action
     - inhibits DEMAND for approximately three milliseconds

   In simulation, the buffer index is reset, a tape fault is cleared, a paper
   fault is determined by the paper status, and any deferred offline command is
   cleared.  Printing is always "complete" at the point of entry, as the actual
   file write is instantaneous from the simulation perspective.  DEMAND is not
   altered, as the printer is "ready" as soon as the command completes.  DEV END
   is reset to the offline status (asserted if the printer is offline, denied if
   online).

   In addition, if a power-on reset (RESET -P) is done, the original FASTTIME
   settings are restored, the standard VFU tape is loaded, and the power failure
   warning is cleared.
*/

static t_stat lp_reset (t_bool programmed_clear)
{
const PRINTER_TYPE model = GET_MODEL (xfer_unit.flags); /* the printer model number */
OUTBOUND_SET signals;
uint32       new_status = 0;
t_stat       result     = SCPE_OK;

if (! programmed_clear && (sim_switches & SWMASK ('P'))) {  /* if this is a commanded power-on reset */
    fast_times.buffer_load = LP_BUFFER_LOAD;                /*   then reset the per-character transfer time, */
    fast_times.print       = LP_PRINT;                      /*     the print and advance-one-line time, */
    fast_times.advance     = LP_ADVANCE;                    /*       and the slew additional lines time */

    result = lp_load_vfu (xfer_uptr, NULL);                 /* load the standard VFU tape */

    power_warning = FALSE;                                  /* clear the power failure warning */
    }

buffer_index = 0;                                       /* clear the buffer without printing */

offline_pending = FALSE;                                /* cancel any pending offline request */

tape_fault  = FALSE;                                    /* clear any tape fault */
paper_fault = ! (xfer_unit.flags & UNIT_ATT);           /*   and set paper fault if out of paper */

if (paper_fault && print_props [model].not_ready)       /* if paper is out and the printer reports it separately */
    new_status |= ST_NOT_READY;                         /*   then set not-ready status */

if (xfer_unit.flags & UNIT_OFFLINE) {                   /* if the printer is offline */
    device_flag_in = TRUE;                              /*   then DEMAND denies while the printer is not ready */
    device_end_in  = TRUE;                              /*     and DEV END asserts while the printer is offline */
    }

else {                                                  /* otherwise the printer is online */
    new_status |= ST_ONLINE;                            /*   so set online status */

    device_flag_in = FALSE;                             /* DEMAND asserts when the printer is ready */
    device_end_in  = FALSE;                             /*   and DEV END denies when the printer is online */
    }

xfer_service (NULL);                                    /* tell the data transfer service that signals have changed */

signals = set_device_status (ST_ONLINE | ST_NOT_READY,  /* set the new device status */
                             new_status);

if (signals & INTREQ)                                   /* if the status change caused an interrupt */
    iop_assert_INTREQ (&lp_dib);                        /*   then assert the INTREQ signal */

return result;                                          /* return the result of the reset */
}


/* Process the printer control word.

   This routine is called when a DCONTSTB or PCONTSTB assertion indicates that
   the control word is to be set.  No direct action is taken by the printer in
   response, so the routine simply returns.
*/

static OUTBOUND_SET lp_control (uint32 control_word)
{
return NO_SIGNALS;                                      /* no special control action is needed */
}


/* Set an alarm condition.

   This routine is called when an alarm condition exists.  An alarm occurs when
   paper is out (paper fault) or a VFU command addresses a channel that does not
   contain a punch (tape fault).  In response, the printer goes offline and,
   for all models except the 2607, becomes not-ready.

   On entry, the routine attempts to set the printer offline.  If this succeeds,
   the printer is set not-ready.  If it fails (for reasons explained in the
   comments for the "lp_set_on_offline" routine), it will be set offline and
   not-ready when printing completes.
*/

static t_bool lp_set_alarm (UNIT *uptr)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */

if (lp_set_locality (uptr, Offline)) {                  /* if the printer went offline */
    if (print_props [model].not_ready)                  /*   then if the printer reports ready status separately */
        set_device_status (ST_NOT_READY, ST_NOT_READY); /*     then set the printer not-ready */

    return TRUE;                                        /* return completion success */
    }

else                                                    /* otherwise the offline request is pending */
    return FALSE;                                       /*   so return deferral status */
}


/* Set the printer locality.

   This routine is called to set the printer online or offline and returns TRUE
   if the request succeeded or FALSE if it was deferred.  An online request
   always succeeds, so it is up to the caller to ensure that going online is
   permissible (e.g., that paper is loaded into the printer).  An offline
   request succeeds only if the printer is idle.  If characters are present in
   the print buffer, or if the printer is printing or slewing, then the request
   is deferred until the current line is complete.

   The printer cable inversely connects DEMAND to the Device Flag input and
   ONLINE to the Device End input.  As both deny when the printer goes offline
   and assert when the printer goes online, Device Flag and Device End assert
   and deny, respectively.

   If the printer goes offline with an operation in progress, Device Flag will
   remain asserted, and the handshake sequencer will remain in the Device_Flag_1
   or Device_Flag_2 state until the printer is set online again.  The transfer
   service routine is informed of these state changes, so that the handshake can
   complete when the printer is again set online.


   Implementation notes:

    1. When called with a NULL parameter, the transfer service routine will
       update its internal device flag state but will take no other action.
       When called with a unit pointer, the routine continues the handshake
       sequence.
*/

static t_bool lp_set_locality (UNIT *uptr, LOCALITY printer_state)
{
OUTBOUND_SET signals;

if (printer_state == Offline) {                         /* if the printer is going offline */
    if (buffer_index == 0                               /*   then if the buffer is empty */
      && sim_is_active (uptr) == FALSE) {               /*     and the printer is idle */
        uptr->flags |= UNIT_OFFLINE;                    /*       then set the printer offline now */

        signals = set_device_status (ST_ONLINE, 0);     /* update the printer status */

        device_flag_in = TRUE;                          /* DEMAND denies while the printer is offline */
        device_end_in  = TRUE;                          /* DEV END asserts while the printer is offline */

        xfer_service (NULL);                            /* inform the service routine of the signal changes */
        }

    else {                                              /*   otherwise the request must wait */
        offline_pending = TRUE;                         /*     until the line is printed */
        return FALSE;                                   /*       and the command is not complete */
        }
    }

else {                                                  /* otherwise the printer is going online */
    uptr->flags &= ~UNIT_OFFLINE;                       /*   so clear the unit flag */

    paper_fault = FALSE;                                /* clear any paper fault */
    tape_fault  = FALSE;                                /*   and any tape fault */

    signals = set_device_status (ST_ONLINE | ST_NOT_READY,  /* set online status */
                                 ST_ONLINE);                /*   and clear not ready status */

    device_flag_in = FALSE;                             /* DEMAND asserts when the printer is online */
    device_end_in  = FALSE;                             /*   and DEV END denies when the printer is online */

    if (sequencer != Idle)                              /* if the transfer handshake is in progress */
        xfer_service (uptr);                            /*   then complete the suspended operation */
    else                                                /* otherwise */
        xfer_service (NULL);                            /*   inform the service routine of the signal changes */
    }

dprintf (lp_dev, DEB_CMD, "Printer set %s\n",
         (printer_state == Offline ? "offline" : "online"));

if (signals & INTREQ)                                   /* if the transition caused an interrupt */
    iop_assert_INTREQ (&lp_dib);                        /*   then assert the INTREQ signal */

offline_pending = FALSE;                                /* the operation completed */
return TRUE;                                            /*   successfully */
}


/* Load the VFU.

   The printer VFU is loaded either with a custom tape image stored in the file
   associated with the stream "vf" or with the standard 66-line tape if the
   stream is NULL.  The "uptr" parameter points to the printer unit.

   The standard VFU tape (02607-80024 for the 8-channel HP 2607 and 02613-80001
   for the 12-channel HP 2613, 2617, and 2618) defines the channels as:

     Chan  Description
     ----  --------------
       1   Top of form
       2   Bottom of form
       3   Single space
       4   Double space
       5   Triple space
       6   Half page
       7   Quarter page
       8   Sixth page
       9   Bottom of form

   ...with channels 10-12 uncommitted.

   A custom tape file starts with a VFU definition line and then contains one
   channel-definition line for each line of the form.  The number of lines
   establishes the form length.  Channel 1 must be dedicated to the top-of-form,
   but the other channels may be defined as desired.

   A semicolon appearing anywhere on a line begins a comment, and the semicolon
   and all following characters are ignored.  Zero-length lines, including lines
   beginning with a semicolon, are ignored.

   Note that a line containing one or more blanks is not a zero-length line, so,
   for example, the line " ; a comment starting in column 2" is not ignored.

   The first (non-ignored) line in the file is a VFU definition line of this
   exact form:

     VFU=<punch characters>,<no-punch character>{,<title>}

   ...where:

     Parameter           Description
     ------------------  -------------------------------------------------------
     punch characters    a string of one or more characters used interchangeably
                         to represent a punched location

     no-punch character  a single character representing a non-punched location

     title               an optional descriptive string printed by the SHOW LP
                         VFU command ("Custom VFU" is used by default)

   If the "VFU" line is missing or not of the correct form, then "Format error"
   status is returned, and the VFU tape is not changed.

   The remaining (non-ignored) lines define the channels punched for each line
   of the printed form.  The line format consists of a sequence of punch,
   no-punch, and "other" characters in channel order.  Each punch or no-punch
   character defines a channel state, starting with channel 1 and proceeding
   left-to-right until all channels for the VFU are defined; if the line
   terminates before all channels are defined, the remaining channels are set to
   the no-punch state.  Any "other" characters (i.e., neither a punch character
   nor a no-punch character) are ignored and may be used freely to delineate the
   tape channels.

   Examples using the standard 66-line tape definition for an 8-channel VFU:


     ; the VFU definition                 |   VFU=1234578,  ; no-punch is a ' '
     VFU=1,0,a binary tape image          |
                                          |   1             ; top of form
     ; the channel definitions            |     345         ; form line 1
                                          |     3           ; form line 2
     10111111   ; top of form             |     34          ; form line 3
     00100000   ; single space            |     3 5         ; form line 4
     0011       ; channels 5-8 no-punch   |     34          ; form line 5
                                          |
     -------------------------------------+-------------------------------------
                                          |
     VFU=X,-,blanks are "others"          |   VFU=TO,.,brackets are "others"
                                          |   ; 1   2   3   4   5   6   7   8
     X  -  X  X  X  X  X  X   ; line 1    |   ;--- --- --- --- --- --- --- ---
     -  -  X  -  -  -  -  -   ; line 2    |    [T]  .  [O] [O] [O] [O] [O] [O]
     -  -  X  X  -  -  -  -   ; line 3    |     .   .  [O]  .   .   .   .   .
                                          |     .   .  [O] [O]  .   .   .   .


   On entry, the "vf" parameter determines whether the standard tape or a custom
   tape is to be loaded.  If "vf" is NULL, a standard 66-line tape is generated
   and stored in the tape buffer.  Otherwise, a custom tape file is read,
   parsed, and assembled VFU entries are stored in the tape buffer.  After
   generation or a successful tape load, the buffer is copied to the VFU array,
   the form length is set, the current line is reset to the top-of-form, and the
   state of VFU channels 9 and 12 are set into the device status.


   Implementation notes:

    1. VFU array entries 1-n correspond to form print lines 1-n.  Entry 0 is the
       logical OR of all of the other entries and is used during VFU format
       command processing to determine if a punch is present somewhere in a
       given channel.
*/

static t_stat lp_load_vfu (UNIT *uptr, FILE *vf)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */
uint32             line, channel, vfu_status;
int32              len;
char               buffer [LINE_SIZE], punch [LINE_SIZE], no_punch;
char               *bptr, *tptr;
uint16             tape [VFU_SIZE] = { 0 };

if (vf == NULL) {                                       /* if the standard VFU is requested */
    tape [ 1] = VFU_CHANNEL_1;                          /*   then punch channel 1 for the top of form */
    tape [60] = VFU_CHANNEL_2 | VFU_CHANNEL_9;          /*     and channels 2 and 9 for the bottom of form */

    for (line = 1; line <= 60; line++) {                            /* load each of the 60 printable lines */
        tape [line] |= VFU_CHANNEL_3                                /* punch channel 3 for single space */
                         | (line %  2 ==  1 ? VFU_CHANNEL_4 : 0)    /* punch channel 4 for double space */
                         | (line %  3 ==  1 ? VFU_CHANNEL_5 : 0)    /* punch channel 5 for triple space */
                         | (line % 30 ==  1 ? VFU_CHANNEL_6 : 0)    /* punch channel 6 for next half page */
                         | (line % 15 ==  1 ? VFU_CHANNEL_7 : 0)    /* punch channel 7 for next quarter page */
                         | (line % 10 ==  1 ? VFU_CHANNEL_8 : 0);   /* punch channel 8 for next sixth page */

        tape [0] |= tape [line];                        /* accumulate the channel punches */
        }

    form_length = 66;                                   /* set the form length */
    strcpy (vfu_title, "Standard VFU");                 /*   and set the title */
    }

else {                                                  /* otherwise load a custom VFU from the file */
    len = lp_read_line (vf, buffer, sizeof buffer);     /* read the first line */

    if (len <= 0                                            /* if there isn't one */
      || strncmp (buffer, "VFU=", strlen ("VFU=")) != 0) {  /*   or it's not a VFU definition statement */
        cputs ("Missing VFU definition line\n");            /*     then complain to the console */
        return SCPE_FMT;                                    /*       and fail with a format error */
        }

    bptr = buffer + strlen ("VFU=");                    /* point at the first control argument */

    tptr = strtok (bptr, ",");                          /* parse the punch token */

    if (tptr == NULL) {                                             /* if it's missing */
        cputs ("Missing punch field in the VFU control line\n");    /*   then complain to the console */
        return SCPE_FMT;                                            /*     and fail with a format error */
        }

    strcpy (punch, tptr);                               /* save the set of punch characters */

    tptr = strtok (NULL, ",");                          /* parse the no-punch token */

    if (tptr == NULL){                                              /* if it's missing */
        cputs ("Missing no-punch field in the VFU control line\n"); /*   then complain to the console */
        return SCPE_FMT;                                            /*     and fail with a format error */
        }

    no_punch = *tptr;                                   /* save the no-punch character */

    tptr = strtok (NULL, ",");                          /* parse the optional title */

    if (tptr != NULL)                                   /* if it's present */
        strcpy (vfu_title, tptr);                       /*   then save the user's title */
    else                                                /* otherwise */
        strcpy (vfu_title, "Custom VFU");               /*   use a generic title */


    for (line = 1; line <= VFU_MAX; line++) {           /* load up to the maximum VFU tape length */
        len = lp_read_line (vf, buffer, sizeof buffer); /* read a tape definition line */

        if (len <= 0)                                   /* if at the EOF or an error occurred */
            break;                                      /*   then the load is complete */

        tptr = buffer;                                  /* point at the first character of the line */

        channel = VFU_CHANNEL_1;                        /* set the channel 1 indicator */

        while (channel != 0 && *tptr != '\0') {         /* loop until the channel or definition is exhausted */
            if (strchr (punch, *tptr) != NULL) {        /* if the character is in the punch set */
                tape [line] |= channel;                 /*   then punch the current channel */
                channel = channel >> 1;                 /*     and move to the next one */
                }

            else if (*tptr == no_punch)                 /* otherwise if the character is the no-punch character */
                channel = channel >> 1;                 /*   then move to the next channel */

            tptr++;                                     /* otherwise the character is neither, so ignore it */
            }

        tape [0] |= tape [line];                        /* accumulate the channel punches */
        }


    if ((tape [1] & VFU_CHANNEL_1) == 0) {                  /* if there is no channel 1 punch in the first line */
        cputs ("Missing punch in channel 1 of line 1\n");   /*   then complain to the console */
        return SCPE_FMT;                                    /*     and fail with a format error */
        }

    form_length = line - 1;                             /* set the form length */
    }

memcpy (VFU, tape, sizeof VFU);                         /* copy the tape buffer to the VFU array */

current_line = 1;                                       /* reset the line counter to the top of the form */

vfu_status = 0;                                         /* assume no punches for channels 9 and 12 */

if (print_props [model].vfu_channels > 8) {             /* if the printer VFU has more than 8 channels */
    if (VFU [1] & VFU_CHANNEL_9)                        /*   then if channel 9 is punched for this line */
        vfu_status |= ST_VFU_9;                         /*     then report it in the device status */

    if (VFU [1] & VFU_CHANNEL_12)                       /* if channel 12 is punched for this line */
        vfu_status |= ST_VFU_12;                        /*   then report it in the device status */
    }

set_device_status (ST_VFU_9 | ST_VFU_12, vfu_status);   /* set the VFU status */

return SCPE_OK;                                         /* the VFU was successfully loaded */
}


/* Read a line from the VFU file.

   This routine reads a line from the VFU tape image file designated by the file
   stream parameter "vf", stores the data in the string buffer pointed to by
   "line" and whose size is given by "size", and returns the length of that
   string.  Comments are stripped from lines that are read, and the routine
   continues to read until a non-zero-length line is found.  If the end of the
   file was reached, the return value is 0.  If a file error occurred, the
   return value is -1.


   Implementation notes:

    1. The routine assumes that the file was opened in text mode, so that
       automatic CRLF-to-LF conversion is done if needed.  This simplifies
       the end-of-line removal.
*/

static int32 lp_read_line (FILE *vf, char *line, uint32 size)
{
char  *result;
int32 len = 0;

while (len == 0) {
    result = fgets (line, size, vf);                    /* get the next line from the file */

    if (result == NULL)                                 /* if an error occurred */
        if (feof (vf))                                  /*   then if the end of file was seen */
            return 0;                                   /*     then return an EOF indication */

        else {                                          /*   otherwise */
            report_error (vf);                          /*     report the error to the console */
            return -1;                                  /*       and return an error indication */
            }

    len = strlen (line);                                /* get the current line length */

    if (len > 0 && line [len - 1] == '\n')              /* if the last character is a newline */
        line [--len] = '\0';                            /*   then remove it and decrease the length */

    result = strchr (line, ';');                        /* search for a comment indicator */

    if (result != NULL) {                               /* if one was found */
        *result = '\0';                                 /*   then truncate the line at that point */
        len = (int32) (result - line);                  /*     and recalculate the line length */
        }
    }

return len;
}
