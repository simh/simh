/* hp2100_lpt.c: HP 2100 12845B Line Printer Interface simulator

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

   LPT          HP 12845B Line Printer Interface

   26-Jun-18    JDB     Revised I/O model
   16-May-17    JDB     Changed REG_A to REG_X
   07-Feb-17    JDB     Passes the 2613/17/18 diagnostic (DSN 145103)
   04-Feb-17    JDB     Rewrote to add the HP 2613/17/18 line printers
   13-May-16    JDB     Modified for revised SCP API function parameter types
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
                        Changed CTIME register width to match documentation
   22-Jan-07    RMS     Added UNIT_TEXT flag
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   19-Nov-04    JDB     Added restart when set online, etc.
   29-Sep-04    JDB     Added SET OFFLINE/ONLINE, POWEROFF/POWERON
                        Fixed status returns for error conditions
                        Fixed TOF handling so form remains on line 0
   03-Jun-04    RMS     Fixed timing (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Revised for extended file support
   24-Oct-02    RMS     Cloned from 12653A

   References:
     - 12845B Line Printer Interface Kit Installation and Service Manual
         (12845-90011, January 1982)
     - HP 2607 Line Printer Diagnostic
         (12987-90004, September 1974)
     - 2613A/2617A/2618A Line Printer Diagnostic Operator's Manual
         (02618-90006, November 1977)
     - RTE Line Printer Driver (DVA12) Reference Manual
         (92001-90010, October 1980)


   The HP 12845B Line Printer Interface Kit connects the 2607A, 2613A, 2617A,
   and 2618A printers to the HP 1000 family.  Each subsystem consists of an
   interface card employing differential line drivers and receivers, an
   interconnecting cable, and an HP 2607A (200 lines per minute), HP 2613 (300
   lpm), HP 2617 (600 lpm), or HP 2618 (1250 lpm) line printer.  The interface
   is supported by RTE driver DVA12 and DOS driver DVR12.  The interface
   supports DMA transfers, but the OS drivers do not use them.

   Two versions of this interface were produced.  The 12845A interface is
   designed to connect the HP 2610 and HP 2614 line printers.  The 12845B
   interface supports these two printers, plus the 2607, 2613, 2617, 2618, 2619,
   and 2631.  The "A" card does not permit DMA transfers, whereas the "B" card
   does.  The latter provides a configuration jumper, "STR", that selectively
   delays the strobe to the printer.  This is needed because DMA output cycles
   assert IOO and STC in same cycle, i.e., data is coincident with strobe, and
   this is insufficient for the 2607, 2610 and 2614.  Placing "STR" in position
   2 delays the strobe for one cycle.  DMA with other printers and all non-DMA
   operation uses "STR" in position 1.

   The 12845B outputs seven data bits to the printer plus these control signals:

     - Master Clear
     - Paper Instruction
     - Strobe

   Master Clear is asserted by CRS and clears the line printer buffer and aborts
   a paper advance in progress (except on the 2613).  It does not abort a print
   cycle in progress.  Paper Instruction asserts to indicate that the data lines
   contain a format word that commands a print-and-slew operation.  Strobe
   asserts to indicate that the signals on the data lines are valid.

   The interface receives these status signals from the printer:

     - Demand
     - Buffer Ready
     - Printer Ready
     - Printer Online
     - VFU Channel 9
     - VFU Channel 12

   Demand denies when the printer is busy and asserts to indicate that the
   printer has completed the requested operation.  Denial causes Strobe to
   deny.  Assertion sets the flag buffer flip-flop if Buffer Ready is also
   asserted.

   Buffer Ready denies when the printer is printing and asserts when printing
   completes and the print buffer may be loaded.  Buffer Ready and Demand are
   reflected in bit 0 of the status word provided to the CPU.  The 2607 has
   Buffer Ready permanently asserted (it is double-buffered and can accept
   characters while printing).

   Printer Ready asserts when power is applied and no fault condition exists.
   It is inverted (i.e., to indicate Not Ready) and presented as bit 14 of the
   status word.

   Printer Online asserts while the printer is online.  It is reflected in bit
   15 of the status word.

   VFU Channel 9 and VFU channel 12 assert when the paper is positioned at the
   print line corresponding to holes punched in the associated VFU tape
   locations.  They are presented as status word bits 13 and 12, respectively.

   The interface status word contains these values for the 2613/17/18 printers:

     Value   Meaning
     ------  -------------------------------------
     140001  Power off or cable disconnected
     100001  Power on, paper loaded, printer ready
     100000  Power on, paper loaded, printer busy
     000000  Power on, print button up
     040000  Power on, paper out or drum gate open

   The 2607 status word values are:

     Value   Meaning
     ------  -----------------------------------------------------
     140001  Power off or cable disconnected
     100001  Power on, paper loaded, printer ready
     100000  Power on, paper loaded, printer busy
     000000  Power on, paper out or print button up or platen open

   The simulator supports a single line printer.  The supported printers are
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
   the software driver requirements.

   Attaching the LPT device simulates loading paper into the printer.  Detaching
   simulates removing or running out of paper.

   The printer may be set offline or online.  It may also be powered off or on
   to accommodate the interface diagnostic.  A diagnostic mode is provided to
   install the 02613-80002 or 02618-80002 VFU tape.  This is the standard VFU
   tape with channel 10 punched for line 59 (BOF - 1), channel 11 punched for
   line 66 (TOF - 1), and channel 12 punched for line 1 (TOF).

   When the ON/OFFLINE button on the printer is pressed, the printer will not go
   offline (i.e., deny the ONLINE signal) if there are characters in the print
   buffer.  Instead, the offline condition is held off until an internal "allow
   offline" signal asserts.  This occurs when the print buffer is empty and the
   print cycle is inactive.

   When a "paper out" condition occurs, the 2613/17/18 printers will go offline
   at the end of the current line.  The 2607 printer waits until the top-of-form
   is seen before going offline.  This has implications for the SET OFFLINE and
   DETACH commands if they are issued while the print buffer contains data or
   the printer unit is busy executing a print action.

   The SET LPT OFFLINE and DETACH LPT commands check for data in the print
   buffer or a print operation in progress.  If either condition is true, they
   set their respective deferred-action flags and display "Command not
   completed."  A SHOW LPT will show that the device is still online and
   attached.  Once simulation is resumed and the print operation completes, the
   printer is set offline or detached as requested.  No console message reports
   this, as it is assumed that the executing program will detect the condition
   and report accordingly.  A subsequent SHOW LPT will indicate the new status.

   A SET LPT ONLINE command when a deferred-action flag is set simply clears the
   flag, which cancels the pending offline or detach condition.

   A RESET LPT command also clears the deferred-action flags and so clears any
   pending offline or detach.  However, it also clears the print buffer and
   terminates any print action in progress, so a SET LPT OFFLINE or DETACH LPT
   will succeed if issued subsequently.

   An immediate detach may be forced by the DETACH -F LPT command.  This
   simulates physically removing the paper from the printer and succeeds
   regardless of the current printer state.


   The interface responds to I/O instructions as follows:

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   - |      ASCII character      | character
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | -   -   -   -   -   -   -   - |        format word        | format
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The printers use only seven data bits, so the MSB of each byte is ignored.
   If the printer's line length is exceeded during write operations, the
   buffered line will be printed, the paper will be advanced one line, and the
   buffer will be cleared to accept the character causing the overflow.

   The format commands recognized by the printers are:

     0 0 0 0 0 0 0 -- slew 0 lines (suppress spacing) after printing *
          ...
     0 0 0 1 1 1 1 -- slew 15 lines after printing

     0 0 1 0 0 0 0 -- slew 16 lines after printing **
          ...
     0 1 1 1 1 1 1 -- slew 63 lines after printing **

      * slew 1 line on the 2607A, which cannot suppress printing.
     ** available only on the 2610A and 2614A

   and:

     1 x x 0 0 0 0 -- slew to VFU channel 1 after printing
          ...
     1 x x 0 1 1 1 -- slew to VFU channel 8 after printing

     1 x x 1 0 0 0 -- slew to VFU channel 9 after printing *
          ...
     1 x x 1 0 1 1 -- slew to VFU channel 12 after printing *

     * available only on the 2613A, 2617A, and 2618A


   Input Data Word format (LIA, LIB, MIA, and MIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | L | R | V | U | -   -   -   -   -   -   -   -   -   -   - | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     L = Online
     R = Not ready
     V = VFU channel 9
     U = VFU channel 12
     D = Demand


   Implementation notes:

     1. The hardware STR jumper that delays the strobe for one cycle for DMA
        compatibility with the 2607, 2610, and 2614 printers is not implemented,
        as it is irrelevant for simulation.

     2. The hardware switch S1 (the NORMAL/TEST switch) that allows printer
        testing without a diagnostic program is not implemented.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"



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

#define BUFFER_SIZE         (CHARS_MAX + VFU_MAX * 2)   /* max chars + max VFU * 2 (for CR LF) */


/* Device flags */

#define DEV_DIAG_SHIFT      (DEV_V_UF + 0)              /* diagnostic VFU tape is installed */
#define DEV_REALTIME_SHIFT  (DEV_V_UF + 1)              /* timing mode is realistic */

#define DEV_DIAG            (1u << DEV_DIAG_SHIFT)      /* diagnostic mode flag */
#define DEV_REALTIME        (1u << DEV_REALTIME_SHIFT)  /* realistic timing flag */


/* Printer unit flags.

   UNIT_V_UF +  7   6   5   4   3   2   1   0
              +---+---+---+---+---+---+---+---+
              | - | - | P | O | E |   model   |
              +---+---+---+---+---+---+---+---+

   Where:

     P = power is off
     O = offline
     E = expanded output
*/

#define UNIT_MODEL_SHIFT    (UNIT_V_UF + 0)     /* printer model ID */
#define UNIT_EXPAND_SHIFT   (UNIT_V_UF + 3)     /* printer uses expanded output */
#define UNIT_OFFLINE_SHIFT  (UNIT_V_UF + 4)     /* printer is offline */
#define UNIT_POWEROFF_SHIFT (UNIT_V_UF + 5)     /* printer power is off */

#define UNIT_MODEL_MASK     0000007u            /* model ID mask */

#define UNIT_MODEL          (UNIT_MODEL_MASK << UNIT_MODEL_SHIFT)
#define UNIT_EXPAND         (1u << UNIT_EXPAND_SHIFT)
#define UNIT_OFFLINE        (1u << UNIT_OFFLINE_SHIFT)
#define UNIT_ONLINE         0
#define UNIT_POWEROFF       (1u << UNIT_POWEROFF_SHIFT)

#define UNIT_2607           (HP_2607 << UNIT_MODEL_SHIFT)
#define UNIT_2613           (HP_2613 << UNIT_MODEL_SHIFT)
#define UNIT_2617           (HP_2617 << UNIT_MODEL_SHIFT)
#define UNIT_2618           (HP_2618 << UNIT_MODEL_SHIFT)


/* Unit flags accessor */

#define GET_MODEL(f)        ((PRINTER_TYPE) (((f) >> UNIT_MODEL_SHIFT) & UNIT_MODEL_MASK))


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


/* Printer control word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   - |      ASCII character      | character
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | -   -   -   -   -   -   -   - |        format word        | format
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_FORMAT           0100000u            /* printer output character/format (0/1) code */

static const BITSET_NAME prt_control_names [] = {       /* Printer control word names */
    "\1format\0character"                               /*   bit 15 */
    };

static const BITSET_FORMAT prt_control_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (prt_control_names, 15, msb_first, has_alt, no_bar) };


/* Printer status word.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | L | R | V | U | -   -   -   -   -   -   -   -   -   -   - | D |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define ST_ONLINE           0100000u            /* online */
#define ST_NOT_READY        0040000u            /* not ready */
#define ST_VFU_9            0020000u            /* VFU channel 9 */
#define ST_VFU_12           0010000u            /* VFU channel 12 */
#define ST_DEMAND           0000001u            /* demand */

#define ST_POWEROFF         (ST_ONLINE | ST_NOT_READY | ST_DEMAND)

static const BITSET_NAME prt_status_names [] = {        /* Printer status word names */
    "\1online\0offline",                                /*   bit 15 */
    "\1not ready\0ready",                               /*   bit 14 */
    "VFU 9",                                            /*   bit 13 */
    "VFU 12",                                           /*   bit 12 */
    NULL,                                               /*   bit 11 */
    NULL,                                               /*   bit 10 */
    NULL,                                               /*   bit  9 */
    NULL,                                               /*   bit  8 */
    NULL,                                               /*   bit  7 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  5 */
    NULL,                                               /*   bit  4 */
    NULL,                                               /*   bit  3 */
    NULL,                                               /*   bit  2 */
    NULL,                                               /*   bit  1 */
    "idle"                                              /*   bit  0 */
    };

static const BITSET_FORMAT prt_status_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (prt_status_names, 0, msb_first, has_alt, no_bar) };


/* Interface state */

typedef struct {
    HP_WORD      output_word;                   /* output data register */
    HP_WORD      status_word;                   /* input data register */
    FLIP_FLOP    command;                       /* command flip-flop */
    FLIP_FLOP    control;                       /* control flip-flop */
    FLIP_FLOP    flag;                          /* flag flip-flop */
    FLIP_FLOP    flag_buffer;                   /* flag buffer flip-flop */
    t_bool       strobe;                        /* STROBE signal to the printer */
    t_bool       demand;                        /* DEMAND signal from the printer */
    } CARD_STATE;

static CARD_STATE lpt;                          /* per-card state */


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

static INTERFACE lp_interface;

static t_stat lp_service (UNIT   *uptr);
static t_stat lp_reset   (DEVICE *dptr);


/* Interface local utility routines */

static void activate_unit (UNIT *uptr);
static void report_error  (FILE *stream);


/* Printer local SCP support routines */

static t_stat lp_attach         (UNIT *uptr, CONST char *cptr);
static t_stat lp_detach         (UNIT *uptr);

static t_stat lp_set_mode       (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_model      (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_on_offline (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_set_vfu        (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat lp_show_mode      (FILE *st,   UNIT *uptr,  int32 value,      CONST void *desc);
static t_stat lp_show_vfu       (FILE *st,   UNIT *uptr,  int32 value,      CONST void *desc);


/* Printer local utility routines */

static void   lp_master_clear (UNIT *uptr);
static t_bool lp_set_alarm    (UNIT *uptr);
static t_bool lp_set_locality (UNIT *uptr, LOCALITY printer_state);
static t_stat lp_load_vfu     (UNIT *uptr, FILE *vf);
static int32  lp_read_line    (FILE *vf,   char *line, uint32 size);


/* Interface SCP data structures */


/* Unit list */

#define UNIT_FLAGS          (UNIT_ATTABLE | UNIT_SEQ | UNIT_EXPAND | UNIT_OFFLINE)

static UNIT lpt_unit [] = {
    { UDATA (&lp_service, UNIT_FLAGS | UNIT_2607, 0), 0 }
    };


/* Device information block */

static DIB lpt_dib = {
    &lp_interface,                              /* the device's I/O interface function pointer */
    LPT,                                        /* the device's select code (02-77) */
    0,                                          /* the card index */
    "12845B Line Printer Interface",            /* the card description */
    NULL                                        /* the ROM description */
    };


/* Register list */

static REG lpt_reg [] = {
/*    Macro   Name    Location                  Radix     Width      Offset      Depth            Flags       */
/*    ------  ------  ------------------------  -----  ------------  ------  -------------  ----------------- */
    { FLDATA (DEVCTL, lpt.control,                                     0)                                     },
    { FLDATA (DEVFLG, lpt.flag,                                        0)                                     },
    { FLDATA (DEVFBF, lpt.flag_buffer,                                 0)                                     },

    { FLDATA (STROBE, lpt.strobe,                                      0)                                     },
    { FLDATA (DEMAND, lpt.demand,                                      0)                                     },

    { ORDATA (OUTPUT, lpt.output_word,                     16),                             PV_RZRO | REG_X   },
    { ORDATA (STATUS, lpt.status_word,                     16),                             PV_RZRO           },

    { FLDATA (PFAULT, paper_fault,                                     0)                                     },
    { FLDATA (TFAULT, tape_fault,                                      0)                                     },
    { FLDATA (OLPEND, offline_pending,                                 0)                                     },

    { DRDATA (PRLINE, current_line,                         8),                             PV_LEFT           },
    { DRDATA (BUFIDX, buffer_index,                         8),                             PV_LEFT           },
    { BRDATA (PRTBUF, buffer,                     8,        8,               BUFFER_SIZE),  PV_RZRO | REG_A   },
    { ORDATA (OVPCHR, overprint_char,                       8),                             PV_RZRO | REG_A   },

    { DRDATA (FORMLN, form_length,                          8),                             PV_LEFT | REG_RO  },
    { BRDATA (TITLE,  vfu_title,                  8,        8,                LINE_SIZE),             REG_HRO },
    { BRDATA (VFU,    VFU,                        2,    VFU_WIDTH,            VFU_SIZE),    PV_RZRO | REG_RO  },
    { ORDATA (PUNCHR, punched_char,                         8),                             PV_RZRO | REG_A   },
    { ORDATA (UNPCHR, unpunched_char,                       8),                             PV_RZRO | REG_A   },

    { DRDATA (BTIME,  fast_times.buffer_load,              24),                             PV_LEFT | REG_NZ  },
    { DRDATA (PTIME,  fast_times.print,                    24),                             PV_LEFT | REG_NZ  },
    { DRDATA (STIME,  fast_times.advance,                  24),                             PV_LEFT | REG_NZ  },
    { DRDATA (POS,    lpt_unit [0].pos,                 T_ADDR_W),                          PV_LEFT           },
    { DRDATA (UWAIT,  lpt_unit [0].wait,                   32),                             PV_LEFT | REG_HRO },

      DIB_REGS (lpt_dib),

    { NULL }
    };


/* Modifier list */

typedef enum {                                  /* Device modes */
    Fast_Time,                                  /*   use optimized timing */
    Real_Time,                                  /*   use realistic timing */
    Printer,                                    /*   use the printer VFU tape */
    Diagnostic                                  /*   use the diagnostic VFU tape */
    } DEVICE_MODES;

static MTAB lpt_mod [] = {
/*    Mask Value     Match Value    Print String       Match String  Validation          Display  Descriptor */
/*    -------------  -------------  -----------------  ------------  ------------------  -------  ---------- */
    { UNIT_MODEL,    UNIT_2607,     "2607",            "2607",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,    UNIT_2613,     "2613",            "2613",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,    UNIT_2617,     "2617",            "2617",       &lp_set_model,      NULL,    NULL       },
    { UNIT_MODEL,    UNIT_2618,     "2618",            "2618",       &lp_set_model,      NULL,    NULL       },

    { UNIT_OFFLINE,  UNIT_OFFLINE,  "offline",         "OFFLINE",    &lp_set_on_offline, NULL,    NULL       },
    { UNIT_OFFLINE,  0,             "online",          "ONLINE",     &lp_set_on_offline, NULL,    NULL,      },

    { UNIT_EXPAND,   UNIT_EXPAND,   "expanded output", "EXPAND",     NULL,               NULL,    NULL       },
    { UNIT_EXPAND,   0,             "compact output",  "COMPACT",    NULL,               NULL,    NULL,      },

    { UNIT_POWEROFF, UNIT_POWEROFF, "power off",       "POWEROFF",   NULL,               NULL,    NULL       },
    { UNIT_POWEROFF, 0,             "power on",        "POWERON",    NULL,               NULL,    NULL,      },

/*    Entry Flags          Value        Print String  Match String  Validation    Display        Descriptor        */
/*    -------------------  -----------  ------------  ------------  ------------  -------------  ----------------- */
    { MTAB_XDV,            Fast_Time,   NULL,         "FASTTIME",   &lp_set_mode, NULL,          NULL              },
    { MTAB_XDV,            Real_Time,   NULL,         "REALTIME",   &lp_set_mode, NULL,          NULL              },
    { MTAB_XDV,            Printer,     NULL,         "PRINTER",    &lp_set_mode, NULL,          NULL              },
    { MTAB_XDV,            Diagnostic,  NULL,         "DIAGNOSTIC", &lp_set_mode, NULL,          NULL              },
    { MTAB_XDV,            0,           "MODES",      NULL,         NULL,         &lp_show_mode, NULL              },

    { MTAB_XDV,             1u,         "SC",         "SC",         &hp_set_dib,  &hp_show_dib,  (void *) &lpt_dib },
    { MTAB_XDV | MTAB_NMO, ~1u,         "DEVNO",      "DEVNO",      &hp_set_dib,  &hp_show_dib,  (void *) &lpt_dib },

    { MTAB_XDV | MTAB_NMO, 1,           "VFU",        NULL,         NULL,         &lp_show_vfu,  NULL              },
    { MTAB_XDV | MTAB_NC,  0,           "VFU",        "VFU",        &lp_set_vfu,  &lp_show_vfu,  NULL              },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB lpt_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "SERV",  TRACE_SERV  },                   /* trace unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE lpt_dev = {
    "LPT",                                      /* device name */
    lpt_unit,                                   /* unit array */
    lpt_reg,                                    /* register array */
    lpt_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    32,                                         /* address width = 4 GB */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &lp_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    &lp_attach,                                 /* attach routine */
    &lp_detach,                                 /* detach routine */
    &lpt_dib,                                   /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    lpt_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Interface local SCP support routines */



/* Line printer interface.

   The line printer interface is installed on the I/O bus and receives I/O
   commands from the CPU and DMA/DCPC channels.  In simulation, the asserted
   signals on the bus are represented as bits in the inbound signal_set.  Each
   signal is processed sequentially in numerical order.


   Implementation notes:

    1. The simulation implements the 12845B Series 1506 interface that inhibits
       STROBE if the printer is offline.

    2. In hardware, an STC signal sets both the Control flip-flop and the
       Information Ready flip-flop.  The latter asserts STROBE to the printer,
       which denies DEMAND in response.  The trailing edge of DEMAND clears the
       Information Ready flip-flop and denies STROBE.  In simulation, STC sets
       the Control flip-flop and calls the device service routine to begin the
       print operation.  This is equivalent to STROBE asserting, then DEMAND
       denying, then STROBE denying.  The Information Ready flip-flop is not
       simulated, as it effectively sets and clears within one CPU instruction.
*/

static SIGNALS_VALUE lp_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            lpt.flag_buffer = CLEAR;                    /* reset the flag buffer */
            lpt.flag        = CLEAR;                    /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            lpt.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (lpt.flag_buffer == SET)                 /* if the flag buffer flip-flop is set */
                lpt.flag = SET;                         /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (lpt.flag == CLEAR)                      /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (lpt.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:
            if (lpt_unit [0].flags & UNIT_POWEROFF)     /* if the printer power is off */
                outbound.value = ST_POWEROFF;           /*   then return the power-off status */

            else if (lpt.demand)                                /* otherwise if DEMAND is asserted */
                outbound.value = lpt.status_word | ST_DEMAND;   /*   then reflect it in the status word */

            else                                        /* otherwise return */
                outbound.value = lpt.status_word;       /*   the (static) status */

            tprintf (lpt_dev, TRACE_CSRW, "Status is %s\n",
                     fmt_bitset (outbound.value, prt_status_format));
            break;


        case ioIOO:
            tprintf (lpt_dev, TRACE_CSRW, "Control is %s | %s\n",
                     fmt_bitset (inbound_value, prt_control_format),
                     fmt_char (inbound_value));

            lpt.output_word = inbound_value;             /* save the character or format word */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            lpt.flag_buffer = SET;                      /* set the flag buffer flip-flop */
            lpt.output_word = 0;                        /*   and clear the output register */
            break;


        case ioCRS:                                     /* Control Reset */
            lp_master_clear (lpt_unit);                 /* CRS asserts MASTER CLEAR to the printer */

            lpt.control = CLEAR;                        /* clear the control flip-flop */
            lpt.command = CLEAR;                        /*   and the command flip-flop */
            lpt.strobe = FALSE;                         /*     and deny STROBE to the printer */

            sim_cancel (lpt_unit);                      /* cancel any operation in progress */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            lpt.control = CLEAR;                        /* clear the control flip-flop */
            lpt.command = CLEAR;                        /*   and the command flip-flop */
            lpt.strobe = FALSE;                         /*     and deny STROBE to the printer */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            lpt.control = SET;                          /* set the control flip-flop */
            lpt.command = SET;                          /*   and the command flip-flop */

            if (lpt.status_word & ST_ONLINE) {          /* if the printer is online */
                lpt.strobe = TRUE;                      /*   then assert STROBE to the printer */

                lpt_unit [0].wait = 0;                  /* set for immediate service entry */
                activate_unit (lpt_unit);               /*   and call the service routine */
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (lpt.control & lpt.flag)                 /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (lpt.control & lpt.flag & lpt.flag_buffer)   /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;        /*   then conditionally assert IRQ */

            if (lpt.flag == SET)                        /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            lpt.flag_buffer = CLEAR;                     /* clear the flag buffer flip-flop */
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


/* Service the printer.

   The printer transfer service is called to output a character to the printer
   buffer or to output a format command that causes the buffered line to be
   printed with specified paper movement.

   In hardware, the interface places a character or format code on the lower
   seven data out lines and asserts STROBE to the printer.  The printer responds
   by denying DEMAND.  The interface then denies STROBE and waits for the
   printer to reassert DEMAND to indicate that the buffer load or print
   operation is complete.

   In simulation, this service routine is called twice for each transfer.  It is
   called immediately when STROBE is asserted with DEMAND asserted and then
   after a variable delay with STROBE denied.  In response to the former call,
   the routine denies DEMAND and STROBE, loads the character buffer or prints
   the buffered line, and then sets up an event delay corresponding to the
   operation performed.  In response to the latter call, the routine asserts
   DEMAND and then clears the event delay time, so that the routine will be
   reentered immediately when STROBE is asserted again.  DEMAND assertion also
   sets the flag buffer flip-flop.

   If a SET LPT OFFLINE or DETACH LPT command simulating an out-of-paper
   condition is given, the printer will not honor the command immediately if
   data exists in the print buffer or the printer is currently printing a line.
   In this case, the action is deferred until this service routine is entered to
   complete a print operation.  At that point, the printer goes offline with
   DEMAND denied.  This leaves the transfer handshake incomplete.  When the
   printer is placed back online, DEMAND is asserted to conclude the handshake.

   Control word bit 15 determines whether the code on the data out lines is
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

    2. A printer going offline, either via a user action or by a paper-out
       condition, does not complete the last handshake.  Instead, DEMAND remains
       denied, so the interface flag never sets.  Typically, this causes an I/O
       timeout in the OS driver, which issues a CLC and downs the device.  When
       the printer is set back online, DEMAND asserts, which sets the flag.
       However, the CLC has cleared the control flip-flop, so no interrupt is
       generated.  When the device is upped, the incomplete request is reissued;
       this results in either a duplicate printed line or a duplicate paper
       movement.

    3. Because attached files are opened in binary mode, newline translation
       (i.e., from LF to CR LF) is not performed by the host system.  Therefore,
       we write explicit CR LF pairs to end lines, even in compact mode, as
       required for fidelity to HP peripherals.  If bare LFs are used by the
       host system, the printer output file must be postprocessed to remove the
       CRs.

    4. Overprinting in expanded mode is simulated by merging the lines in the
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

    5. Printers that support 12-channel VFUs treat the VFU format command as
       modulo 16.  Printers that support 8-channel VFUs treat the command as
       modulo 8.

    6. As a convenience to the user, the printer output stream is flushed when a
       TOF operation is performed.  This permits inspection of the output file
       from the SCP command prompt while output is ongoing.

    7. The user may examine the TFAULT and PFAULT registers to determine why the
       printer went offline.

    8. Explicit tests for lowercase and control characters are much faster and
       are used rather than calls to "islower" and "iscntrl", which must
       consider the current locale.

    9. This routine will not be entered with the printer unit unattached.  The
       printer must be online (and therefore attached) before an STC will
       schedule the service, and "lp_detach" will not detach the unit if it is
       busy unless it is forced (and, in the latter case, "lp_detach" will
       cancel the service event).  So protection against "uptr->fileref" being
       NULL is not required.
*/

static t_stat lp_service (UNIT *uptr)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);                 /* the printer model number */
const t_bool       printing = ((lpt.output_word & CN_FORMAT) != 0); /* TRUE if a print command was received */
static uint32      overprint_index = 0;                             /* the "high-water" mark while overprinting */
uint8              data_byte, format_byte;
uint16             channel;
uint32             line_count, slew_count;

tprintf (lpt_dev, TRACE_SERV, "Printer service entered\n");

if (uptr->flags & UNIT_POWEROFF)                        /* if the printer power is off */
    return SCPE_OK;                                     /*   then no action is taken */

else if (lpt.strobe == FALSE) {                         /* otherwise if STROBE has denied */
    if (printing) {                                     /*   then if printing occurred */
        buffer_index = 0;                               /*     then clear the buffer */

        if (paper_fault) {                              /* if an out-of-paper condition is pending */
            if (print_props [model].fault_at_eol        /*   then if the printer faults at the end of a line */
              || current_line == 1)                     /*     or the printer is at the top of the form */
                return lp_detach (uptr);                /*       then complete it now with the printer offline */
            }

        else if (tape_fault) {                          /* otherwise if a referenced VFU channel was not punched */
            tprintf (lpt_dev, TRACE_CMD, "Commanded VFU channel is not punched\n");
            lp_set_alarm (uptr);                        /*   then set an alarm condition that takes the printer offline */
            return SCPE_OK;
            }

        else if (offline_pending) {                     /* otherwise if a non-alarm offline request is pending */
            lp_set_locality (uptr, Offline);            /*   then take the printer offline now */
            return SCPE_OK;
            }
        }

    lpt.demand = TRUE;                                  /* assert DEMAND to complete the handshake */
    uptr->wait = 0;                                     /*   and request immediate entry when STROBE next asserts */

    lpt.flag_buffer = SET;                              /* set the flag buffer */
    io_assert (&lpt_dev, ioa_ENF);                      /*   and flag flip-flops on DEMAND assertion */
    }

else if (lpt.demand == TRUE) {                          /* otherwise if STROBE has asserted while DEMAND is asserted */
    lpt.demand = FALSE;                                 /*   then deny DEMAND */
    lpt.strobe = FALSE;                                 /*     which resets STROBE */

    data_byte = (uint8) (lpt.output_word & DATA_MASK);  /* only the lower 7 bits are sent to the printer */

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

            tprintf (lpt_dev, TRACE_XFER, "Character %s sent to printer\n",
                     fmt_char (data_byte));
            }

        else if (print_props [model].autoprints) {      /* otherwise if a buffer overflow auto-prints */
            tprintf (lpt_dev, TRACE_CMD, "Buffer overflow printed %u characters on line %u\n",
                     buffer_index, current_line);

            buffer [buffer_index++] = CR;               /*   then tie off */
            buffer [buffer_index++] = LF;               /*     the current buffer */

            fwrite (buffer, sizeof buffer [0],          /* write the buffer to the printer file */
                    buffer_index, uptr->fileref);

            uptr->pos = (t_addr) ftell (uptr->fileref); /* update the file position */

            current_line = current_line + 1;            /* move the paper one line */

            if (current_line > form_length)             /* if the current line is beyond the end of the form */
                current_line = 1;                       /*   then reset to the top of the next form */

            tprintf (lpt_dev, TRACE_CMD, "Printer advanced 1 line to line %u\n",
                     current_line);

            overprint_index = 0;                        /* clear any accumulated overprint index */

            buffer [0] = data_byte;                     /* store the character */
            buffer_index = 1;                           /*   in the empty buffer */

            uptr->wait = dlyptr->print                  /* schedule the print delay */
                           + dlyptr->advance            /*   plus the paper advance delay */
                           + dlyptr->buffer_load;       /*   plus the buffer load delay */

            tprintf (lpt_dev, TRACE_XFER, "Character %s sent to printer\n",
                     fmt_char (data_byte));
            }

        else {                                          /* otherwise the printer discards excess characters */
            uptr->wait = dlyptr->buffer_load;           /*   so just schedule the load delay */

            tprintf (lpt_dev, TRACE_CMD, "Buffer overflow discards character %s\n",
                     fmt_char (data_byte));
            }
        }

    else {                                              /* otherwise this is a print format command */
        tprintf (lpt_dev, TRACE_XFER, "Format code %03o sent to printer\n",
                 data_byte);

        format_byte = data_byte & FORMAT_MASK;          /* format commands ignore bits 5-4 */

        if (overprint_index > buffer_index)             /* if the overprinted line is longer than the current line */
            buffer_index = overprint_index;             /*   then extend the current buffer index */

        if (buffer_index > 0 && format_byte != FORMAT_SUPPRESS) /* if printing will occur, then trace it */
            tprintf (lpt_dev, TRACE_CMD, "Printed %u character%s on line %u\n",
                     buffer_index, (buffer_index == 1 ? "" : "s"), current_line);

        if (format_byte == FORMAT_SUPPRESS              /* if this is a "suppress space" request */
          && print_props [model].overprints) {          /*   and the printer is capable of overprinting */
            slew_count = 0;                             /*     then do not slew after printing */

            if (uptr->flags & UNIT_EXPAND) {            /* if the printer is in expanded mode */
                if (buffer_index > overprint_index)     /*   then if the current length exceeds the overprinted length */
                    overprint_index = buffer_index;     /*     then extend the overprinted line */

                buffer_index = 0;                       /* reset the buffer index to overprint the next line */
                }

            else                                        /* otherwise the printer is in compact mode */
                buffer [buffer_index++] = CR;           /*   so overprint by emitting a CR without a LF */

            tprintf (lpt_dev, TRACE_CMD, "Printer commanded to suppress spacing on line %u\n",
                     current_line);
            }

        else if (format_byte & FORMAT_VFU) {            /* otherwise if this is a VFU command */
            if (print_props [model].vfu_channels == 8)  /*   then if it's an 8-channel VFU */
                format_byte &= FORMAT_VFU_8_MASK;       /*     then only three bits are significant */

            channel = VFU_CHANNEL_1 >> (format_byte - FORMAT_VFU_BIAS - 1); /* set the requested channel */

            tprintf (lpt_dev, TRACE_CMD, "Printer commanded to slew to VFU channel %u from line %u\n",
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
            slew_count = format_byte;                   /*   so get the number of lines to slew */

            if (format_byte == FORMAT_SUPPRESS)         /* if the printer cannot overprint */
                slew_count = 1;                         /*   then the paper advances after printing */

            tprintf (lpt_dev, TRACE_CMD, "Printer commanded to slew %u line%s from line %u\n",
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

        lpt.status_word &= ~(ST_VFU_9 | ST_VFU_12);     /* assume no punches for channels 9 and 12 */

        if (print_props [model].vfu_channels > 8) {     /* if the printer VFU has more than 8 channels */
            if (VFU [current_line] & VFU_CHANNEL_9)     /*   then if channel 9 is punched for this line */
                lpt.status_word |= ST_VFU_9;            /*     then report it in the device status */

            if (VFU [current_line] & VFU_CHANNEL_12)    /* if channel 12 is punched for this line */
                lpt.status_word |= ST_VFU_12;           /*   then report it in the device status */
            }

        if (format_byte == FORMAT_VFU_CHAN_1)           /* if a TOF request was performed */
            fflush (uptr->fileref);                     /*   then flush the file buffer for inspection */

        uptr->wait = dlyptr->print                      /* schedule the print delay */
                       + slew_count * dlyptr->advance;  /*   plus the paper advance delay */

        uptr->pos = (t_addr) ftell (uptr->fileref);     /* update the file position */

        if (slew_count > 0)
            tprintf (lpt_dev, TRACE_CMD, "Printer advanced %u line%s to line %u\n",
                     slew_count, (slew_count == 1 ? "" : "s"), current_line);
        }

    if (ferror (uptr->fileref)) {                       /* if a host file system error occurred */
        report_error (uptr->fileref);                   /*   then report the error to the console */

        lp_set_alarm (uptr);                            /* set an alarm condition */
        return SCPE_IOERR;                              /*   and stop the simulator */
        }

    else                                                /* otherwise the write succeeded */
        activate_unit (lpt_unit);                       /*   so schedule the DEMAND reassertion */
    }

return SCPE_OK;                                         /* return event service success */
}


/* Device reset routine.

   This routine is called for a RESET or RESET LPT command.  It is the
   simulation equivalent of the POPIO signal, which is asserted by the front
   panel PRESET switch.
*/

static t_stat lp_reset (DEVICE *dptr)
{
UNIT * const uptr = dptr->units;                        /* a pointer to the printer unit */

io_assert (dptr, ioa_POPIO);                            /* PRESET the device (asserts MASTER CLEAR) */

if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    fast_times.buffer_load = LP_BUFFER_LOAD;            /*   then reset the per-character transfer time, */
    fast_times.print       = LP_PRINT;                  /*     the print and advance-one-line time, */
    fast_times.advance     = LP_ADVANCE;                /*       and the slew additional lines time */

    return lp_load_vfu (uptr, NULL);                    /* load the standard VFU tape */
    }

else                                                    /* otherwise this is a normal reset */
    return SCPE_OK;                                     /*   so just return */
}



/* Interface local utility routines */


/* Activate a unit.

   The specified unit is added to the event queue with the delay specified by
   the unit wait field.


   Implementation notes:

    1. This routine may be called with wait = 0, which will expire immediately
       and enter the service routine with the next sim_process_event call.
       Activation is required in this case to allow the service routine to
       return an error code to stop the simulation.  If the service routine was
       called directly, any returned error would be lost.
*/

static void activate_unit (UNIT *uptr)
{
tprintf (lpt_dev, TRACE_SERV, "Unit delay %u service scheduled\n",
         uptr->wait);

sim_activate (uptr, uptr->wait);                        /* activate the unit with the specified wait */

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



/* Printer local SCP support routines */



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

    2. The pointer to the appropriate event delay times is set in case we are
       being called during a RESTORE command (the assignment is redundant
       otherwise).
*/

static t_stat lp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = hp_attach (uptr, cptr);                        /* attach the specified printer image file for appending */

if (result == SCPE_OK                                   /* if the attach was successful */
  && (sim_switches & SIM_SW_REST) == 0) {               /*   and we are not being called during a RESTORE command */
    lpt.status_word &= ~ST_NOT_READY;                   /*     then clear not-ready status */
    current_line = 1;                                   /*       and reset the line counter to the top of the form */

    tprintf (lpt_dev, TRACE_CMD, "Printer paper loaded\n");

    lp_set_locality (uptr, Online);                     /* set the printer online */
    }

if (lpt_dev.flags & DEV_REALTIME)                       /* if the printer is in real-time mode */
    dlyptr = &real_times [GET_MODEL (uptr->flags)];     /*   then point at the times for the current model */
else                                                    /* otherwise */
    dlyptr = &fast_times;                               /*   point at the fast times */

return result;                                          /* return the result of the attach */
}


/* Detach the printer image file.

   The specified file is detached from the indicated unit.  This is the
   simulation equivalent of running out of paper or unloading the paper from the
   printer.  The out-of-paper condition cause a paper fault alarm, and the
   printer goes offline.

   When the printer runs out of paper, it will not go offline until characters
   present in the buffer are printed and paper motion stops.  In addition, the
   2607 printer waits until the paper reaches the top-of-form position before
   going offline.

   In simulation, entering a DETACH LPT command while the printer is busy will
   defer the file detach until print operations reach the top of the next form
   (2607) or until the current print operation completes (2613/17/18).  An
   immediate detach may be forced by adding the -F switch to the DETACH command.
   This simulates physically removing the paper from the printer and succeeds
   regardless of the current printer state.


   Implementation notes:

    1. During simulator shutdown, this routine is called for the printer unit.
       The printer must be detached, even if a detach has been deferred, to
       ensure that the file is closed properly.  We do this in response to a
       detach request with the SIM_SW_SHUT switch present.

    2. The DETACH ALL command will fail if any detach routine returns a status
       other than SCPE_OK.  Because a deferred detach is not fatal, we must
       return SCPE_OK, but we still want to print a warning to the user.

    3. Because the 2607 only paper faults at TOF, we must explicitly set the
       offline_pending flag, as lp_set_alarm may not have been called.
*/

static t_stat lp_detach (UNIT *uptr)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not currently attached */
    return SCPE_UNATT;                                  /*   then report it */

else {
    if (sim_switches & (SWMASK ('F') | SIM_SW_SHUT)) {  /* if this is a forced detach or shut down request */
        current_line = 1;                               /*   then reset the printer to TOF to enable the detach */
        sim_cancel (uptr);                              /*     and terminate */
        lpt.strobe = FALSE;                             /*       any print action in progress */
        }

    if ((print_props [model].fault_at_eol               /* otherwise if the printer faults at the end of any line */
      || current_line == 1)                             /*   or the printer is at the top of the form */
      && lp_set_alarm (uptr)) {                         /*   and a paper alarm is accepted */
        paper_fault = TRUE;                             /*     then set the out-of-paper condition */

        tprintf (lpt_dev, TRACE_CMD, "Printer is out of paper\n");

        return detach_unit (uptr);                      /* detach the unit */
        }

    else {                                              /* otherwise the alarm was rejected at this time */
        paper_fault = TRUE;                             /*   so set the out-of-paper condition now */
        offline_pending = TRUE;                         /*     but defer the detach */

        tprintf (lpt_dev, TRACE_CMD, "Paper out request deferred until print completes\n");

        cprintf ("%s\n", sim_error_text (SCPE_INCOMP)); /* report that the actual detach must be deferred */
        return SCPE_OK;                                 /*   until the buffer has been printed */
        }
    }
}


/* Set the device modes.

   This validation routine is entered with the "value" parameter set to one of
   the DEVICE_MODES values.  The device flag implied by the value is set or
   cleared.  The unit, character, and descriptor pointers are not used.
*/

static t_stat lp_set_mode (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
switch ((DEVICE_MODES) value) {                         /* dispatch the mode to set */

    case Fast_Time:                                     /* entering optimized timing mode */
        lpt_dev.flags &= ~DEV_REALTIME;                 /*   so clear the real-time flag */
        dlyptr = &fast_times;                           /*     and point at the fast times */
        break;


    case Real_Time:                                     /* entering realistic timing mode */
        lpt_dev.flags |= DEV_REALTIME;                  /*   so set the real-time flag */
        dlyptr = &real_times [GET_MODEL (uptr->flags)]; /*     and point at the times for the current model */
        break;


    case Printer:                                       /* entering printer mode */
        lpt_dev.flags &= ~DEV_DIAG;                     /*   so clear the diagnostic flag */

        lp_load_vfu (uptr, NULL);                       /* reload the standard VFU tape */
        break;


    case Diagnostic:                                    /* entering diagnostic mode */
        lpt_dev.flags |= DEV_DIAG;                      /*   so set the diagnostic flag */

        lp_load_vfu (uptr, NULL);                       /* load the diagnostic VFU tape */
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
if (lpt_dev.flags & DEV_REALTIME)                       /* if the printer is in real-time mode */
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
   complete" status to inform the user.  Otherwise, the unit is set offline and
   DEMAND is denied to indicate that the printer is not ready.

   If the printer is being put online and paper is present, the unit is set
   online, and any paper or tape fault present is cleared.  DEMAND is asserted
   to indicate that the printer is ready to accept characters.  If the flag
   flip-flop is clear, then DEMAND assertion sets the flag buffer flip-flop.

   As a special case, a detach (out-of-paper condition) or offline request that
   has been deferred until printing completes may be cancelled by setting the
   printer online.  No other action is taken, because the printer has never
   transitioned to the offline state.


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
    tprintf (lpt_dev, TRACE_CMD, "Offline request deferred until print completes\n");
    return SCPE_INCOMP;                                 /*   then let the user know */
    }

return SCPE_OK;                                         /* return operation success */
}


/* Set the VFU tape.

   This validation routine is entered to set up the VFU on the printer.  It is
   invoked by one of two commands:

     SET LPT VFU
     SET LPT VFU=<filename>

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
         (lpt_dev.flags & DEV_REALTIME ? "realistic" : "fast"),
         (lpt_dev.flags & DEV_DIAG ? "diagnostic" : "printer"));

return SCPE_OK;
}


/* Show the VFU tape.

   This display routine is called to show the content of the tape currently
   loaded in the printer's VFU.  The "value" parameter indicates how the routine
   was called.  It is 0 if a SHOW LPT command was given and 1 if a SHOW LPT VFU
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

    for (line = 1; line <= form_length; line++) {       /* loop through the VFU array */
        fprintf (st, "%3d ", line);                     /* display the current form line number */

        current_channel = VFU_CHANNEL_1;                /* start with channel 1 */

        for (chan = 1; chan <= channel_count; chan++) { /* loop through the defined channels */
            fputs ("    ", st);                         /* add some space */

            if (VFU [line] & current_channel)           /* if the current channel is punched for this line */
                fputc (punched_char, st);               /*   then display a punched location */
            else                                        /* otherwise */
                fputc (unpunched_char, st);             /*   display an unpunched location */

            current_channel = current_channel >> 1;     /* move to the next channel */
            }

        fputc ('\n', st);                               /* end the line */
        }
    }

return SCPE_OK;
}



/* Printer local utility routines */


/* Clear the printer.

   This routine simulates the assertion of the MASTER CLEAR signal to the
   printer.  In response, the printer clears the line buffer and aborts a paper
   advance in progress (except on the 2613).  It does not abort a print cycle in
   progress.


   Implementation notes:

    1. In simulation, printing and slewing occurs when the event service routine
       is entered on DEMAND assertion.  Service is then scheduled for a delay
       corresponding to the print action.  On service entry with DEMAND denied,
       the buffer is cleared and DEMAND is asserted.  Therefore, cancelling the
       event service does not abort a print cycle or paper advance, as they have
       already occurred when the event service is queued.
*/

static void lp_master_clear (UNIT *uptr)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */

sim_cancel (uptr);                                      /* deactivate the unit */

lpt.strobe = FALSE;                                     /* deny STROBE to the printer */
lpt.demand = FALSE;                                     /*   and assume that DEMAND is also denied */

buffer_index = 0;                                       /* clear the buffer without printing */

offline_pending = FALSE;                                /* cancel any pending offline request */

tape_fault  = FALSE;                                    /* clear any tape fault */
paper_fault = ! (uptr->flags & UNIT_ATT);               /* set a paper fault if the printer is out of paper */

if (sim_is_active (uptr)) {                             /* if the event service is scheduled */
    sim_cancel (uptr);                                  /*   then cancel it */
    lp_service (uptr);                                  /*     and call the service routine now to clean up */
    }

if (! (uptr->flags & UNIT_POWEROFF)) {                  /* if the printer power is on */
    lpt.status_word = 0;                                /*   then prepare to set the printer status */

    if (paper_fault && print_props [model].not_ready)   /* if paper is out and the printer reports it separately */
        lpt.status_word |= ST_NOT_READY;                /*   then add not-ready status */

    if (! (uptr->flags & UNIT_OFFLINE)) {               /* if the printer is online */
        lpt.demand = TRUE;                              /*   then DEMAND is asserted */

        lpt.status_word |= ST_ONLINE;                   /* set ONLINE status */
        }
    }

tprintf (lpt_dev, TRACE_CMD, "Master clear asserted to the printer\n");

return;
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
        lpt.status_word |= ST_NOT_READY;                /*     then add not-ready status */

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

   If the printer goes offline with an operation in progress, DEMAND will remain
   denied when the operation completes, so the interface flag will not set.
   DEMAND reasserts when the printer is set back online; if the flag flip-flop
   is clear, then the flag buffer flip-flop is set.
*/

static t_bool lp_set_locality (UNIT *uptr, LOCALITY printer_state)
{
if (printer_state == Offline) {                         /* if the printer is going offline */
    if (buffer_index == 0                               /*   then if the buffer is empty */
      && sim_is_active (uptr) == FALSE) {               /*     and the printer is idle */
        uptr->flags |= UNIT_OFFLINE;                    /*       then set the printer offline now */

        lpt.status_word &= ~ST_ONLINE;                  /* update the printer status */

        lpt.demand = FALSE;                             /* DEMAND denies while the printer is not ready */
        }

    else {                                              /*   otherwise the request must wait */
        offline_pending = TRUE;                         /*     until the line is printed */
        return FALSE;                                   /* report that the command is not complete */
        }
    }

else {                                                  /* otherwise the printer is going online */
    uptr->flags &= ~UNIT_OFFLINE;                       /*   so clear the unit flag */

    paper_fault = FALSE;                                /* clear any paper fault */
    tape_fault  = FALSE;                                /*   and any tape fault */

    lpt.status_word = lpt.status_word & ~ST_NOT_READY | ST_ONLINE;  /* update the printer status */

    lpt.demand = TRUE;                                  /* DEMAND asserts when the printer is online */

    if (lpt.flag == CLEAR) {                            /* if the flag flip-flop is clear */
        lpt.flag_buffer = SET;                          /* then DEMAND assertion sets the flag buffer */
        io_assert (&lpt_dev, ioa_ENF);                  /*   and flag flip-flops */
        }
    }

tprintf (lpt_dev, TRACE_CMD, "Printer set %s\n",
         (printer_state == Offline ? "offline" : "online"));

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

   If the DIAGNOSTIC mode is set, the standard tape is replaced with the
   diagnostic tape (02613-80002 for the 2613 and 2617 or 02618-80002 for the
   2618).  This tape extends the standard VFU tape above as follows:

     Chan  Description
     ----  ----------------------------------------
      10   Line before the bottom of form (line 59)
      11   Line before the top of form (line 66)
      12   Top of form (line 1)

   The 2607 diagnostic mode uses the standard 8-channel tape.

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

     title               an optional descriptive string printed by the SHOW LPT
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

    2. In DIAGNOSTIC mode, the 2613/17/18 diagnostic tape is generated
       unconditionally, as the 2607 will use only the eight standard channels.
*/

static t_stat lp_load_vfu (UNIT *uptr, FILE *vf)
{
const PRINTER_TYPE model = GET_MODEL (uptr->flags);     /* the printer model number */
uint32             line, channel;
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

    if (lpt_dev.flags & DEV_DIAG) {                     /* if the device is in diagnostic mode */
        strcpy (vfu_title, "Diagnostic VFU");           /*   then install the diagnostic VFU tape */

        tape [59] |= VFU_CHANNEL_10;                    /* punch channel 10 on line 59 */
        tape [66] |= VFU_CHANNEL_11;                    /* punch channel 11 on line 66 */
        tape [ 1] |= VFU_CHANNEL_12;                    /* punch channel 12 on line  1 */

        tape [0] |= VFU_CHANNEL_10                      /* add the additional channel punches */
                      | VFU_CHANNEL_11                  /*   to the accumulator */
                      | VFU_CHANNEL_12;
        }

    else                                                /* otherwise the device is in standard mode */
        strcpy (vfu_title, "Standard VFU");             /*   so install the standard VFU tape */

    form_length = 66;                                   /* set the form length */
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

lpt.status_word &= ~(ST_VFU_9 | ST_VFU_12);             /* assume no punches for channels 9 and 12 */

if (print_props [model].vfu_channels > 8) {             /* if the printer VFU has more than 8 channels */
    if (VFU [1] & VFU_CHANNEL_9)                        /*   then if channel 9 is punched for this line */
        lpt.status_word |= ST_VFU_9;                    /*     then report it in the device status */

    if (VFU [1] & VFU_CHANNEL_12)                       /* if channel 12 is punched for this line */
        lpt.status_word |= ST_VFU_12;                   /*   then report it in the device status */
    }

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
