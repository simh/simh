/* hp2100_ipl.c: HP 12875A Processor Interconnect simulator

   Copyright (c) 2002-2016, Robert M. Supnik
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

   IPLI, IPLO   12875A Processor Interconnect

   11-Jul-18    JDB     Revised I/O model
   22-May-18    JDB     Added process synchronization commands
   01-May-18    JDB     Removed ioCRS counter, as consecutive ioCRS calls are no longer made
   26-Mar-18    JDB     Converted from socket to shared memory connections
   28-Feb-18    JDB     Added the special IOP BBL
   13-Aug-17    JDB     Revised so that only IPLI boots
   19-Jul-17    JDB     Removed unused "ipl_stopioe" variable and register
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   15-Mar-17    JDB     Trace flags are now global
                        Changed DEBUG_PRJ calls to tpprintfs
   10-Mar-17    JDB     Added IOBUS to the debug table
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   14-Sep-15    JDB     Exposed "ipl_edtdelay" via a REG_HIDDEN to allow user tuning
                        Corrected typos in comments and strings
   05-Jun-15    JDB     Merged 3.x and 4.x versions using conditionals
   11-Feb-15    MP      Revised ipl_detach and ipl_dscln for new sim_close_sock API
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   12-Dec-12    MP      Revised ipl_attach for new socket API
   25-Oct-12    JDB     Removed DEV_NET to allow restoration of listening ports
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added CARD_INDEX casts to dib.card_index
   07-Apr-11    JDB     A failed STC may now be retried
   28-Mar-11    JDB     Tidied up signal handling
   27-Mar-11    JDB     Consolidated reporting of consecutive CRS signals
   29-Oct-10    JDB     Revised for new multi-card paradigm
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   15-Jul-08    JDB     Revised EDT handler to refine completion delay conditions
   09-Jul-08    JDB     Revised ipl_boot to use ibl_copy
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   01-Mar-07    JDB     IPLI EDT delays DMA completion interrupt for TSB
                        Added debug printouts
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Oct-04    JDB     Fixed enable/disable from either device
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   21-Dec-03    RMS     Adjusted ipl_ptime for TSB (from Mike Gemeny)
   09-May-03    RMS     Added network device flag
   31-Jan-03    RMS     Links are full duplex (found by Mike Gemeny)

   References:
     - 12875A Processor Interconnect Kit Operating and Service Manual
         (12875-90002, January 1974)
     - 12566B[-001/2/3] Microcircuit Interface Kits Operating and Service Manual
         (12566-90015,  April 1976)

   The 12875A Processor Interconnect Kit consists four 12566A Microcircuit
   Interface cards.  Two are used in each processor.  One card in each system is
   used to initiate transmissions to the other, and the second card is used to
   receive transmissions from the other.  Each pair of cards forms a
   bidirectional link, as the sixteen data lines are cross-connected, so that
   data sent and status returned are supported.  In each processor, data is sent
   on the lower priority card and received on the higher priority card.  Two
   sets of cards are used to support simultaneous transmission in both
   directions.


   Implementation notes:

    1. The "IPL" ("InterProcessor Link") designation is used throughout this
       file for historical reasons, although HP designates this device as the
       Processor Interconnect Kit.
*/



#include <signal.h>

#include "hp2100_defs.h"
#include "hp2100_io.h"

#include "sim_timer.h"

#if (SIM_MAJOR >= 4)
  #include "sim_fio.h"
#else
  #include "sim_shmem.h"
#endif



/* Process synchronization definitions */


/* Windows process synchronization.


   Implementation notes:

    1. SIMH and the HP2100 simulator define the CONST and INTERFACE macros,
       respectively.  Including "windows.h" here redefines these two symbols, so
       we save and then restore their simulator definitions when including the
       Windows header file.
*/

#if defined (_WIN32)

#pragma push_macro("CONST")
#pragma push_macro("INTERFACE")

#undef CONST
#undef INTERFACE

#include <windows.h>

#pragma pop_macro("CONST")
#pragma pop_macro("INTERFACE")


typedef HANDLE              EVENT;              /* the event type */

#define NO_EVENT            NULL                /* the initial (undefined) event value */


/* UNIX process synchronization */

#elif defined (HAVE_SEMAPHORE)

#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

typedef sem_t               *EVENT;             /* the event type */

#define NO_EVENT            SEM_FAILED          /* the initial (undefined) event value */

static t_bool event_fallback = FALSE;           /* TRUE if semaphores are defined but not supported */


/* Process synchronization stub */

#else

typedef uint32              EVENT;              /* the event type */

#define NO_EVENT            0                   /* the initial (undefined) event value */


#endif



/* Program limits */

#define CARD_COUNT          2                   /* count of cards supported */


/* ATTACH mode switches */

#define SP                  SWMASK ('S')        /* SP switch */
#define IOP                 SWMASK ('I')        /* IOP switch */

#define LISTEN              SWMASK ('L')        /* listen switch (deprecated) */
#define CONNECT             SWMASK ('C')        /* connect switch (deprecated) */


/* Per-unit state variables */

#define ID                  u3                  /* session identifying number */


/* Unit flags */

#define UNIT_DIAG_SHIFT     (UNIT_V_UF + 0)     /* diagnostic mode */

#define UNIT_DIAG           (1u << UNIT_DIAG_SHIFT)


/* Unit references */

typedef enum {
    ipli,                                       /* inbound card index */
    iplo                                        /* outbound card index */
    } CARD_INDEX;

#define ipli_unit           ipl_unit [ipli]     /* inbound card unit */
#define iplo_unit           ipl_unit [iplo]     /* outbound card unit */


/* Device information block references */

#define ipli_dib            ipl_dib [ipli]      /* inbound card DIB */
#define iplo_dib            ipl_dib [iplo]      /* outbound card DIB */


/* IPL state */

typedef struct {
    HP_WORD    output_word;                     /* output word register */
    HP_WORD    input_word;                      /* input word register */
    FLIP_FLOP  command;                         /* command flip-flop */
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE ipl [CARD_COUNT];             /* per-card state */


/* IPL I/O device state.

   The 12566B Microcircuit Interface provides a 16-bit Data Out bus and a 16-bit
   Data In bus, as well as an outbound Device Command signal and an inbound
   Device Flag signal to indicate data availability.  The output and input
   states are modelled by a pair of structures that also contain Boolean flags
   to indicate cable connectivity.

   The two interface cards provided each may be connected in one of four
   possible ways:

    1. No connection (the I/O cable is not connected).

    2. Loopback connection (a loopback connector is in place).

    3. Cross connection (an I/O cable connects one card to the other card in the
       same machine).

    4. Processor interconnection (an I/O cable connects a card in one machine to
       a card in the other machine).

   In simulation, these four connection states are modelled by setting input and
   output pointers (accessors) to point at the appropriate state structures, as
   follows:

    1. The input and output accessors point at separate local input and output
       state structures.

    2. The input and output accessors point at a single local state structure.

    3. The input and output accessors of one card point at the separate local
       state structures of the other card.

    4. The input and output accessors of one card point at the separate shared
       state structures of the other card.

   Connection is accomplished by having an output accessor and an input accessor
   point at the same state structure.  Graphically, the four possibilities are:

     1. No connection:

                             +------------------+
        card [n].output -->  |     Data Out     |
                             +------------------+
                             |  Device Command  |
                             +------------------+

                             +------------------+
        card [n].input  -->  |     Data In      |
                             +------------------+
                             |   Device Flag    |
                             +------------------+


     2. Loopback connection:

                             +------------------+------------------+
        card [n].output -->  |     Data Out     |     Data In      |  <-- card [n].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+


     3. Cross connection:

                             +------------------+------------------+
        card [0].output -->  |     Data Out     |     Data In      |  <-- card [1].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [0].input  -->  |     Data In      |     Data Out     |  <-- card [1].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+


     4. Processor interconnection:

                             +------------------+------------------+
        card [0].output -->  |     Data Out     |     Data In      |  <-- card [1].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [0].input  -->  |     Data In      |     Data Out     |  <-- card [1].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+

                             +------------------+------------------+
        card [1].output -->  |     Data Out     |     Data In      |  <-- card [0].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [1].input  -->  |     Data In      |     Data Out     |  <-- card [0].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+

   In all but case 1, two accessors point at the same structure but with
   different views.
*/

typedef struct {
    t_bool   cable_connected;                   /* TRUE if the inbound cable is connected */
    t_bool   device_flag_in;                    /* external DEVICE FLAG signal state */
    HP_WORD  data_in;                           /* external DATA IN signal bus */
    } INPUT_STATE, *INPUT_STATE_PTR;

typedef struct {
    t_bool   cable_connected;                   /* TRUE if the outbound cable is connected */
    t_bool   device_command_out;                /* external DEVICE COMMAND signal state */
    HP_WORD  data_out;                          /* external DATA OUT signal bus */
    } OUTPUT_STATE, *OUTPUT_STATE_PTR;

typedef struct {                                /* the normal ("forward direction") state view */
    INPUT_STATE   input;
    OUTPUT_STATE  output;
    } FORWARD_STATE;

typedef struct {                                /* the cross-connected ("reverse direction") state view */
    OUTPUT_STATE  output;
    INPUT_STATE   input;
    } REVERSE_STATE;

typedef union {                                 /* the state may be accessed in either direction */
    FORWARD_STATE  forward;
    REVERSE_STATE  reverse;
    } IO_STATE, *IO_STATE_PTR;

typedef IO_STATE IO_ARRAY [CARD_COUNT];         /* an array of I/O states for the two cards */

static IO_ARRAY dev_bus;                        /* the local device I/O bus states */

typedef struct {
    INPUT_STATE_PTR   input;                    /* the input accessor */
    OUTPUT_STATE_PTR  output;                   /* the output accessor */
    } STATE_PTRS;

static STATE_PTRS io_ptrs [CARD_COUNT] = {      /* the card accessors pointing at the local state */
    { &dev_bus [ipli].forward.input,            /*   card [0].input */
      &dev_bus [ipli].forward.output },         /*   card [0].output */

    { &dev_bus [iplo].forward.input,            /*   card [1].input */
      &dev_bus [iplo].forward.output }          /*   card [1].output */
    };


/* IPL interface state */

static t_bool cpu_is_iop     = FALSE;           /* TRUE if this is the IOP instance, FALSE if SP instance */
static int32  edt_delay      = 1;               /* EDT delay (msec) */
static int32  poll_wait      = 50;              /* maximum poll wait time */

static char   event_name [PATH_MAX];            /* the event name */
static uint32 event_error    = 0;               /* the host OS error code from a failed process sync call */
static t_bool wait_aborted   = FALSE;           /* TRUE if the user aborted a SET IPL WAIT command */
static EVENT  event_id       = NO_EVENT;        /* the synchronization event */
static SHMEM  *memory_region = NULL;            /* a pointer to the shared memory region descriptor */


/* IPL local SCP support routines */

static INTERFACE ipl_interface;


/* IPL local SCP support routines */

static t_stat ipl_set_diag (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat ipl_set_sync (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

static t_stat ipl_reset  (DEVICE *dptr);
static t_stat ipl_boot   (int32 unitno, DEVICE *dptr);

static t_stat ipl_attach (UNIT *uptr, CONST char *cptr);
static t_stat ipl_detach (UNIT *uptr);


/* IPL local utility routines */

static t_stat card_service  (UNIT *uptr);
static void   activate_unit (UNIT *uptr);
static void   abort_handler (int signal);


/* Process synchronization routines */

static uint32 create_event       (const char *name, EVENT *event);
static uint32 destroy_event      (const char *name, EVENT *event);
static t_bool event_is_undefined (EVENT event);
static uint32 wait_event         (EVENT event, uint32 wait_in_ms, t_bool *signaled);
static uint32 signal_event       (EVENT event);


/* IPL SCP data structures */

/* Device information blocks */

static DIB ipl_dib [CARD_COUNT] = {
    { &ipl_interface,                                   /* the device's I/O interface function pointer */
      IPLI,                                             /* the device's select code (02-77) */
      0,                                                /* the card index */
      "12875A Processor Interconnect Lower Data PCA",   /* the card description */
      "12992K Processor Interconnect Loader" },         /* the ROM description */

    { &ipl_interface,                                   /* the device's I/O interface function pointer */
      IPLO,                                             /* the device's select code (02-77) */
      1,                                                /* the card index */
      "12875A Processor Interconnect Upper Data PCA",   /* the card description */
      NULL }                                            /* the ROM description */
    };


/* Unit lists */

static UNIT ipl_unit [CARD_COUNT] = {
    { UDATA (&card_service, UNIT_ATTABLE, 0) },
    { UDATA (&card_service, UNIT_ATTABLE, 0) }
    };


/* Register lists */

static REG ipli_reg [] = {
/*    Macro   Name      Location                Width  Offset         Flags         */
/*    ------  --------  ----------------------  -----  ------  -------------------- */
    { ORDATA (IBUF,     ipl [ipli].input_word,   16)                                },
    { ORDATA (OBUF,     ipl [ipli].output_word,  16)                                },
    { FLDATA (CTL,      ipl [ipli].control,              0)                         },
    { FLDATA (FLG,      ipl [ipli].flag,                 0)                         },
    { FLDATA (FBF,      ipl [ipli].flag_buffer,          0)                         },
    { DRDATA (TIME,     poll_wait,               24),          PV_LEFT              },
    { DRDATA (EDTDELAY, edt_delay,               32),          PV_LEFT | REG_HIDDEN },
    { DRDATA (EVTERR,   event_error,             32),          PV_LEFT | REG_HRO    },

      DIB_REGS (ipli_dib),

    { NULL }
    };

static REG iplo_reg [] = {
/*    Macro   Name      Location                Width  Offset         Flags         */
/*    ------  --------  ----------------------  -----  ------  -------------------- */
    { ORDATA (IBUF,     ipl [iplo].input_word,   16)                                },
    { ORDATA (OBUF,     ipl [iplo].output_word,  16)                                },
    { FLDATA (CTL,      ipl [iplo].control,              0)                         },
    { FLDATA (FLG,      ipl [iplo].flag,                 0)                         },
    { FLDATA (FBF,      ipl [iplo].flag_buffer,          0)                         },
    { DRDATA (TIME,     poll_wait,               24),          PV_LEFT              },

      DIB_REGS (iplo_dib),

    { NULL }
    };


/* Modifier lists */

static MTAB ipl_mod [] = {
/*    Mask Value  Match Value  Print String       Match String  Validation      Display  Descriptor */
/*    ----------  -----------  -----------------  ------------  --------------  -------  ---------- */
    { UNIT_DIAG,  UNIT_DIAG,  "diagnostic mode",  "DIAGNOSTIC", &ipl_set_diag,  NULL,    NULL       },
    { UNIT_DIAG,  0,          "link mode",        "LINK",       &ipl_set_diag,  NULL,    NULL       },

/*    Entry Flags           Value  Print String  Match String  Validation      Display        Descriptor        */
/*    --------------------  -----  ------------  ------------  --------------  -------------  ----------------- */
    { MTAB_XDV,               0u,  NULL,         "WAIT",       &ipl_set_sync,  NULL,          NULL              },
    { MTAB_XDV,               1u,  NULL,         "SIGNAL",     &ipl_set_sync,  NULL,          NULL              },

    { MTAB_XDV,               2u,  "SC",         "SC",         &hp_set_dib,    &hp_show_dib,  (void *) &ipl_dib },
    { MTAB_XDV | MTAB_NMO,   ~2u,  "DEVNO",      "DEVNO",      &hp_set_dib,    &hp_show_dib,  (void *) &ipl_dib },
    { 0 }
    };


/* Debugging trace lists */

static DEBTAB ipl_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "PSERV", TRACE_PSERV },                   /* trace periodic unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptors */

DEVICE ipli_dev = {
    "IPL",                                      /* device name (logical name "IPLI") */
    &ipli_unit,                                 /* unit array */
    ipli_reg,                                   /* register array */
    ipl_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    16,                                         /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ipl_reset,                                 /* reset routine */
    &ipl_boot,                                  /* boot routine */
    &ipl_attach,                                /* attach routine */
    &ipl_detach,                                /* detach routine */
    &ipli_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    ipl_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

DEVICE iplo_dev = {
    "IPLO",                                     /* device name */
    &iplo_unit,                                 /* unit array */
    iplo_reg,                                   /* register array */
    ipl_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    16,                                         /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ipl_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &ipl_attach,                                /* attach routine */
    &ipl_detach,                                /* detach routine */
    &iplo_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    ipl_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dptrs [CARD_COUNT] = {
    &ipli_dev,
    &iplo_dev
    };



/* Microcircuit interface.

   In the link mode, the IPLI and IPLO devices are linked via a shared memory
   region to the corresponding cards in another CPU instance.  If only one or
   the other device is in the diagnostic mode, we simulate the attachment of a
   loopback connector to that device.  If both devices are in the diagnostic
   mode, we simulate the attachment of the interprocessor cable between IPLI and
   IPLO in this machine.


   Implementation notes:

    1. 2000 Access has a race condition that manifests itself by an apparently
       normal boot and operational system console but no PLEASE LOG IN response
       to terminals connected to the multiplexer.  The frequency of occurrence
       is higher on multiprocessor host systems, where the SP and IOP instances
       may execute concurrently.

       The cause is this code in the SP disc loader source (2883.asm, 7900.asm,
       790X.asm, 79X3.asm, and 79XX.asm):

         LDA SDVTR     REQUEST
         JSB IOPMA,I     DEVICE TABLE
         [...]
         STC DMAHS,C   TURN ON DMA
         SFS DMAHS     WAIT FOR
         JMP *-1         DEVICE TABLE
         STC CH2,C     SET CORRECT
         CLC CH2         FLAG DIRECTION

       The STC/CLC normally would cause a second "request device table" command
       to be recognized by the IOP, except that the IOP DMA setup routine
       "DMAXF" (in D61.asm) has specified an end-of-block CLC that holds off the
       IPL interrupt, and the completion interrupt routine "DMCMP" ends with a
       STC,C that clears the IPL flag.

       In hardware, the two CPUs are essentially interlocked by the DMA
       transfer, and DMA completion interrupts occur almost simultaneously.
       Therefore, the STC/CLC in the SP is guaranteed to occur before the STC,C
       in the IOP.  Under simulation, and especially on multiprocessor hosts,
       that guarantee does not hold.  If the STC/CLC occurs after the STC,C,
       then the IOP starts a second device table DMA transfer, which the SP is
       not expecting.  The IOP never processes the subsequent "start
       timesharing" command, and the multiplexer is non-responsive.

       We employ a workaround that decreases the incidence of the problem: DMA
       output completion interrupts are delayed to allow the other SIMH instance
       a chance to process its own DMA completion.  We do this by processing the
       EDT (End Data Transfer) I/O backplane signal and "sleep"ing for a short
       time if the transfer was an output transfer to the input channel, i.e.,
       a data response to the SP.  This improves the race condition by delaying
       the IOP until the SP has a chance to receive the last word, recognize its
       own DMA input completion, drop out of the SFS loop, and execute the
       STC/CLC.  The delay, "edt_delay", is initialized to one millisecond but
       is exposed via a hidden IPLI register, "EDTDELAY", that allows the user
       to lengthen the delay if necessary.

       The condition is only improved, and not solved, because "sleep"ing the
       IOP doesn't guarantee that the SP will actually execute.  It's possible
       that a higher-priority host process will preempt the SP, and that at the
       sleep expiration, the SP still has not executed the STC/CLC.  Still, in
       testing, the incidence dropped dramatically, so the problem is much less
       intrusive.
*/

static SIGNALS_VALUE ipl_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const char * const iotype [] = { "Status", "Command" };
const CARD_INDEX card = (CARD_INDEX) dibptr->card_index;    /* set card selector */
UNIT * const uptr = &(ipl_unit [card]);                     /* associated unit pointer */

INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            ipl [card].flag_buffer = CLEAR;             /* reset the flag buffer */
            ipl [card].flag        = CLEAR;             /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            ipl [card].flag_buffer = SET;               /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (ipl [card].flag_buffer == SET)          /* if the flag buffer flip-flop is set */
                ipl [card].flag = SET;                  /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (ipl [card].flag == CLEAR)                       /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (ipl [card].flag == SET)                         /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = ipl [card].input_word;     /* get return data */

            tpprintf (dptrs [card], TRACE_CSRW, "%s input word is %06o\n",
                      iotype [card ^ 1], ipl [card].input_word);
            break;


        case ioIOO:                                         /* I/O data output */
            ipl [card].output_word = inbound_value;         /* clear supplied status */

            io_ptrs [card].output->data_out = ipl [card].output_word;

            tpprintf (dptrs [card], TRACE_CSRW, "%s output word is %06o\n",
                      iotype [card], ipl [card].output_word);
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            ipl [card].flag_buffer = SET;               /* set the flag buffer flip-flop */
            ipl [card].output_word = 0;                 /*   and clear the output register */

            io_ptrs [card].output->data_out = 0;
            break;


        case ioCRS:                                     /* Control Reset */
            ipl [card].control = CLEAR;                 /* clear the control flip-flop */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            ipl [card].control = CLEAR;                 /* clear the control flip-flop */
            break;


        case ioSTC:                                             /* Set Control flip-flop */
            ipl [card].control = SET;                           /* set the control flip-flop */

            io_ptrs [card].output->device_command_out = TRUE;   /* assert Device Command */

            if (uptr->flags & UNIT_DIAG)                        /* if this card is in the diagnostic mode */
                if (ipl_unit [card ^ 1].flags & UNIT_DIAG) {    /*   then if both cards are in diagnostic mode */
                    ipl_unit [card ^ 1].wait = 1;               /*     then schedule the other card */
                    activate_unit (&ipl_unit [card ^ 1]);       /*       for immediate reception */
                    }

                else {                                          /*   otherwise simulate a loopback */
                    uptr->wait = 1;                             /*     by scheduling this card */
                    activate_unit (uptr);                       /*       for immediate reception */
                    }

            tpprintf (dptrs [card], TRACE_XFER, "Word %06o sent to link\n",
                      ipl [card].output_word);
            break;


        case ioEDT:                                     /* end data transfer */
            if (cpu_is_iop                              /* if this is the IOP instance */
              && inbound_signals & ioIOO                /*   and the card is doing output */
              && card == ipli) {                        /*     on the input card */

                tprintf (ipli_dev, TRACE_CMD, "Delaying DMA completion interrupt for %d msec\n",
                         edt_delay);

                sim_os_ms_sleep (edt_delay);            /*       then delay DMA completion */
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (ipl [card].control & ipl [card].flag)   /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (ipl [card].control & ipl [card].flag    /* if the control and flag */
              & ipl [card].flag_buffer)                 /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */

            if (ipl [card].flag == SET)                 /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            ipl [card].flag_buffer = CLEAR;             /* clear the flag buffer flip-flop */
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


        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* Unit service - poll for input */

static t_stat card_service (UNIT *uptr)
{
static uint32 delta [CARD_COUNT] = { 0, 0 };                /* per-card accumulated time between receptions */
const CARD_INDEX card = (CARD_INDEX) (uptr == &iplo_unit);  /* set card selector */
t_stat status = SCPE_OK;

tpprintf (dptrs [card], TRACE_PSERV, "Poll delay %d service entered\n",
          uptr->wait);

delta [card] = delta [card] + uptr->wait;               /* update the accumulated time */

if (io_ptrs [card].input->device_flag_in == TRUE) {     /* if the Device Flag is asserted */
    io_ptrs [card].input->device_flag_in = FALSE;       /*   then clear it */

    ipl [card].input_word = io_ptrs [card].input->data_in;  /* read the data input lines */

    tpprintf (dptrs [card], TRACE_XFER, "Word %06o delta %u received from link\n",
              ipl [card].input_word, delta [card]);

    ipl [card].flag_buffer = SET;                       /* set the flag buffer */
    io_assert (dptrs [card], ioa_ENF);                  /*   and flag flip-flops */

    io_ptrs [card].output->device_command_out = FALSE;  /* reset Device Command */

    uptr->wait = 1;                                     /* restart polling at the minimum time */
    delta [card] = 0;                                   /*   and clear the accumulated time */
    }

else {                                                  /* otherwise Device Flag is denied */
    uptr->wait = uptr-> wait * 2;                       /*   so double the wait time for the next check */

    if (uptr->wait > poll_wait)                         /* if the new time is greater than the maximum time */
        uptr->wait = poll_wait;                         /*   then limit it to the maximum */

    if (io_ptrs [card].input->cable_connected == FALSE  /* if the interconnecting cable is not present */
      && cpu_io_stop (uptr))                            /*   and the I/O error stop is enabled */
        status = STOP_NOCONN;                           /*     then report the disconnection */
    }

if (uptr->flags & UNIT_ATT)                             /* if the link is active */
    activate_unit (uptr);                               /*   then continue to poll for input */

return status;                                          /* return the event service status */
}


/* Reset the IPL.

   This routine is called for a RESET, RESET IPLI, or RESET IPLO command.  It is
   the simulation equivalent of the POPIO signal, which is asserted by the front
   panel PRESET switch.

   For a power-on reset, the logical name "IPLI" is assigned to the first
   processor interconnect card, so that it may referenced either as that name or
   as "IPL" for use when a SET command affects both interfaces.
*/

static t_stat ipl_reset (DEVICE *dptr)
{
UNIT *uptr = dptr->units;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */
CARD_INDEX card = (CARD_INDEX) dibptr->card_index;      /* card number */

hp_enbdis_pair (dptr, dptrs [card ^ 1]);                /* ensure that the pair state is consistent */

if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    ipl [card].input_word = 0;
    ipl [card].output_word = 0;

    if (ipli_dev.lname == NULL)                         /* logical name unassigned? */
        ipli_dev.lname = strdup ("IPLI");               /* allocate and initialize the name */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

if (uptr->flags & UNIT_ATT) {                           /* if the link is active */
    uptr->wait = poll_wait;                             /*   then continue to poll for input */
    activate_unit (uptr);                               /*     at the idle rate */
    }

else                                                    /* otherwise the link is inactive */
    sim_cancel (uptr);                                  /*   so stop input polling */

return SCPE_OK;
}


/* Attach one end of the interconnecting cables.

   This routine connects the IPL device pair to a shared memory region.  This
   simulates connecting one end of the processor interconnect kit cables to the
   card pair in this CPU.  The command is:

     ATTACH [ -S | -I ] IPL <code>

   ...where <code> is a user-selected decimal number between 1 and 65535 that
   uniquely identifies the instance pair to interconnect.  The -S or -I switch
   indicates whether this instance is acting as the System Processor or the I/O
   Processor.  The command will be rejected if either device is in diagnostic
   mode, or if the <code> is omitted, malformed, or out of range.

   For backward compatibility with prior IPL implementations that used network
   interconnections, the following commands are also accepted:

     ATTACH [ -L ] [ IPLI | IPLO] <port-1>
     ATTACH   -C   [ IPLI | IPLO] <port-2>

   For these commands, -L or no switch indicates the SP instance, and -C
   indicates the IOP instance.  <port-1> and <port-2> used to indicate the
   network port numbers to use, but now it serves only to supply the code
   number from the lesser of the two values.

   Local memory is allocated to hold the code number string, which serves as the
   "attached file name" for the SCP SHOW command.  If memory allocation fails,
   the command is rejected.

   This routine creates a shared memory region and an event (semaphore) that are
   used to coordinate a data exchange with the other simulator instance.  If -S
   or -I is specified, then creation occurs after the ATTACH command is given
   for either the IPLI or IPLO device, and both devices are marked as attached.
   If -L or -C is specified, then both devices must be attached before creation
   occurs using the lower port number.

   Two object names that identify the shared memory region and synchronization
   event are derived from the <code> (or lower <port>) number:

     /HP 2100-MEM-<code>
     /HP 2100-EVT-<code>

   Each simulator instance must use the same <code> (or <port> pair) when
   attaching for the interconnection to occur.  This permits multiple instance
   pairs to operate simultaneously and independently, if desired.

   Once shared memory is allocated, pointers to the region for the SP and IOP
   instances are set so that the output card pointer of one instance and the
   input card pointer of the other instance reference the same memory structure.
   This accomplishes the interconnection, as a write to one instance's card will
   be seen by a read from the other instance's card.

   If the shared memory allocation succeeds but the process synchronization
   event creation fails, the routine returns "Command not completed" status to
   indicate that interconnection without synchronization is permitted.


   Implementation notes:

    1. The implementation supports process synchronization only on the local
       system.

    2. The object names begin with slashes to conform to POSIX requirements to
       guarantee that multiple instances to refer to the same shared memory
       region.  Omitting the slash results in implementation-defined behavior on
       POSIX systems.
*/

static t_stat ipl_attach (UNIT *uptr, CONST char *cptr)
{
t_stat       status;
int32        id_number;
char         object_name [PATH_MAX];
CONST        char *zptr;
char         *tptr;
IO_STATE_PTR isp;
UNIT         *optr;

if ((ipli_unit.flags | iplo_unit.flags) & UNIT_DIAG)    /* if either unit is in diagnostic mode */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (uptr->flags & UNIT_ATT)                        /* otherwise if the unit is currently attached */
    ipl_detach (uptr);                                  /*   then detach it first */

id_number = (int32) strtotv (cptr, &zptr, 10);          /* parse the command for the ID number */

if (cptr == zptr || *zptr != '\0' || id_number == 0)    /* if the parse failed or extra characters or out of range */
    return SCPE_ARG;                                    /*   then reject the attach with an invalid argument error */

else {                                                  /* otherwise a single number was specified */
    tptr = (char *) malloc (strlen (cptr) + 1);         /*   so allocate a string buffer to hold the ID */

    if (tptr == NULL)                                   /* if the allocation failed */
        return SCPE_MEM;                                /*   then reject the attach with an out-of-memory error */

    else {                                              /* otherwise */
        strcpy (tptr, cptr);                            /*   copy the ID number to the buffer */
        uptr->filename = tptr;                          /*     and assign it as the attached object name */

        uptr->flags |= UNIT_ATT;                        /* set the unit attached flag */
        uptr->ID = id_number;                           /*   and save the ID number */

        uptr->wait = poll_wait;                         /* set up the initial poll time */
        activate_unit (uptr);                           /*   and activate the unit to poll */
        }

    if ((sim_switches & (SP | IOP)) == 0)               /* if this is not a single-device attach */
        if (ipli_unit.ID == 0 || iplo_unit.ID == 0)     /*   then if both devices have not been attached yet */
            return SCPE_OK;                             /*     then we've done all we can do */

        else if (ipli_unit.ID < iplo_unit.ID)           /*   otherwise */
            id_number = ipli_unit.ID;                   /*     determine */
        else                                            /*       the lower */
            id_number = iplo_unit.ID;                   /*         ID number */

    else {                                              /* otherwise this is a single-device attach */
        if (uptr == &ipli_unit)                         /*   so if we are attaching the input unit */
            optr = &iplo_unit;                          /*     then point at the output unit */
        else                                            /*   otherwise we are attaching the output unit */
            optr = &ipli_unit;                          /*     so point at the input unit */

        optr->filename = tptr;                          /* assign the ID as the attached object name */

        optr->flags |= UNIT_ATT;                        /* set the unit attached flag */
        optr->ID = id_number;                           /*   and save the ID number */

        optr->wait = poll_wait;                         /* set up the initial poll time */
        activate_unit (optr);                           /*   and activate the unit to poll */
        }

    sprintf (object_name, "/%s-MEM-%d",                 /* generate the shared memory area name */
             sim_name, id_number);

    status = sim_shmem_open (object_name, sizeof dev_bus,   /* allocate the shared memory area */
                             &memory_region, (void **) &isp);

    if (status != SCPE_OK) {                                    /* if the allocation failed */
        ipl_detach (uptr);                                      /*   then detach this unit */
        return status;                                          /*     and report the error */
        }

    else {                                                      /* otherwise */
        cpu_is_iop = ((sim_switches & (CONNECT | IOP)) != 0);   /*   -C or -I imply that this is the I/O Processor */

        if (cpu_is_iop) {                                       /* if this is the IOP instance */
            io_ptrs [ipli].input  = &isp [iplo].reverse.input;  /*   then cross-connect */
            io_ptrs [ipli].output = &isp [iplo].reverse.output; /*     the input and output */
            io_ptrs [iplo].input  = &isp [ipli].reverse.input;  /*       interface cards to the */
            io_ptrs [iplo].output = &isp [ipli].reverse.output; /*         SP interface cards */
            }

        else {                                                  /* otherwise this is the SP instance */
            io_ptrs [ipli].input  = &isp [ipli].forward.input;  /*   so connect */
            io_ptrs [ipli].output = &isp [ipli].forward.output; /*     the interface cards */
            io_ptrs [iplo].input  = &isp [iplo].forward.input;  /*       to the I/O cables */
            io_ptrs [iplo].output = &isp [iplo].forward.output; /*         directly */
            }

        io_ptrs [ipli].output->cable_connected = TRUE;          /* indicate that the cables */
        io_ptrs [iplo].output->cable_connected = TRUE;          /*   have been connected */
        }

    sprintf (event_name, "/%s-EVT-%d",                  /* generate the process synchronization event name */
             sim_name, id_number);

    event_error = create_event (event_name, &event_id); /* create the event */

    if (event_error == 0)                               /* if event creation succeeded */
        return SCPE_OK;                                 /*   then report a successful attach */
    else                                                /* otherwise */
        return SCPE_INCOMP;                             /*   report that the command did not complete */
    }
}


/* Detach the interconnecting cables.

   This routine disconnects the IPL device pair from the shared memory region.
   This simulates disconnecting the processor interconnect kit cables from the
   card pair in this CPU.  The command is:

     DETACH IPL

   For backward compatibility with prior IPL implementations that used network
   interconnections, the following commands are also accepted:

     DETACH IPLI
     DETACH IPLO

   In either case, the shared memory region and process synchronization event
   are destroyed, and the card state pointers are reset to point at the local
   memory structure.  If a single ATTACH was done, a single DETACH will detach
   both devices and free the allocated "file name" memory.  The input poll is
   also stopped.

   If the event destruction failed, the routine returns "Command not completed"
   status to indicate that the error code register should be checked.
*/

static t_stat ipl_detach (UNIT *uptr)
{
UNIT *optr;

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    if (sim_switches & SIM_SW_REST)                     /*   then if this is a restoration call */
        return SCPE_OK;                                 /*     then return success */
    else                                                /*   otherwise this is a manual request */
        return SCPE_UNATT;                              /*     so complain that the unit is not attached */

if (ipli_unit.filename == iplo_unit.filename) {         /* if both units are attached to the same object */
    if (uptr == &ipli_unit)                             /*   then if we are detaching the input unit */
        optr = &iplo_unit;                              /*     then point at the output unit */
    else                                                /*   otherwise we are detaching the output unit */
        optr = &ipli_unit;                              /*     so point at the input unit */

    optr->filename = NULL;                              /* clear the other unit's attached object name */

    optr->flags &= ~UNIT_ATT;                           /* clear the other unit's attached flag */
    optr->ID = 0;                                       /*   and the ID number */

    sim_cancel (optr);                                  /* cancel the other unit's poll */
    }

free (uptr->filename);                                  /* free the memory holding the ID number */
uptr->filename = NULL;                                  /*   and clear the attached object name */

uptr->flags &= ~UNIT_ATT;                               /* clear the unit attached flag */
uptr->ID = 0;                                           /*   and the ID number */

sim_cancel (uptr);                                      /* cancel the poll */

io_ptrs [ipli].output->cable_connected = FALSE;         /* disconnect the cables */
io_ptrs [iplo].output->cable_connected = FALSE;         /*   from both cards */

io_ptrs [ipli].input  = &dev_bus [ipli].forward.input;  /* restore local control */
io_ptrs [ipli].output = &dev_bus [ipli].forward.output; /*   over the I/O state */
io_ptrs [iplo].input  = &dev_bus [iplo].forward.input;  /*     for both cards */
io_ptrs [iplo].output = &dev_bus [iplo].forward.output;

sim_shmem_close (memory_region);                        /* deallocate the shared memory region */
memory_region = NULL;                                   /*   and clear the region pointer */

event_error = destroy_event (event_name, &event_id);    /* destroy the event */

if (event_error == 0)                                   /* if the destruction succeeded */
    return SCPE_OK;                                     /*   then report success */
else                                                    /* otherwise */
    return SCPE_INCOMP;                                 /*   report that the command did not complete */
}


/* Set the diagnostic or link mode.

   This validation routine is entered with the "value" parameter set to zero if
   the unit is to be set into the link (normal) mode or non-zero if the unit is
   to be set into the diagnostic mode.  The character and descriptor pointers
   are not used.

   In addition to setting or clearing the UNIT_DIAG flag, the I/O state pointers
   are set to point at the appropriate state structure.  The selected pointer
   configuration depends on whether none, one, or both the IPLI and IPLO devices
   are in diagnostic mode.

   If both devices are in diagnostic mode, the pointers are set to point at
   their respective state structures but with the input and output pointers
   reversed.  This simulates connecting one of the interprocessor cables between
   the two cards within the same CPU, permitting the Processor Interconnect
   Cable Diagnostic to be run.

   If only one of the devices is in diagnostic mode, the pointers are set to
   point at the device's state structure with the input and output pointers
   reversed.  This simulates connected a loopback connector to the card,
   permitting the General Register Diagnostic to be run.

   If a device is in link mode, that device's pointers are set to point at the
   corresponding parts of the device's state structure.  This simulates a card
   with no cable connected.

   If the device is attached, setting it into diagnostic mode will detach it
   first.
*/

static t_stat ipl_set_diag (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (value) {                                            /* if this is an entry into diagnostic mode */
    ipl_detach (uptr);                                  /*   then detach it first */
    uptr->flags |= UNIT_DIAG;                           /*     before setting the flag */
    }

else                                                    /* otherwise this is an entry into link mode */
    uptr->flags &= ~UNIT_DIAG;                          /*   so clear the flag */

if (ipli_unit.flags & iplo_unit.flags & UNIT_DIAG) {        /* if both devices are now in diagnostic mode */
    io_ptrs [ipli].input  = &dev_bus [iplo].reverse.input;  /*   then connect the cable */
    io_ptrs [ipli].output = &dev_bus [ipli].forward.output; /*     so that the outputs of one card */
    io_ptrs [iplo].input  = &dev_bus [ipli].reverse.input;  /*       are connected to the inputs of the other card */
    io_ptrs [iplo].output = &dev_bus [iplo].forward.output; /*         and vice versa */

    io_ptrs [ipli].output->cable_connected = TRUE;          /* indicate that the cable */
    io_ptrs [iplo].output->cable_connected = TRUE;          /*   has been connected between the cards */
    }

else {                                                          /* otherwise */
    if (ipli_unit.flags & UNIT_DIAG) {                          /*   if the input card is in diagnostic mode */
        io_ptrs [ipli].input  = &dev_bus [ipli].reverse.input;  /*     then loop the card outputs */
        io_ptrs [ipli].output = &dev_bus [ipli].forward.output; /*       back to the inputs and vice versa */
        io_ptrs [ipli].output->cable_connected = TRUE;          /*         and indicate that the card is connected */
        }

    else {                                                      /*   otherwise the card is in link mode */
        io_ptrs [ipli].input  = &dev_bus [ipli].forward.input;  /*     so point at the card state */
        io_ptrs [ipli].output = &dev_bus [ipli].forward.output; /*       in the normal direction */
        io_ptrs [ipli].output->cable_connected = FALSE;         /*         and indicate that the card is not connected */
        }

    if (iplo_unit.flags & UNIT_DIAG) {                          /* otherwise */
        io_ptrs [iplo].input  = &dev_bus [iplo].reverse.input;  /*   if the output card is in diagnostic mode */
        io_ptrs [iplo].output = &dev_bus [iplo].forward.output; /*     then loop the card outputs */
        io_ptrs [iplo].output->cable_connected = TRUE;          /*       back to the inputs and vice versa */
        }                                                       /*         and indicate that the card is connected */

    else {
        io_ptrs [iplo].input  = &dev_bus [iplo].forward.input;  /*   otherwise the card is in link mode */
        io_ptrs [iplo].output = &dev_bus [iplo].forward.output; /*     so point at the card state */
        io_ptrs [iplo].output->cable_connected = FALSE;         /*       in the normal direction */
        }                                                       /*         and indicate that the card is not connected */
    }

return SCPE_OK;
}


/* Synchronize the simulator instance.

   This validation routine is entered with the "value" parameter set to zero to
   wait on the synchronization event or non-zero to signal the synchronization
   event.  The unit, character, and descriptor pointers are not used.

   This routine is called for the following commands:

     SET IPLI WAIT
     SET IPLI SIGNAL

   If the event object has not been created yet by attaching the IPL device, the
   routine returns "Command not allowed" status.  For either command, the
   routine returns "Command not completed" if the signal or wait function
   returned an error to indicate that the error code register should be checked.

   To permit the user to abort the wait command, a CTRL+C handler is installed,
   and waits of one second each are performed in a loop.  The handler will set
   the "wait_aborted" variable TRUE if it is called, and the loop then will
   terminate and return with success status.


   Implementation notes:

    1. The "wait_event" routine returns TRUE if the event is signaled and FALSE
       if it times out while waiting.
*/

static t_stat ipl_set_sync (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
const uint32 wait_time = 1000;                          /* the wait time in milliseconds */
t_bool signaled;

if (event_is_undefined (event_id))                      /* if the event has not been defined yet */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

if (value) {                                            /* if this is a SIGNAL command */
    event_error = signal_event (event_id);              /*   then signal the event */

    if (event_error == 0)                               /* if signaling succeeded */
        return SCPE_OK;                                 /*     then report command success */
    else                                                /*   otherwise */
        return SCPE_INCOMP;                             /*     report that the command did not complete */
    }

else {                                                  /* otherwise it's a WAIT command */
    wait_aborted = FALSE;                               /* clear the abort flag */
    signal (SIGINT, abort_handler);                     /*   and set up the CTRL+C handler */

    do
        event_error = wait_event (event_id, wait_time,      /* wait for the event */
                                  &signaled);               /*   to be signaled */
    while (! (event_error || wait_aborted || signaled));    /*     unless an error or user abort occurs */

    signal (SIGINT, SIG_DFL);                           /* restore the default CTRL+C handler */

    if (event_error == 0)                               /* if the wait function succeeded */
        return SCPE_OK;                                 /*   then report command success */
    else                                                /* otherwise */
        return SCPE_INCOMP;                             /*   report that the command did not complete */
    }
}


/* Handler for the CTRL+C signal.

   This handler is installed while waiting for a synchronization event.  It is
   called if the user presses CTRL+C to abort the wait command.
*/

static void abort_handler (int signal)
{
wait_aborted = TRUE;                                    /* the user has aborted the event wait */
return;
}


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
const CARD_INDEX card = (CARD_INDEX) (uptr == &iplo_unit);  /* set card selector */

tpprintf (dptrs [card], TRACE_PSERV, "Poll delay %u service scheduled\n",
          uptr->wait);

sim_activate (uptr, uptr->wait);                        /* activate the unit with the specified wait */

return;
}


/* Processor interconnect bootstrap loaders (special BBL and 12992K).

   The special Basic Binary Loader (BBL) used by the 2000 Access system loads
   absolute binary programs into memory from either the processor interconnect
   interface or the paper tape reader interface.  Two program entry points are
   provided.  Starting the loader at address x7700 loads from the processor
   interconnect, while starting at address x7750 loads from the paper tape
   reader.  The S register setting does not affect loader operation.

   For a 2100/14/15/16 CPU, entering a LOAD IPLI or BOOT IPLI command loads the
   special BBL into memory and executes the processor interconnect portion
   starting at x7700.  Loader execution ends with one of the following halt
   instructions:

     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 55 - the program load address would overlay the loader.
     * HLT 77 - the end of input with successful read; A = the paper tape select
                code, B = the processor interconnect select code.

   The 12992K boot loader ROM reads an absolute program from the processor
   interconnect or paper tape interfaces into memory.  The S register setting
   does not affect loader operation.  Loader execution ends with one of the
   following halt instructions:

     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 55 - the program load address would overlay the ROM loader.
     * HLT 77 - the end of tape was reached with a successful read.


   Implementation notes:

    1. After the BMDL has been loaded into memory, the paper tape portion may be
       executed manually by setting the P register to the starting address
       (x7750).

    2. For compatibility with the "cpu_copy_loader" routine, the BBL device I/O
       instructions address select code 10.

    3. For 2000B, C, and F versions that use dual CPUs, the I/O Processor is
       loaded with the standard BBL configured for the select codes of the
       processor interconnect interface.  2000 Access must use the special BBL
       because the paper tape reader is connected to the IOP in this version; in
       prior versions, it was connected to the System Processor and could use
       the paper-tape portion of the BMDL that was installed in the SP.
*/

static const LOADER_ARRAY ipl_loaders = {
    {                               /* HP 21xx 2000/Access special Basic Binary Loader */
      000,                          /*   loader starting index */
      IBL_NA,                       /*   DMA index */
      073,                          /*   FWA index */
      { 0163774,                    /*   77700:  PI    LDA 77774,I               Processor Interconnect start */
        0027751,                    /*   77701:        JMP 77751                 */
        0107700,                    /*   77702:  START CLC 0,C                   */
        0002702,                    /*   77703:        CLA,CCE,SZA               */
        0063772,                    /*   77704:        LDA 77772                 */
        0002307,                    /*   77705:        CCE,INA,SZA,RSS           */
        0027760,                    /*   77706:        JMP 77760                 */
        0017736,                    /*   77707:        JSB 77736                 */
        0007307,                    /*   77710:        CMB,CCE,INB,SZB,RSS       */
        0027705,                    /*   77711:        JMP 77705                 */
        0077770,                    /*   77712:        STB 77770                 */
        0017736,                    /*   77713:        JSB 77736                 */
        0017736,                    /*   77714:        JSB 77736                 */
        0074000,                    /*   77715:        STB 0                     */
        0077771,                    /*   77716:        STB 77771                 */
        0067771,                    /*   77717:        LDB 77771                 */
        0047773,                    /*   77720:        ADB 77773                 */
        0002040,                    /*   77721:        SEZ                       */
        0102055,                    /*   77722:        HLT 55                    */
        0017736,                    /*   77723:        JSB 77736                 */
        0040001,                    /*   77724:        ADA 1                     */
        0177771,                    /*   77725:        STB 77771,I               */
        0037771,                    /*   77726:        ISZ 77771                 */
        0000040,                    /*   77727:        CLE                       */
        0037770,                    /*   77730:        ISZ 77770                 */
        0027717,                    /*   77731:        JMP 77717                 */
        0017736,                    /*   77732:        JSB 77736                 */
        0054000,                    /*   77733:        CPB 0                     */
        0027704,                    /*   77734:        JMP 77704                 */
        0102011,                    /*   77735:        HLT 11                    */
        0000000,                    /*   77736:        NOP                       */
        0006600,                    /*   77737:        CLB,CME                   */
        0103700,                    /*   77740:        STC 0,C                   */
        0102300,                    /*   77741:        SFS 0                     */
        0027741,                    /*   77742:        JMP 77741                 */
        0106400,                    /*   77743:        MIB 0                     */
        0002041,                    /*   77744:        SEZ,RSS                   */
        0127736,                    /*   77745:        JMP 77736,I               */
        0005767,                    /*   77746:        BLF,CLE,BLF               */
        0027740,                    /*   77747:        JMP 77740                 */
        0163775,                    /*   77750:  PTAPE LDA 77775,I               Paper tape start */
        0043765,                    /*   77751:  CONFG ADA 77765                 */
        0073741,                    /*   77752:        STA 77741                 */
        0043766,                    /*   77753:        ADA 77766                 */
        0073740,                    /*   77754:        STA 77740                 */
        0043767,                    /*   77755:        ADA 77767                 */
        0073743,                    /*   77756:        STA 77743                 */
        0027702,                    /*   77757:  EOT   JMP 77702                 */
        0063777,                    /*   77760:        LDA 77777                 */
        0067776,                    /*   77761:        LDB 77776                 */
        0102077,                    /*   77762:        HLT 77                    */
        0027702,                    /*   77763:        JMP 77702                 */
        0000000,                    /*   77764:        NOP                       */
        0102300,                    /*   77765:        SFS 0                     */
        0001400,                    /*   77766:        OCT 1400                  */
        0002500,                    /*   77767:        OCT 2500                  */
        0000000,                    /*   77770:        OCT 0                     */
        0000000,                    /*   77771:        OCT 0                     */
        0177746,                    /*   77772:        DEC -26                   */
        0100100,                    /*   77773:        ABS -PI                   */
        0077776,                    /*   77774:        DEF *+2                   */
        0077777,                    /*   77775:        DEF *+2                   */
        0000010,                    /*   77776:  PISC  OCT 10                    */
        0000010 } },                /*   77777:  PTRSC OCT 10                    */

    {                               /* HP 1000 Loader ROM (12992K) */
      IBL_START,                    /*   loader starting index */
      IBL_DMA,                      /*   DMA index */
      IBL_FWA,                      /*   FWA index */
      { 0107700,                    /*   77700:  ST    CLC 0,C            ; intr off */
        0002401,                    /*   77701:        CLA,RSS            ; skip in */
        0063756,                    /*   77702:  CN    LDA M11            ; feed frame */
        0006700,                    /*   77703:        CLB,CCE            ; set E to rd byte */
        0017742,                    /*   77704:        JSB READ           ; get #char */
        0007306,                    /*   77705:        CMB,CCE,INB,SZB    ; 2's comp */
        0027713,                    /*   77706:        JMP *+5            ; non-zero byte */
        0002006,                    /*   77707:        INA,SZA            ; feed frame ctr */
        0027703,                    /*   77710:        JMP *-3            */
        0102077,                    /*   77711:        HLT 77B            ; stop */
        0027700,                    /*   77712:        JMP ST             ; next */
        0077754,                    /*   77713:        STA WC             ; word in rec */
        0017742,                    /*   77714:        JSB READ           ; get feed frame */
        0017742,                    /*   77715:        JSB READ           ; get address */
        0074000,                    /*   77716:        STB 0              ; init csum */
        0077755,                    /*   77717:        STB AD             ; save addr */
        0067755,                    /*   77720:  CK    LDB AD             ; check addr */
        0047777,                    /*   77721:        ADB MAXAD          ; below loader */
        0002040,                    /*   77722:        SEZ                ; E =0 => OK */
        0027740,                    /*   77723:        JMP H55            */
        0017742,                    /*   77724:        JSB READ           ; get word */
        0040001,                    /*   77725:        ADA 1              ; cont checksum */
        0177755,                    /*   77726:        STA AD,I           ; store word */
        0037755,                    /*   77727:        ISZ AD             */
        0000040,                    /*   77730:        CLE                ; force wd read */
        0037754,                    /*   77731:        ISZ WC             ; block done? */
        0027720,                    /*   77732:        JMP CK             ; no */
        0017742,                    /*   77733:        JSB READ           ; get checksum */
        0054000,                    /*   77734:        CPB 0              ; ok? */
        0027702,                    /*   77735:        JMP CN             ; next block */
        0102011,                    /*   77736:        HLT 11             ; bad csum */
        0027700,                    /*   77737:        JMP ST             ; next */
        0102055,                    /*   77740:  H55   HLT 55             ; bad address */
        0027700,                    /*   77741:        JMP ST             ; next */
        0000000,                    /*   77742:  RD    NOP                */
        0006600,                    /*   77743:        CLB,CME            ; E reg byte ptr */
        0103710,                    /*   77744:        STC RDR,C          ; start reader */
        0102310,                    /*   77745:        SFS RDR            ; wait */
        0027745,                    /*   77746:        JMP *-1            */
        0106410,                    /*   77747:        MIB RDR            ; get byte */
        0002041,                    /*   77750:        SEZ,RSS            ; E set? */
        0127742,                    /*   77751:        JMP RD,I           ; no, done */
        0005767,                    /*   77752:        BLF,CLE,BLF        ; shift byte */
        0027744,                    /*   77753:        JMP RD+2           ; again */
        0000000,                    /*   77754:  WC    000000             ; word count */
        0000000,                    /*   77755:  AD    000000             ; address */
        0177765,                    /*   77756:  M11   DEC -11            ; feed count */
        0000000,                    /*   77757:        NOP                */
        0000000,                    /*   77760:        NOP                */
        0000000,                    /*   77761:        NOP                */
        0000000,                    /*   77762:        NOP                */
        0000000,                    /*   77763:        NOP                */
        0000000,                    /*   77764:        NOP                */
        0000000,                    /*   77765:        NOP                */
        0000000,                    /*   77766:        NOP                */
        0000000,                    /*   77767:        NOP                */
        0000000,                    /*   77770:        NOP                */
        0000000,                    /*   77771:        NOP                */
        0000000,                    /*   77772:        NOP                */
        0000000,                    /*   77773:        NOP                */
        0000000,                    /*   77774:        NOP                */
        0000000,                    /*   77775:        NOP                */
        0000000,                    /*   77776:        NOP                */
        0100100 } }                 /*   77777:  MAXAD ABS -ST            ; max addr */
    };


/* Device boot routine.

   This routine is called directly by the BOOT IPLI and LOAD IPLI commands to
   copy the device bootstrap into the upper 64 words of the logical address
   space.  It is also called indirectly by a BOOT CPU or LOAD CPU command when
   the specified HP 1000 loader ROM socket contains a 12992K ROM.

   When called in response to a BOOT IPLI or LOAD IPLI command, the "unitno"
   parameter indicates the unit number specified in the BOOT command or is zero
   for the LOAD command, and "dptr" points at the IPLI device structure.
   Depending on the current CPU model, the special BBL or 12992K loader ROM will
   be copied into memory and configured for the IPLI select code.  If the CPU is
   a 1000, the S register will be set as it would be by the front-panel
   microcode.

   When called for a BOOT/LOAD CPU command, the "unitno" parameter indicates the
   select code to be used for configuration, and "dptr" will be NULL.  As above,
   the special BBL or 12992K loader ROM will be copied into memory and
   configured for the specified select code.  The S register is assumed to be
   set correctly on entry and is not modified.

   For the 12992K boot loader ROM, the S register will be set as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   0 |   IPLI select code    | 0   0   0   0   0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

static t_stat ipl_boot (int32 unitno, DEVICE *dptr)
{
static const HP_WORD ipl_ptx = 074u;                    /* the index of the pointer to the IPL select code */
static const HP_WORD ptr_ptx = 075u;                    /* the index of the pointer to the PTR select code */
static const HP_WORD ipl_scx = 076u;                    /* the index of the IPL select code */
static const HP_WORD ptr_scx = 077u;                    /* the index of the PTR select code */
uint32 start;
uint32 ptr_sc, ipl_sc;
DEVICE *ptr_dptr;

ptr_dptr = find_dev ("PTR");                            /* get a pointer to the paper tape reader device */

if (ptr_dptr == NULL)                                   /* if the paper tape device is not present */
    return SCPE_IERR;                                   /*   then something is seriously wrong */
else                                                    /* otherwise */
    ptr_sc = ((DIB *) ptr_dptr->ctxt)->select_code;     /*   get the select code from the device's DIB */

if (dptr == NULL)                                       /* if we are being called for a BOOT/LOAD CPU command */
    ipl_sc = (uint32) unitno;                           /*   then get the select code from the "unitno" parameter */
else                                                    /* otherwise */
    ipl_sc = ipli_dib.select_code;                      /*   use the device select code from the DIB */

start = cpu_copy_loader (ipl_loaders, ipl_sc,           /* copy the boot loader to memory */
                         IBL_S_NOCLEAR, IBL_S_NOSET);   /*   but do not alter the S register */

if (start == 0)                                         /* if the copy failed */
    return SCPE_NOFNC;                                  /*   then reject the command */

else {                                                                  /* otherwise */
    if (mem_examine (start + ptr_scx) <= SC_MAX) {                      /*   if this is the special BBL */
        mem_deposit (start + ipl_ptx, (HP_WORD) start + ipl_scx);       /*     then configure */
        mem_deposit (start + ptr_ptx, (HP_WORD) start + ptr_scx);       /*       the pointers */
        mem_deposit (start + ipl_scx, (HP_WORD) ipli_dib.select_code);  /*         and select codes */
        mem_deposit (start + ptr_scx, (HP_WORD) ptr_sc);                /*           for the loader */
        }

    return SCPE_OK;                                     /* the boot loader was successfully copied */
    }
}



/* Process synchronization functions */



#if defined (_WIN32)

/* Windows process synchronization */


/* Create a synchronization event.

   This routine creates a synchronization event using the supplied name and
   returns the event handle to the caller.  If creation succeeds, the routine
   returns 0.  Otherwise, the error value is returned.

   The event is created with these attributes: no security, automatic reset, and
   initially non-signaled.
*/

static uint32 create_event (const char *name, EVENT *event)
{
*event = CreateEvent (NULL, FALSE, FALSE, name);      /* create an auto-reset, initially not-signaled event */

tprintf (ipli_dev, TRACE_CMD, "Created event %p with identifier \"%s\"\n",
         (void *) *event, name);

if (*event == NULL)                                     /* if event creation failed */
    return (uint32) GetLastError ();                    /*   then return the error code */
else                                                    /* otherwise the creation succeeded */
    return 0;                                           /*   so return success */
}


/* Destroy a synchronization event.

   This routine destroys the synchronization event specified by the supplied
   event handle.  If destruction succeeds, the event handle is invalidated, and
   the routine returns 0.  Otherwise, the error value is returned.

   The event name parameter is not used but is present for interoperability.
*/

static uint32 destroy_event (const char *name, EVENT *event)
{
BOOL status;

if (*event == NULL)                                     /* if the event does not exist */
    return 0;                                           /*   then indicate that it has already been destroyed */

else {                                                  /* otherwise the event exists */
    status = CloseHandle (*event);                      /*   so close it */
    *event = NULL;                                      /*     and clear the event handle */

    if (status == FALSE)                                /* if the close failed */
        return (uint32) GetLastError ();                /*   then return the error code */
    else                                                /* otherwise the close succeeded */
        return 0;                                       /*   so return success */
    }
}


/* Test if the synchronization event exists.

   This routine returns TRUE if the supplied event handle does not exist and
   FALSE if the handle refers to a defined event.
*/

static t_bool event_is_undefined (EVENT event)
{
return (event == NULL);                                 /* return TRUE if the event does not exist */
}


/* Wait for a synchronization event.

   This routine waits for a synchronization event to be signaled or for the
   supplied maximum wait time to elapse.  If the event identified by the
   supplied handle is signaled, the routine returns 0 and sets the "signaled"
   flag to TRUE.  If the timeout expires without the event being signaled, the
   routine returns 0 with the "signaled" flag set to FALSE.  If the event wait
   fails, the routine returns the error value.


   Implementation notes:

    1. The maximum wait time may be zero to test the signaled state and return
       immediately, or may be set to "INFINITE" to wait forever.  The latter is
       not recommended, as it provides the user with no means to cancel the wait
       and return to the SCP prompt.
*/

static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
const DWORD wait_time = (DWORD) wait_in_ms;             /* interval wait time in milliseconds */
DWORD status;

status = WaitForSingleObject (event, wait_time);        /* wait for the event, but not forever */

tprintf (ipli_dev, TRACE_CMD, "Wait status is %lu\n", status);

if (status == WAIT_FAILED)                              /* if the wait failed */
    return (uint32) GetLastError ();                    /*   then return the error code */

else {                                                  /* otherwise the wait completed */
    *signaled = (status != WAIT_TIMEOUT);               /*   so set the flag TRUE if the wait did not time out */
    return 0;                                           /*     and return success */
    }
}


/* Signal the synchronization event.

   This routine signals a the synchronization event specified by the supplied
   event handle.  If signaling succeeds, the routine returns 0.  Otherwise, the
   error value is returned.
*/

static uint32 signal_event (EVENT event)
{
BOOL status;

status = SetEvent (event);                              /* signal the event */

if (status == FALSE)                                    /* if the call failed */
    return (uint32) GetLastError ();                    /*   then return the error code */
else                                                    /* otherwise the signal succeeded */
    return 0;                                           /*   so return success */
}



#elif defined (HAVE_SEMAPHORE)

/* UNIX process synchronization */


/* Create the synchronization event.

   This routine creates a synchronization event using the supplied name and
   returns an event object  to the caller.  If creation succeeds, the routine
   returns 0.  Otherwise, the error value is returned.

   Systems that define the semaphore functions but implement them as stubs will
   return ENOSYS.  We handle this case by enabling fallback to the unimplemented
   behavior, i.e., emulating a process wait by a two-second sleep.  All other
   error returns are reported back to the caller.

   Regarding the choice of event name, the Single Unix Standard says:

     If [the] name begins with the <slash> character, then processes calling
     sem_open() with the same value of name shall refer to the same semaphore
     object, as long as that name has not been removed.  If name does not begin
     with the <slash> character, the effect is implementation-defined.

   Therefore, event names passed to this routine should begin with a slash
   character.

   The event is created as initially not-signaled.
*/

static uint32 create_event (const char *name, EVENT *event)
{
*event = sem_open (name, O_CREAT, S_IRWXU, 0);          /* create an initially not-signaled event */

if (*event == SEM_FAILED)                               /* if event creation failed */
    if (errno == ENOSYS) {                              /*   then if the function is not implemented */
        tprintf (ipli_dev, TRACE_CMD, "sem_open is unsupported on this system; using fallback\n");

        event_fallback = TRUE;                          /*     then fall back to event emulation */
        return 0;                                       /*       and claim that the open succeeded */
        }

    else {                                              /*   otherwise it is an unexpected error */
        tprintf (ipli_dev, TRACE_CMD, "sem_open error is %u\n", errno);
        return (uint32) errno;                          /*     so return the error code */
        }

else {                                                  /* otherwise the creation succeeded */
    tprintf (ipli_dev, TRACE_CMD, "Created event %p with identifier \"%s\"\n",
             (void *) *event, name);
    return 0;                                           /*   so return success */
    }
}


/* Destroy the synchronization event.

   This routine destroys the synchronization event specified by the supplied
   event name.  If destruction succeeds, the event object is invalidated, and
   the routine returns 0.  Otherwise, the error value is returned.


   Implementation notes:

    1. If the other simulator instance destroys the event first, our
       "sem_unlink" call will fail with ENOENT.  This is an expected error, and
       the routine returns success in this case.
*/

static uint32 destroy_event (const char *name, EVENT *event)
{
int status;

if (*event == SEM_FAILED)                               /* if the event does not exist */
    return 0;                                           /*   then indicate that it has already been deleted */

else {                                                  /* otherwise the event exists */
    status = sem_unlink (name);                         /*   so delete it */
    *event = SEM_FAILED;                                /*     and clear the event handle */

    if (status != 0 && errno != ENOENT) {               /* if the deletion failed */
        tprintf (ipli_dev, TRACE_CMD, "sem_unlink error is %u\n", errno);
        return (uint32) errno;                          /*   then return the error code */
        }

    else                                                /* otherwise the deletion succeeded */
        return 0;                                       /*   so return success */
    }
}


/* Test if the synchronization event exists.

   This routine returns TRUE if the supplied event object does not exist and
   FALSE if the object refers to a defined event.
*/

static t_bool event_is_undefined (EVENT event)
{
if (event_fallback)                                     /* if events are being emulated */
    return FALSE;                                       /*   then claim that the event is defined */
else                                                    /* otherwise */
    return (event == SEM_FAILED);                       /*   return TRUE if the event does not exist */
}


/* Wait for the synchronization event.

   This routine waits for a synchronization event to be signaled or for the
   supplied maximum wait time to elapse.  If the event identified by the
   supplied event object is signaled, the routine returns 0 and sets the
   "signaled" flag to TRUE.  If the timeout expires without the event being
   signaled, the routine returns 0 with the "signaled" flag set to FALSE.  If
   the event wait fails, the routine returns the error value.


   Implementation notes:

    1. The maximum wait time may be zero to test the signaled state and return
       immediately, or may be set to a large value to wait forever.  The latter
       is not recommended, as it provides the user with no means to cancel the
       wait and return to the SCP prompt.
*/

static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
const int wait_time = (int) wait_in_ms / 1000;          /* interval wait time in seconds */
struct timespec until_time;
int    status;

if (event_fallback) {                                   /* if events are being emulated */
    sim_os_sleep (2);                                   /*   then wait for two seconds */

    *signaled = TRUE;                                   /* indicate a signaled completion */
    return 0;                                           /*   and return success */
    }

else if (clock_gettime (CLOCK_REALTIME, &until_time)) { /* get the current time; if it failed */
    tprintf (ipli_dev, TRACE_CMD, "clock_gettime error is %u\n", errno);
    return (uint32) errno;                              /*   then return the error number */
    }

else                                                    /* otherwise */
    until_time.tv_sec = until_time.tv_sec + wait_time;  /*   set the (absolute) timeout expiration */

status = sem_timedwait (event, &until_time);            /* wait for the event, but not forever */

*signaled = (status == 0);                              /* set the flag TRUE if the wait did not time out */

if (status)                                             /* if the wait terminated */
    if (errno == ETIMEDOUT || errno == EINTR)           /*   then if it timed out or was manually aborted */
        return 0;                                       /*     then return success */

    else {                                              /*   otherwise it's an unexpected error */
        tprintf (ipli_dev, TRACE_CMD, "sem_timedwait error is %u\n", errno);
        return (uint32) errno;                          /*     so return the error code */
        }

else                                                    /* otherwise the event is signaled */
    return 0;                                           /*   so return success */
}


/* Signal the synchronization event.

   This routine signals a the synchronization event specified by the supplied
   event handle.  If signaling succeeds, the routine returns 0.  Otherwise, the
   error value is returned.
*/

static uint32 signal_event (EVENT event)
{
int status;

if (event_fallback)                                     /* if events are being emulated */
    return 0;                                           /*   then claim that the event was signaled */
else                                                    /* otherwise */
    status = sem_post (event);                          /*   signal the event */

if (status) {                                           /* if the call failed */
    tprintf (ipli_dev, TRACE_CMD, "sem_post error is %u\n", errno);
    return (uint32) errno;                              /*   then return the error code */
    }

else                                                    /* otherwise the event was signaled */
    return 0;                                           /*   so return success */
}



#else

/* Process synchronization stubs.

   The stubs return success, rather than failure, because we want the callers to
   continue as though synchronization is occurring, even though the host system
   does not support the operations necessary to implement this.  Without host
   system synchronization support, the simulated system's OS might work, but if
   the routines returned failure, then the simulator command files running on
   such systems would refuse to run.


   Implementation notes:

    1. We provide an event wait by sleeping for a few seconds to give the other
       simulator instance a chance to catch up.  This has a reasonable chance of
       working, provided the other instance isn't preempted during the sleep.
*/

static uint32 create_event (const char *name, EVENT *event)
{
tprintf (ipli_dev, TRACE_CMD, "Synchronization is unsupported on this system; using fallback\n");
return 0;
}


static uint32 destroy_event (const char *name, EVENT *event)
{
return 0;
}


static t_bool event_is_undefined (EVENT event)
{
return FALSE;
}


static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
sim_os_sleep (2);                                       /* wait for two seconds */

*signaled = TRUE;                                       /* then indicate a signaled completion */
return 0;                                               /*   and return success */
}


static uint32 signal_event (EVENT event)
{
return 0;
}


#endif
