/* hp3000_scmb.c: HP 3000 30033A Selector Channel Maintenance Board simulator

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

   SCMB1,SCMB2  HP 30033A Selector Channel Maintenance Board

   12-Sep-16    JDB     Changed DIB register macro usage from SRDATA to DIB_REG
   11-Jun-16    JDB     Bit mask constants are now unsigned
   13-May-16    JDB     Modified for revised SCP API function parameter types
   21-Mar-16    JDB     Changed uint16 types to HP_WORD
   21-Sep-15    JDB     First release version
   27-Jan-15    JDB     Passes the selector channel diagnostic (D429A)
   12-Jan-15    JDB     Passes the SCMB diagnostic (D429A)
   07-Jan-15    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - HP 3000 Series II Computer System System Service Manual
         (30000-90018, March 1977)
     - Stand-Alone HP 30030B/C Selector Channel Diagnostic
         (30030-90011, July 1978)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 30033A Selector Channel Maintenance Board provides the circuitry
   necessary to test the I/O bus signals driven and received by the selector and
   multiplexer channels.  Used with the Stand-Alone Selector Channel and
   Multiplexer Channel diagnostics, the SCMB is used to verify that the correct
   bus signals are driven in response to each of the programmed I/O orders, and
   that the channel responds correctly to the signals returned to it.  The SCMB
   functions as a programmable interface that can log incoming signals and drive
   the outgoing signals, as well as simulate a number of interface hardware
   faults.  Two SCMBs are required to test the multiplexer channel fully, so two
   SCMBs are provided; they are named "SCMB" (or "SCMB1") and "SCMB2".

   In hardware, the SCMB is connected either to the selector channel or
   multiplexer channel buses, and jumper W1 must be set to the SC or MX
   position, depending on the desired diagnostic test.  The device number and
   the service request number jumpers may be configured to use any unassigned
   numbers.

   In simulation, a SET SCMB SC configures the interface for the selector
   channel diagnostic, and a SET SCMB MX configures the interface for the
   multiplexer diagnostic.  If the selector channel diagnostic is run with SET
   SCMB MX, the SCMB itself is tested.  The multiplexer diagnostic requires two
   SCMB cards, so SET SCMB1 MX and SET SCMB2 MX are required.


   The SCMB responds to direct and programmed I/O instructions, as follows:

   Control Word Format (CIO):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | J | V | A | S | load  | H | N | T | C | L |  counter  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     M = master reset
     R = reset interrupt
     J = set jump met condition
     V = set device end condition
     A = inhibit channel acknowledge
     S = inhibit service request
     H = enable high speed service request
     N = enable special device number
     T = terminate on terminal count
     C = terminate on compare failure
     L = enable device end/clear interface (0/1) on terminate

   Load:
     00 = load the IOAW into the control word
     01 = load the IOCW into the buffer
     10 = load the IOAW into the buffer
     11 = load the IOCW and then the IOAW into the buffer

   Counter:
     000 = counter is disabled
     001 = count READNEXTWD signals
     010 = count PREADSTB signals
     011 = count TOGGLEINXFER signals
     100 = count PWRITESTB signals
     101 = count TOGGLEOUTXFER signals
     110 = count EOT signals
     111 = count CHANSO signals

   The Load field defines how programmed I/O orders will affect the control word
   and buffer.  The Counter field defines which signal occurrences, if any, are
   counted.  If value 000 is selected, the counter does not operate, and the
   buffer value does not change.


   Control Word Format (SIO Control):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | - | 1   0   0 |                 buffer value                  |  word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | M | R | J | V | A | S | load  | H | N | T | C | L |  counter  |  word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   If the current control word specifies a Load field value of 01, word 1 is
   loaded into the counter/buffer register.  Otherwise, word 2 is loaded into
   the control word register or counter/buffer register, depending on the Load
   field value.

   If the A bit (inhibit channel acknowledge) is set, CHANACK will be issued for
   this Control order, but all future orders will not be acknowledged.
   Similarly, if the S bit (inhibit service request) is set, a CHANSR will be
   issued for this order but not for future orders.


   Status Word Format (TIO and SIO Status):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | D | R | A | X | N | V | E | C | T | I | O | L | 0   0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = SIO OK
     D = direct I/O OK (always 1)
     R = interrupt requested
     A = interrupt active
     X = transfer error was asserted
     N = SIO enabled is asserted
     V = device end was asserted
     E = end of transfer was asserted
     C = an end-on-miscompare occurred
     T = an end-on-terminal-count occurred
     I = an input transfer is in progress
     O = an output transfer is in progress
     L = a clear interface has asserted to abort the I/O program

   Note that the Series II Service Manual and the Series III CE Handbook list
   the wrong assignments for status bits 8-11.  The Selector Channel Diagnostic
   manual has the correct assignments.


   Output Data Word Format (WIO and SIO Write):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |               new counter/buffer register value               |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   If the control word C bit (terminate on compare failure) is set, the current
   counter/buffer value is compared to the new value.  If they are not equal,
   the C bit (an end-on-miscompare occurred) is set in the status word.

   The new value is stored in the counter/buffer register only if the control
   word Counter field value is less than 100 (i.e., it is not set to count
   writes).  Otherwise, the value is ignored, but the write is counted.

   If DEVEND is asserted for a selector channel SIO Write, the write is ignored,
   and the PWRITESTB signal is not counted.


   Input Data Word Format (RIO and SIO Read):

       0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |             current counter/buffer register value             |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   If DEVEND is asserted for a selector channel SIO Read, the read is ignored,
   and the PREADSTB signal is not counted.
*/



#include "hp3000_defs.h"
#include "hp3000_io.h"



/* Program constants */

#define SERVICE_DELAY       uS (5)              /* 5 microsecond delay for non-high-speed service request */


/* Unit flags */

#define UNIT_W1_SHIFT       (UNIT_V_UF + 0)     /* jumper W1 */

#define UNIT_W1_SEL         (1u << UNIT_W1_SHIFT)

#define MPX_BUS(card)       ((scmb_unit [card].flags & UNIT_W1_SEL) == 0)
#define SEL_BUS(card)       ((scmb_unit [card].flags & UNIT_W1_SEL) != 0)


/* Debug flags */

#define DEB_CSRW            (1u << 0)           /* trace commands received and status returned */
#define DEB_XFER            (1u << 1)           /* trace channel data reads and writes */
#define DEB_SERV            (1u << 2)           /* trace unit service scheduling calls */
#define DEB_IOB             (1u << 3)           /* trace I/O bus signals and data words */


/* Control word.

     0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | M | R | J | V | A | S | load  | H | N | T | C | L |  counter  |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_MR               0100000u            /* M = master reset */
#define CN_IRQ_RESET        0040000u            /* R = interrupt reset */
#define CN_JMPMET           0020000u            /* J = set jump met */
#define CN_DEVEND           0010000u            /* V = set device end */
#define CN_NOACK            0004000u            /* A = inhibit channel acknowledge */
#define CN_NOSR             0002000u            /* S = inhibit service request */
#define CN_LOAD_MASK        0001400u            /* load operation mask */
#define CN_HSREQ            0000200u            /* H = high speed service request */
#define CN_DEVNO            0000100u            /* N = special device number */
#define CN_TERM_COUNT       0000040u            /* T = terminate on count */
#define CN_TERM_COMP        0000020u            /* C = terminate on miscompare */
#define CN_CLEAR_IF         0000010u            /* L = clear interface */
#define CN_CNTR_MASK        0000007u            /* counter operation mask */

#define CN_LOAD_SHIFT       8                   /* load operation alignment shift */
#define CN_CNTR_SHIFT       0                   /* counter operation alignment shift */

#define CN_LOAD(c)          ((LOAD_OP) (((c) & CN_LOAD_MASK) >> CN_LOAD_SHIFT))
#define CN_CNTR(c)          ((CNTR_OP) (((c) & CN_CNTR_MASK) >> CN_CNTR_SHIFT))

typedef enum {                                  /* load operations */
    load_cntl_IOAW = 0,
    load_bufr_IOCW = 1,
    load_bufr_IOAW = 2,
    load_bufr_both = 3
    } LOAD_OP;

static const char *const load_names [4] = {     /* indexed by LOAD_OP */
    "load control IOAW",                        /*   00 = load IOAW into control word */
    "load buffer IOCW",                         /*   01 = load IOCW into buffer */
    "load buffer IOAW",                         /*   10 = load IOAW into buffer */
    "load buffer IOCW/AW"                       /*   11 = load IOCW and IOAW into buffer */
    };

typedef enum {                                  /* counter operations */
    count_nothing       = 0,
    count_READNEXTWD    = 1,
    count_PREADSTB      = 2,
    count_TOGGLEINXFER  = 3,
    count_PWRITESTB     = 4,
    count_TOGGLEOUTXFER = 5,
    count_EOT           = 6,
    count_CHANSO        = 7
    } CNTR_OP;

static const char *const count_names [8] = {    /* indexed by CNTR_OP */
    "count nothing",                            /*   000 = counter is disabled */
    "count READNEXTWD",                         /*   001 = count READNEXTWD */
    "count PREADSTB",                           /*   010 = count PREADSTB */
    "count TOGGLEINXFER",                       /*   011 = count TOGGLEINXFER */
    "count PWRITESTB",                          /*   100 = count PWRITESTB */
    "count TOGGLEOUTXFER",                      /*   101 = count TOGGLEOUTXFER */
    "count EOT",                                /*   110 = count EOT */
    "count CHANSO"                              /*   111 = count CHANSO */
    };

static const BITSET_NAME control_names [] = {           /* Control word names */
    "master reset",                                     /*   bit  0 */
    "reset interrupt",                                  /*   bit  1 */
    "set JMPMET",                                       /*   bit  2 */
    "set DEVEND",                                       /*   bit  3 */
    "inhibit CHANACK",                                  /*   bit  4 */
    "inhibit SR",                                       /*   bit  5 */
    NULL,                                               /*   bit  6 */
    NULL,                                               /*   bit  7 */
    "high speed",                                       /*   bit  8 */
    "send DEVNO",                                       /*   bit  9 */
    "end on count",                                     /*   bit 10 */
    "end on miscompare",                                /*   bit 11 */
    "\1end with clear interface\0end with device end"   /*   bit 12 */
    };

static const BITSET_FORMAT control_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (control_names, 3, msb_first, has_alt, append_bar) };


/* Status word.

     0 | 1   2   3 | 4   5   6 | 7   8   9 |10  11  12 |13  14  15
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | S | D | R | A | X | N | V | E | C | T | I | O | L | 0   0   0 |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   NOTE: The Series II Service Manual and the Series III CE Handbook list the
   wrong assignments for status bits 8-11.  The Selector Channel Diagnostic
   manual has the correct assignments.
*/

#define ST_SIO_OK           0100000u            /* S = SIO OK to use */
#define ST_DIO_OK           0040000u            /* D = direct I/O OK to use (always 1) */
#define ST_INTREQ           0020000u            /* R = interrupt requested */
#define ST_INTACT           0010000u            /* A = interrupt active */
#define ST_XFERERR          0004000u            /* X = transfer error is asserted */
#define ST_SIOENABLED       0002000u            /* N = SIO enabled is asserted */
#define ST_DEVEND           0001000u            /* V = device end is asserted */
#define ST_EOT              0000400u            /* E = end of transfer is asserted */
#define ST_END_MISCMP       0000200u            /* C = end on miscompare occurred */
#define ST_END_COUNT        0000100u            /* T = end on terminal count occurred */
#define ST_INXFER           0000040u            /* I = input transfer is asserted */
#define ST_OUTXFER          0000020u            /* O = output transfer is asserted  */
#define ST_CLEAR_IF         0000010u            /* L = clear interface is asserted */

#define END_CONDITION       (ST_END_MISCMP | ST_END_COUNT)

static const BITSET_NAME status_names [] = {    /* Status word names */
    "SIO OK",                                   /*   bit  0 */
    "DIO OK",                                   /*   bit  1 */
    "int request",                              /*   bit  2 */
    "int active",                               /*   bit  3 */
    "transfer error",                           /*   bit  4 */
    "SIO enabled",                              /*   bit  5 */
    "device end",                               /*   bit  6 */
    "end of transfer",                          /*   bit  7 */
    "miscompare",                               /*   bit  8 */
    "terminal count",                           /*   bit  9 */
    "input transfer",                           /*   bit 10 */
    "output transfer",                          /*   bit 11 */
    "clear interface"                           /*   bit 12 */
    };

static const BITSET_FORMAT status_format =      /* names, offset, direction, alternates, bar */
    { FMT_INIT (status_names, 3, msb_first, no_alt, no_bar) };


/* SCMB state */

typedef enum {
    card1,                                      /* first card ID */
    card2                                       /* second card ID */
    } CARD_ID;

typedef struct {
    HP_WORD control_word;                       /* control word register */
    HP_WORD status_word;                        /* status word register */
    HP_WORD counter;                            /* counter/buffer register */
    HP_WORD flags;                              /* status flags */
    uint32  saved_srn;                          /* saved SR number */

    FLIP_FLOP sio_busy;                         /* SIO busy flip-flop */
    FLIP_FLOP channel_sr;                       /* channel service request flip-flop */
    FLIP_FLOP device_sr;                        /* device service request flip-flop */
    FLIP_FLOP input_xfer;                       /* input transfer flip-flop */
    FLIP_FLOP output_xfer;                      /* output transfer flip-flop */

    FLIP_FLOP jump_met;                         /* jump met flip-flop */
    FLIP_FLOP device_end;                       /* device end flip-flop */
    FLIP_FLOP stop_transfer;                    /* stop transfer flip-flop */
    } SCMB_STATE;

static SCMB_STATE scmb [2];                     /* per-card state variables */


/* SCMB local SCP support routines */

static CNTLR_INTRF scmb_interface;
static t_stat      scmb_service   (UNIT   *uptr);
static t_stat      scmb_reset     (DEVICE *dptr);
static t_stat      scmb_set_bus   (UNIT   *uptr, int32 value, CONST char *cptr, void *desc);


/* SCMB local utility routines */

static void sio_reset         (CARD_ID card);
static void clear_logic       (CARD_ID card);
static void increment_counter (CARD_ID card);


/* SCMB SCP interface data structures.


   Implementation notes:

    1. The DIB, UNIT, and DEVICE structures for the two cards must be arrayed so
       that access via card number is possible.

    2. The SCMB interfaces are disabled by default, as they are only used during
       diagnostic testing.
*/


/* Device information blocks */

static DIB scmb_dib [] = {
    { &scmb_interface,                          /* device interface */
      65,                                       /* device number */
      0,                                        /* service request number */
      10,                                       /* interrupt priority */
      INTMASK_UNUSED,                           /* interrupt mask */
      card1                                     /* card index for card 1 */
      },

    { &scmb_interface,                          /* device interface */
      66,                                       /* device number */
      1,                                        /* service request number */
      11,                                       /* interrupt priority */
      INTMASK_UNUSED,                           /* interrupt mask */
      card2                                     /* card index for card 2 */
      }
    };


/* Unit list */

static UNIT scmb_unit [] = {
    { UDATA (&scmb_service, 0, 0), SERVICE_DELAY },     /* unit for card 1 */
    { UDATA (&scmb_service, 0, 0), SERVICE_DELAY }      /* unit for card 2 */
    };


/* Register lists */

static REG scmb1_reg [] = {
/*    Macro   Name    Location                    Width  Offset        Flags        */
/*    ------  ------  --------------------------  -----  ------  -----------------  */
    { ORDATA (CNTL,   scmb [card1].control_word,   16),          REG_FIT            },
    { ORDATA (STAT,   scmb [card1].status_word,    16),          REG_FIT            },
    { ORDATA (CNTR,   scmb [card1].counter,        16),          REG_FIT            },
    { ORDATA (SRSAVE, scmb [card1].saved_srn,       8),                    REG_HRO  },

    { FLDATA (SIOBSY, scmb [card1].sio_busy,               0)                       },
    { FLDATA (CHANSR, scmb [card1].channel_sr,             0)                       },
    { FLDATA (DEVSR,  scmb [card1].device_sr,              0)                       },
    { FLDATA (INXFR,  scmb [card1].input_xfer,             0)                       },
    { FLDATA (OUTXFR, scmb [card1].output_xfer,            0)                       },

    { FLDATA (JMPMET, scmb [card1].jump_met,               0)                       },
    { FLDATA (XFRERR, scmb [card1].flags,                 11)                       },
    { FLDATA (EOT,    scmb [card1].flags,                  8)                       },
    { FLDATA (TRMCNT, scmb [card1].flags,                  6)                       },
    { FLDATA (MISCMP, scmb [card1].flags,                  7)                       },
    { FLDATA (DEVEND, scmb [card1].device_end,             0)                       },
    { FLDATA (STOP,   scmb [card1].stop_transfer,          0)                       },

      DIB_REGS (scmb_dib [card1]),

    { NULL }
    };

static REG scmb2_reg [] = {
/*    Macro   Name    Location                    Width  Offset        Flags        */
/*    ------  ------  --------------------------  -----  ------  -----------------  */
    { ORDATA (CNTL,   scmb [card2].control_word,   16),          REG_FIT            },
    { ORDATA (STAT,   scmb [card2].status_word,    16),          REG_FIT            },
    { ORDATA (CNTR,   scmb [card2].counter,        16),          REG_FIT            },
    { ORDATA (SRSAVE, scmb [card2].saved_srn,       8),                    REG_HRO  },

    { FLDATA (SIOBSY, scmb [card2].sio_busy,               0)                       },
    { FLDATA (CHANSR, scmb [card2].channel_sr,             0)                       },
    { FLDATA (DEVSR,  scmb [card2].device_sr,              0)                       },
    { FLDATA (INXFR,  scmb [card2].input_xfer,             0)                       },
    { FLDATA (OUTXFR, scmb [card2].output_xfer,            0)                       },

    { FLDATA (JMPMET, scmb [card2].jump_met,               0)                       },
    { FLDATA (XFRERR, scmb [card2].flags,                 11)                       },
    { FLDATA (EOT,    scmb [card2].flags,                  8)                       },
    { FLDATA (TRMCNT, scmb [card2].flags,                  6)                       },
    { FLDATA (MISCMP, scmb [card2].flags,                  7)                       },
    { FLDATA (DEVEND, scmb [card2].device_end,             0)                       },
    { FLDATA (STOP,   scmb [card2].stop_transfer,          0)                       },

      DIB_REGS (scmb_dib [card2]),

    { NULL }
    };


/* Modifier lists */

static MTAB scmb1_mod [] = {
/*    Mask Value   Match Value  Print String  Match String  Validation     Display  Descriptor */
/*    -----------  -----------  ------------  ------------  -------------  -------  ---------- */
    { UNIT_W1_SEL, UNIT_W1_SEL, "W1=SC",      "SC",         &scmb_set_bus, NULL,    NULL       },
    { UNIT_W1_SEL, 0,           "W1=MX",      "MX",         &scmb_set_bus, NULL,    NULL       },

/*    Entry Flags  Value       Print String  Match String  Validation   Display       Descriptor                */
/*    -----------  ----------  ------------  ------------  -----------  ------------  ------------------------- */
    { MTAB_XDV,    VAL_DEVNO,  "DEVNO",      "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card1] },
    { MTAB_XDV,    VAL_INTPRI, "INTPRI",     "INTPRI",     &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card1] },
    { MTAB_XDV,    VAL_SRNO,   "SRNO",       "SRNO",       &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card1] },
    { 0 }
    };

static MTAB scmb2_mod [] = {
/*    Mask Value   Match Value  Print String  Match String  Validation     Display  Descriptor */
/*    -----------  -----------  ------------  ------------  -------------  -------  ---------- */
    { UNIT_W1_SEL, UNIT_W1_SEL, "W1=SC",      "SC",         &scmb_set_bus, NULL,    NULL       },
    { UNIT_W1_SEL, 0,           "W1=MX",      "MX",         &scmb_set_bus, NULL,    NULL       },

/*    Entry Flags  Value       Print String  Match String  Validation   Display       Descriptor                */
/*    -----------  ----------  ------------  ------------  -----------  ------------  ------------------------- */
    { MTAB_XDV,    VAL_DEVNO,  "DEVNO",      "DEVNO",      &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card2] },
    { MTAB_XDV,    VAL_INTPRI, "INTPRI",     "INTPRI",     &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card2] },
    { MTAB_XDV,    VAL_SRNO,   "SRNO",       "SRNO",       &hp_set_dib, &hp_show_dib, (void *) &scmb_dib [card2] },
    { 0 }
    };


/* Debugging trace list */

static DEBTAB scmb_deb [] = {
    { "CSRW",  DEB_CSRW },                      /* Interface control, status, read, and write actions */
    { "XFER",  DEB_XFER },                      /* Channel data reads and writes */
    { "SERV",  DEB_SERV },                      /* Unit service scheduling calls */
    { "IOBUS", DEB_IOB  },                      /* Interface I/O bus signals and data words */
    { NULL,    0        }
    };


/* Device descriptors */

DEVICE scmb_dev [] = {
    { "SCMB",                                   /* device name */
      &scmb_unit [card1],                       /* unit array */
      scmb1_reg,                                /* register array */
      scmb1_mod,                                /* modifier array */
      1,                                        /* number of units */
      8,                                        /* address radix */
      PA_WIDTH,                                 /* address width */
      1,                                        /* address increment */
      8,                                        /* data radix */
      DV_WIDTH,                                 /* data width */
      NULL,                                     /* examine routine */
      NULL,                                     /* deposit routine */
      &scmb_reset,                              /* reset routine */
      NULL,                                     /* boot routine */
      NULL,                                     /* attach routine */
      NULL,                                     /* detach routine */
      &scmb_dib [card1],                        /* device information block pointer */
      DEV_DEBUG | DEV_DISABLE | DEV_DIS,        /* device flags */
      0,                                        /* debug control flags */
      scmb_deb,                                 /* debug flag name array */
      NULL,                                     /* memory size change routine */
      NULL                                      /* logical device name */
      },

    { "SCMB2",                                  /* device name */
      &scmb_unit [card2],                       /* unit array */
      scmb2_reg,                                /* register array */
      scmb2_mod,                                /* modifier array */
      1,                                        /* number of units */
      8,                                        /* address radix */
      PA_WIDTH,                                 /* address width */
      1,                                        /* address increment */
      8,                                        /* data radix */
      DV_WIDTH,                                 /* data width */
      NULL,                                     /* examine routine */
      NULL,                                     /* deposit routine */
      &scmb_reset,                              /* reset routine */
      NULL,                                     /* boot routine */
      NULL,                                     /* attach routine */
      NULL,                                     /* detach routine */
      &scmb_dib [card2],                        /* device information block pointer */
      DEV_DEBUG | DEV_DISABLE | DEV_DIS,        /* device flags */
      0,                                        /* debug control flags */
      scmb_deb,                                 /* debug flag name array */
      NULL,                                     /* memory size change routine */
      NULL                                      /* logical device name */
      }
    };



/* SCMB local SCP support routines */



/* Selector Channel Maintenance Board interface.

   The interface is installed on the IOP bus and either the Multiplexer or
   Selector Channel bus and receives direct and programmed I/O commands from the
   IOP and channel, respectively.  In simulation, the asserted signals on the
   buses are represented as bits in the inbound_signals set.  Each signal is
   processed sequentially in numerical order, and a set of similar
   outbound_signals is assembled and returned to the caller, simulating
   assertion of the corresponding backplane signals.

   Jumper W1 on the interface PCA must be set to match the bus (multiplexer or
   selector) to which the SCMB is connected.  The multiplexer and selector
   channels have slightly different signal requirements, and this jumper
   configures the logic to account for the difference.

   The diagnostics use direct I/O to configure the SCMB and then use programmed
   I/O to test the channel's interaction with the interface.


   Implementation notes:

    1. In hardware, asserting DSTARTIO sets the Channel SR flip-flop, but the
       output is masked off unless the SCMB is connected to the multiplexer
       channel (the selector channel does not use the Channel SR flip-flop).
       In simulation, setting the flip-flop is inhibited.

    2. In hardware, asserting DEVEND to the selector channel inhibits
       generation of the PREADSTB and PWRITESTB signals.  In simulation, DEVEND
       is returned in response to a PREADSTB or PWRITESTB if the Device End
       flip-flop is set.  As the strobes may cause the counter to increment,
       counting is inhibited in simulation if the Device End flip-flop is set
       and the SCMB is on the selector channel bus.

    3. In hardware, the SCMB does not use ACKSR to reset the Device SR
       flip-flop.  Instead, the flip-flop is preset by PCMD1 or PCONTSTB or if
       the Input Transfer or Output Transfer flip-flop is set; it is clocked to
       zero by the leading edge of CHANSO.  In simulation, the Device SR
       flip-flop is cleared on entry by CHANSO if both the Input and Output
       Transfer flip-flops are clear.  This provides the same action as the
       asynchronous set overriding the synchronous clear in hardware.

    4. If channel acknowledgement is inhibited, the CHANACK signal is not
       returned to the selector channel.  This causes a CHANSO timeout in the
       channel.  Similarly, if channel service requests are inhibited, CHANSR
       will not be returned to the selector channel, which will cause a timeout
       and channel abort.

    5. In hardware, clearing the "enable high speed service request" bit in the
       control word delays SR assertion for five microseconds after the device
       SR flip-flop sets.  In software, the SCMB unit service routine is
       scheduled and the request signal is not returned from the interface; when
       the delay expires, the service routine calls either "mpx_assert_SRn" or
       "sel_assert_CHANSR" to request channel service.

       However, if the "inhibit channel acknowledge" or "inhibit service
       request" bit in the control word is also set, then scheduling of the
       service routine is inhibited.  Otherwise, SR would be asserted after the
       channel had been aborted by the timeout.

    6. In hardware, setting the "enable special device number" bit in the
       control word causes the SCMB to gate bits 8-15 of the counter/buffer
       register onto SR6-13 for the DSTARTIO signal only.  This supplies the
       selector channel with a configurable device number instead of that of the
       SCMB.  For all other operations, e.g., interrupts, the regular SCMB
       device number is used.

       In simulation, the device number is obtained from the DIB passed to the
       "sel_assert_REQ" routine.  If the special bit is set, the device number
       is changed temporarily before calling "sel_assert_REQ" and then restored
       afterward.  This ensures that interrupts in particular are handled
       correctly.  (An alternate method of passing a secondary DIB containing
       the special device number won't work, as the selector channel will use
       the secondary DIB to request an interrupt, but the IOP will use the
       standard DIB to respond to the interrupt.)

    7. Receipt of a DRESETINT signal clears the interrupt request and active
       flip-flops but does not cancel a request pending but not yet serviced by
       the IOP.  However, when the IOP does service the request by asserting
       INTPOLLIN, the interface routine returns INTPOLLOUT, which will cancel
       the request.

    8. Although support for the "count_CHANSO" option is provided, none of the
       diagnostics (SCMB, MPX, and SEL) test this option.

    9. In simulation, we allow the device number to be changed during a
       simulation stop.  However, the SCMB may be spoofing the device number,
       and it is this spoofed number that must be restored during the channel
       initialization that follows resumption.  This presents no problem to the
       multiplexer channel, which asserts DEVNODB to the interface as part of
       each I/O order execution.  However, the selector channel requests the
       device number once during the REQ assertion that starts the I/O program
       and saves it internally for later use.

       To accommodate changing device numbers while spoofing is enabled, the
       selector channel simulator asserts DEVNODB to the interface during
       initialization.  The SCMB responds to the DEVNODB signal, as it supports
       connection to the multiplexer channel.  Devices that connect only to the
       selector channel will not respond to DEVNODB, causing the initializer to
       use the DIB field to obtain the device number.
*/

static SIGNALS_DATA scmb_interface (DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const CARD_ID card = (CARD_ID) (dibptr->card_index);    /* the ID number of the card */
LOAD_OP        load_operation;
FLIP_FLOP      assert_sr;
HP_WORD        saved_devno;
INBOUND_SIGNAL signal;
INBOUND_SET    working_set      = inbound_signals;
HP_WORD        outbound_value   = 0;
OUTBOUND_SET   outbound_signals = NO_SIGNALS;

dprintf (scmb_dev [card], DEB_IOB, "Received data %06o with signals %s\n",
         inbound_value, fmt_bitset (inbound_signals, inbound_format));

if (inbound_signals & CHANSO                            /* the leading edge of CHANSO */
  && scmb [card].input_xfer == CLEAR                    /*   clears the Device SR flip-flop */
  && scmb [card].output_xfer == CLEAR)                  /*     if not overridden by the Q outputs */
    scmb [card].device_sr = CLEAR;                      /*       of the Input and Output Transfer flip-flops */

while (working_set) {                                   /* while there are signals to process */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch an I/O signal */

        case DWRITESTB:
            dprintf (scmb_dev [card], DEB_CSRW, "Counter/buffer value %06o set\n",
                     inbound_value);

            scmb [card].counter = inbound_value;        /* set the counter/buffer */
            break;


        case DREADSTB:
            outbound_value = scmb [card].counter;       /* return the counter/buffer value */

            dprintf (scmb_dev [card], DEB_CSRW, "Counter/buffer value %06o returned\n",
                     outbound_value);
            break;


        case DCONTSTB:
            dprintf (scmb_dev [card], DEB_CSRW, "Control is %s%s | %s\n",
                     fmt_bitset (inbound_value, control_format),
                     load_names  [CN_LOAD  (inbound_value)],
                     count_names [CN_CNTR (inbound_value)]);

            scmb [card].control_word = inbound_value;       /* save the new control word value */

            if (scmb [card].control_word & CN_MR)           /* if master reset is requested */
                scmb_reset (&scmb_dev [card]);              /*   then perform an I/O reset */

            if (scmb [card].control_word & CN_IRQ_RESET)    /* if reset interrupt is requested */
                dibptr->interrupt_request = CLEAR;          /*   then clear the interrupt request */

            scmb [card].device_end = CLEAR;                 /* clear DEVEND and EOT status */
            scmb [card].flags     &= ~ST_EOT;               /*   in preparation for a new transfer */
            break;


        case DSTATSTB:
        case PSTATSTB:
            scmb [card].status_word = ST_DIO_OK | scmb [card].flags;    /* copy the flags to the status word */

            if (MPX_BUS (card) || sel_is_idle) {            /* if we're on the MPX bus or the SEL is not busy */
                scmb [card].status_word |= ST_SIOENABLED;   /*   then SIO is enabled */

                if (scmb [card].sio_busy == CLEAR)          /* if we're not running an SIO program */
                    scmb [card].status_word |= ST_SIO_OK;   /*   then report that the SCMB is available */
                }

            if (dibptr->interrupt_request == SET)           /* reflect the interrupt request state */
                scmb [card].status_word |= ST_INTREQ;       /*   in the status word */

            if (dibptr->interrupt_active == SET)            /* reflect the interrupt active state */
                scmb [card].status_word |= ST_INTACT;       /*   in the status word */

            if (scmb [card].device_end == SET)              /* reflect the device end flip-flop state */
                scmb [card].status_word |= ST_DEVEND;       /*   in the status word */

            if (scmb [card].input_xfer == SET)              /* reflect the input transfer flip-flop state */
                scmb [card].status_word |= ST_INXFER;       /*   in the status word */

            if (scmb [card].output_xfer == SET)             /* reflect the output transfer flip-flop state */
                scmb [card].status_word |= ST_OUTXFER;      /*   in the status word */

            outbound_value = scmb [card].status_word;       /* return the status word */

            dprintf (scmb_dev [card], DEB_CSRW, "Status is %s\n",
                     fmt_bitset (outbound_value, status_format));
            break;


        case DSETINT:
        case SETINT:
            dibptr->interrupt_request = SET;            /* set the interrupt request flip-flop */
            outbound_signals |= INTREQ;                 /*   and request the interrupt */
            break;


        case DRESETINT:
            dibptr->interrupt_active  = CLEAR;          /* reset the interrupt active flip-flop */
            break;


        case INTPOLLIN:
            if (dibptr->interrupt_request) {            /* if a request is pending */
                dibptr->interrupt_request = CLEAR;      /*   then clear it */
                dibptr->interrupt_active  = SET;        /*     and mark it now active */

                outbound_signals = INTACK;              /* acknowledge the interrupt */
                outbound_value = dibptr->device_number; /*   and return our device number */
                }

            else                                        /* otherwise the request has been reset */
                outbound_signals = INTPOLLOUT;          /*   so let the IOP know to cancel it */
            break;


        case DSTARTIO:
            dprintf (scmb_dev [card], DEB_CSRW, "Channel program started\n");

            scmb [card].sio_busy      = SET;            /* set the SIO busy flip-flop */
            scmb [card].stop_transfer = CLEAR;          /*   and clear the stop transfer flip-flop */

            sio_reset (card);                           /* clear in preparation for the new program */

            if (MPX_BUS (card)) {                       /* if the card is configured for the multiplexer channel */
                scmb [card].channel_sr = SET;           /*   then set the channel service request flip-flop */
                mpx_assert_REQ (dibptr);                /*     and request the channel */
                outbound_signals |= SRn;
                }

            else                                            /* otherwise request the selector channel */
                if (scmb [card].control_word & CN_DEVNO) {  /* if the special device number flag is set */
                    saved_devno = dibptr->device_number;    /*   then save the real device number */

                    dibptr->device_number =                 /* use the counter as the device number */
                      LOWER_BYTE (scmb [card].counter);

                    sel_assert_REQ (dibptr);                /* request the channel */

                    dibptr->device_number = saved_devno;    /* restore the real device number */
                    }

                else                                        /* otherwise request the channel */
                    sel_assert_REQ (dibptr);                /*   with the standard device number */
            break;


        case CHANSO:
            if (CN_CNTR (scmb [card].control_word) == count_CHANSO) /* if counting is enabled for this signal */
                increment_counter (card);                           /*   then increment the counter */

            if ((scmb [card].control_word & CN_NOACK) == 0)         /* if CHANACK is not inhibited */
                outbound_signals |= CHANACK;                        /*   then acknowledge the CHANSO signal */
            break;


        case TOGGLESR:
            TOGGLE (scmb [card].channel_sr);            /* set or clear the service request flip-flop */
            break;


        case TOGGLESIOOK:
            TOGGLE (scmb [card].sio_busy);              /* set or clear the SIO busy flip-flop */

            if (scmb [card].sio_busy == CLEAR)          /* if the channel is now idle */
                dprintf (scmb_dev [card], DEB_CSRW, "Channel program ended\n");
            break;


        case TOGGLEINXFER:
            if (CN_CNTR (scmb [card].control_word) == count_TOGGLEINXFER)   /* if counting is enabled for this signal */
                increment_counter (card);                                   /*   then increment the counter */

            TOGGLE (scmb [card].input_xfer);                    /* set or clear the input transfer flip-flop */

            if (scmb [card].input_xfer == SET) {                /* if we're starting a new transfer */
                scmb [card].flags &= ~ST_EOT;                   /*   then clear the EOT flag */

                scmb [card].device_end =                        /* set or clear device end status depending on */
                  D_FF (scmb [card].control_word & CN_DEVEND);  /*   whether an immediate device end is enabled */
                }

            scmb [card].device_sr = SET;                        /* preset the device SR flip-flop */
            break;


        case TOGGLEOUTXFER:
            if (CN_CNTR (scmb [card].control_word) == count_TOGGLEOUTXFER)  /* if counting is enabled for this signal */
                increment_counter (card);                                   /*   then increment the counter */

            TOGGLE (scmb [card].output_xfer);                   /* set or clear the output transfer flip-flop */

            if (scmb [card].output_xfer == SET) {               /* if we're starting a new transfer */
                scmb [card].flags &= ~ST_EOT;                   /*   then clear the EOT flag */

                scmb [card].device_end =                        /* set or clear device end status depending on */
                  D_FF (scmb [card].control_word & CN_DEVEND);  /*   whether an immediate device end is enabled */
                }

            scmb [card].device_sr = SET;                        /* preset the device SR flip-flop */
            break;


        case DEVNODB:
            if (scmb [card].control_word & CN_DEVNO)                    /* if the special device number flag is set */
                outbound_value = LOWER_BYTE (scmb [card].counter) * 4;  /*   then use the counter as the device number */
            else                                                        /* otherwise */
                outbound_value = dibptr->device_number * 4;             /*   use the preset device number */

            outbound_signals = NO_SIGNALS;                              /* clear CHANACK in case SEL issued the signal */
            break;


        case PCMD1:
            if (CN_LOAD (scmb [card].control_word) == load_bufr_IOCW)   /* if buffer load is enabled */
                working_set |= DWRITESTB;                               /*   then set the counter to the inbound value */

            scmb [card].device_sr = SET;                                /* request channel service */
            break;


        case PCONTSTB:
            load_operation = CN_LOAD (scmb [card].control_word);    /* isolate the load operation from the control word */

            if (load_operation == load_cntl_IOAW) {     /* if loading IOAW into the control word is requested */
                working_set |= DCONTSTB;                /*   then set the control word to the inbound value */

                if (inbound_value & CN_NOACK)           /* if the CHANACK timeout will be enabled */
                    outbound_signals |= CHANACK;        /*   then acknowledge the CHANSO signal this time only */
                }

            else if (load_operation != load_bufr_IOCW)  /* otherwise if loading the IOAW into the buffer if enabled */
                working_set |= DWRITESTB;               /*   then set the buffer to the inbound value */

            scmb [card].device_sr = SET;                /* request channel service */
            break;


        case READNEXTWD:
            if (CN_CNTR (scmb [card].control_word) == count_READNEXTWD) /* if counting is enabled for this signal */
                increment_counter (card);                               /*   then increment the counter */
            break;


        case PREADSTB:
            if (scmb [card].device_end == CLEAR || MPX_BUS (card)) {    /* if device end is clear or we're on the MPX bus */
                outbound_value = scmb [card].counter;                   /*   then read the counter/buffer value */

                if (CN_CNTR (scmb [card].control_word) == count_PREADSTB)   /* if counting is enabled for this signal */
                    increment_counter (card);                               /*   then increment the counter */

                dprintf (scmb_dev [card], DEB_XFER, "Counter/buffer value %06o read\n",
                         outbound_value);
                }
            break;


        case PWRITESTB:
            if (scmb [card].device_end == CLEAR || MPX_BUS (card)) {    /* if device end is clear or we're on the MPX bus */
                if (scmb [card].control_word & CN_TERM_COMP) {          /*   then if we're doing a comparison */
                    if (scmb [card].counter != inbound_value)           /*     and the inbound value doesn't match */
                        scmb [card].flags |= ST_END_MISCMP;             /*       then set the miscompare flag */

                    dprintf (scmb_dev [card], DEB_XFER, "Inbound value %06o compared to counter/buffer value %06o\n",
                             inbound_value, scmb [card].counter);
                    }

                else if (CN_CNTR (scmb [card].control_word) < count_PWRITESTB) {    /* otherwise if we're not counting writes */
                    scmb [card].counter = inbound_value;                            /*   then set the counter/buffer */

                    dprintf (scmb_dev [card], DEB_XFER, "Counter/buffer value %06o written\n",
                             inbound_value);
                    }

                if (CN_CNTR (scmb [card].control_word) == count_PWRITESTB)  /* if counting is enabled for this signal */
                    increment_counter (card);                               /*   then increment the counter */
                }
            break;


        case SETJMP:
            if (scmb [card].control_word & CN_JMPMET)   /* if conditional jumps are configured to succeed */
                scmb [card].jump_met = SET;             /*   then set JMPMET status */
            break;


        case EOT:
            if (CN_CNTR (scmb [card].control_word) == count_EOT)    /* if counting is enabled for this signal */
                increment_counter (card);                           /*   then increment the counter */

            scmb [card].flags |= ST_EOT;                            /* set the end of transfer status */
            break;


        case XFERERROR:
            if (scmb [card].stop_transfer == CLEAR) {   /* if we haven't stopped yet */
                clear_logic (card);                     /*   then clear the interface and abort the transfer */

                scmb [card].stop_transfer = SET;                /* inhibit another interface clear */
                scmb [card].flags |= ST_XFERERR | ST_CLEAR_IF;  /* set the transfer error and clear interface status */

                sim_cancel (&scmb_unit [card]);         /* cancel any pending delayed SR assertion */

                dibptr->interrupt_request = SET;        /* set the interrupt request flip-flop */
                outbound_signals |= INTREQ;             /*   and request the interrupt */
                }
            break;


        case DSETMASK:                                  /* not used by this interface */
        case ACKSR:                                     /* not used by this interface */
        case PFWARN:                                    /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }


if (scmb [card].flags & END_CONDITION)                  /* if a termination condition is present */
    if ((scmb [card].control_word & CN_CLEAR_IF) == 0)  /*   then if we want a device end */
        scmb [card].device_end = SET;                   /*     then indicate a device end abort */

    else if (scmb [card].stop_transfer == CLEAR) {      /* otherwise if we haven't stopped yet */
        clear_logic (card);                             /*   then clear the interface and abort the transfer */

        scmb [card].stop_transfer = SET;                /* inhibit another interface clear */
        scmb [card].flags |= ST_CLEAR_IF;               /*   and set the clear interface status */

        dibptr->interrupt_request = SET;                /* set the request flip-flop */
        outbound_signals |= INTREQ;                     /*   and request the interrupt */
        }

if (scmb [card].control_word & CN_HSREQ)                /* if high-speed requests are enabled */
    assert_sr = D_FF (scmb [card].channel_sr            /*   then assert SR immediately if indicated */
                      | scmb [card].device_sr);

else {                                                  /* otherwise assert SR immediately */
    assert_sr = scmb [card].channel_sr;                 /*   only if the channel is requesting service */

    if ((! assert_sr & scmb [card].device_sr)           /* if a delayed device SR assertion is requested */
      && (MPX_BUS (card) || outbound_signals & CHANACK) /*   and we're on the MPX bus or CHANACK is not inhibited */
      && (scmb [card].control_word & CN_NOSR) == 0) {   /*     and channel service is not inhibited */
        sim_activate (&scmb_unit [card],                /*       then schedule SR assertion in 5 microseconds */
                      scmb_unit [card].wait);

        dprintf (scmb_dev [card], DEB_SERV, "Delay %u SR service scheduled\n",
                 scmb_unit [card].wait);
        }
    }


if (assert_sr)                                          /* if a service request is indicated */
    if (MPX_BUS (card))                                 /*   then if we're on the multiplexer bus */
        outbound_signals |= SRn;                        /*     then assert the SRn signal */
    else if ((scmb [card].control_word & CN_NOSR) == 0) /*   otherwise if channel service is not inhibited */
        outbound_signals |= CHANSR;                     /*     then assert the CHANSR signal */

if (scmb [card].jump_met == SET)                        /* if the jump met flip-flop is set */
    outbound_signals |= JMPMET;                         /*   then assert the JMPMET signal */

if (scmb [card].device_end == SET && SEL_BUS (card)) /* if device end is set and we're on the SEL bus */
    outbound_signals |= DEVEND;                         /*   then assert the DEVEND signal */

dprintf (scmb_dev [card], DEB_IOB, "Returned data %06o with signals %s\n",
         outbound_value, fmt_bitset (outbound_signals, outbound_format));

return IORETURN (outbound_signals, outbound_value);     /* return the outbound signals and value */
}


/* Service the SCMB.

   The service routine delays assertion of channel service request if the SCMB
   is not in high-speed mode.  The delay corresponds to five microseconds.

   It is important that scheduling not be performed if the channel is given an
   abort condition.  Otherwise, SR would be asserted while the channel is idle
   or servicing another device.
*/

static t_stat scmb_service (UNIT *uptr)
{
const CARD_ID card = (CARD_ID) (uptr == &scmb_unit [card2]);    /* the ID number of the card */

dprintf (scmb_dev [card], DEB_SERV, "SR service entered\n");

if (MPX_BUS (card))                                     /* if we're connected to the multiplexer channel */
    mpx_assert_SRn (&scmb_dib [card]);                  /*   then assert the SRn signal */
else                                                    /* otherwise we're connected to the selector channel */
    sel_assert_CHANSR (&scmb_dib [card]);               /*   so assert the CHANSR signal */

return SCPE_OK;
}


/* Reset the SCMB.

   This routine is called for a RESET or RESET SCMB command.  It is the
   simulation equivalent of the IORESET signal, which is asserted by the front
   panel LOAD and DUMP switches.

   For this interface, IORESET is identical to a Programmed Master Reset, which
   corresponds to the internal RST1 signal.

   For a power-on reset, the logical name "SCMB1" is assigned to the first SCMB
   card, so that it may referenced either as that name or as "SCMB" for use
   where only one SCMB is needed.
*/

static t_stat scmb_reset (DEVICE *dptr)
{
const DIB *const dibptr = (DIB *) (dptr->ctxt);         /* the DIB pointer */
const CARD_ID card = (CARD_ID) (dibptr->card_index);    /* the ID number of the card */

if ((sim_switches & SWMASK ('P'))                       /* if this is a power-on reset */
  && card == card1                                      /*   and we're being called for SCMB1 */
  && scmb_dev [card1].lname == NULL)                    /*     and the logical name has not been set yet */
    scmb_dev [card1].lname = strdup ("SCMB1");          /*       then allocate and initialize the name */

scmb [card].counter = 0;                                /* clear the counter/buffer register */
scmb [card].control_word = 0;                           /*   and the control word register */

sio_reset (card);                                       /* reset the remainder */
clear_logic (card);                                     /*   of the card logic */

sim_cancel (dptr->units);                               /* cancel any pending delayed SR assertion */

return SCPE_OK;
}


/* Set the bus connection.

   The SCMB may be connected either to the multiplexer or the selector channel
   bus.  If the interface is being moved from the multiplexer to the selector,
   save the SCMB's current service request number and set it to "unused" so that
   multiplexer initialization won't pick it up by mistake.
*/

static t_stat scmb_set_bus (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const CARD_ID card = (CARD_ID) (uptr == &scmb_unit [card2]);

if (value == UNIT_W1_SEL && MPX_BUS (card)) {                       /* if we're moving from MPX to SEL */
    scmb [card].saved_srn = scmb_dib [card].service_request_number; /*   then save the current SR number */
    scmb_dib [card].service_request_number = SRNO_UNUSED;           /*     for later restoration */
    }

else if (value == 0 && SEL_BUS (card))                              /* otherwise if moving from SEL to MPX */
    scmb_dib [card].service_request_number = scmb [card].saved_srn; /*   then restore the previous SR number */

return SCPE_OK;
}



/* SCMB local utility routines */



/* Reset for a new program.

   This routine is called for an IORESET signal, a Programmed Master Reset, or
   in response to an SIO instruction.  It corresponds in hardware to the
   internal RST2 signal, which is generated to clear the SCMB logic in
   preparation for a new I/O program.
*/

static void sio_reset (CARD_ID card)
{
scmb [card].jump_met   = CLEAR;                         /* clear the JMPMET */
scmb [card].device_end = CLEAR;                         /*   and DEVEND flip-flops */

scmb [card].flags = 0;                                  /* clear the flags */

scmb_dib [card].interrupt_request = CLEAR;              /* clear the interrupt request flip-flop */

return;
}


/* Reset the interface logic.

   This routine is called for an IORESET signal, a Programmed Master Reset, or
   an internal CLRIL signal, which, if enabled, is generated for a condition
   that terminates an I/O program.  It corresponds in hardware to the internal
   RST3 signal.
*/

static void clear_logic (CARD_ID card)
{
scmb [card].sio_busy    = CLEAR;                        /* clear the SIO Busy flip-flop */

scmb [card].channel_sr  = CLEAR;                        /* clear the channel */
scmb [card].device_sr   = CLEAR;                        /*   and device service request flip-flops */

scmb [card].input_xfer  = CLEAR;                        /* clear the input */
scmb [card].output_xfer = CLEAR;                        /*   and output transfer flip-flops */

scmb_dib [card].interrupt_active = CLEAR;               /* clear the interrupt active flip-flop */

if (SEL_BUS (card) && ! sel_is_idle)                    /* if we're connected to the selector channel and it's busy */
    sel_assert_REQ (&scmb_dib [card]);                  /*   then abort the transfer */

return;
}


/* Increment the counter.

   Increment the counter/buffer register in response to an enabled count
   condition.  If the count rolls over, and the "terminate on terminal count"
   condition is enabled, then set the end-on-terminal-count status.
*/

static void increment_counter (CARD_ID card)
{
scmb [card].counter = scmb [card].counter + 1 & R_MASK; /* increment the counter with rollover */

if (scmb [card].counter == 0                            /* if the counter rolled over */
  && scmb [card].control_word & CN_TERM_COUNT)          /*   and termination is enabled */
    scmb [card].flags |= ST_END_COUNT;                  /*     then set the terminal count flag */

return;
}
