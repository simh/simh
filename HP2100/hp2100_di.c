/* hp2100_di.c: HP 12821A HP-IB Disc Interface simulator

   Copyright (c) 2010-2018, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   DI           12821A Disc Interface

   11-Jul-18    JDB     Revised I/O model
   15-Mar-17    JDB     Converted debug fprintfs to tpprintfs
   10-Mar-17    JDB     Added IOBUS to the debug table
   17-Jan-17    JDB     Changed to use new byte accessors in hp2100_defs.h
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
                        Removed redundant global declarations
   13-Feb-12    JDB     First release
   15-Dec-11    JDB     Added dummy DC device for diagnostics
   09-Oct-10    JDB     Created DI simulation

   References:
     - HP 12821A Disc Interface Installation and Service Manual
         (12821-90006, February 1985)
     - IEEE Standard Digital Interface for Programmable Instrumentation
         (IEEE-488A-1980, September 1979)


   The 12821A was a high-speed implementation of the Hewlett-Packard Interface
   Bus (HP-IB, formalized as IEEE Std. 488-1978).  It was used to interface
   HP-IB disc and tape devices, such as the HP 7906H, 7908A, and 7974A, to the
   HP 1000 running RTE-IVB or RTE-6/VM.  Three device command protocols were
   supported by the I/O drivers: Amigo discs by driver DVA32, CS/80 discs by
   DVM33, and Amigo tapes by DVS23.

   In an RTE environment, the 12821A was the system controller.  While
   electrically compatible with the HP-IB specification and capable of receiving
   addressing commands from the bus, the 12821A did not use the full IEEE-488
   protocol.  Card talker and listener states were set by bits in the control
   register, rather than by receiving talk and listen commands over the bus.
   The bus address of the card could be set via DIP switches, but this feature
   was only used by the diagnostic.

   The card supported packed and unpacked transfers across the bus.  Up to four
   devices could be connected to each card; this limit was imposed by the
   maximum electrical loading on the bus compatible with the high data rate.

   The 12821A had a 16-word FIFO buffer and could sustain DCPC transfers of one
   megabyte per second.  Burst transfers by the CPU to fill or empty the FIFO
   could run at the full bandwidth of the I/O backplane.  This could hold off
   lower-priority devices for 10-15 microseconds until the card slowed down to
   the rate of the disc or tape.

   Card assembly 12821-60003 was revised to add a DCPC pacing option.  Placing
   jumper W1 in position A inhibited SRQ for one I/O cycle in six to allow a
   lower-priority interface card to transfer one word.  Position B allowed SRQ
   to assert continuously as it did on the earlier card assembly 12821-60001.

   The simulator is logically partitioned into three sets of functions: the
   interface card simulation, the HP-IB bus simulation, and the device
   simulation.  This is the card simulation and the card portion of the HP-IB
   simulation.  Separate modules for the tape and disc devices contain the
   device simulations and the device portions of the HP-IB simulations.

   This simulator is written to allow the definition of multiple DI cards in a
   system.  The RTE operating system provided separate I/O drivers for the Amigo
   disc, Amigo tape, and CS/80 disc devices.  As only one I/O driver could
   control a given interface, separate interfaces were required if more than one
   device class was installed.  For example, it was not possible to control an
   Amigo disc and an Amigo tape connected to the same interface card.


   Implementation notes:

    1. The simulator behaves as though card switches S1-S7 are initially closed,
       providing a card bus address of 0.  The address may be changed with the
       SET <dev> ADDRESS=n command.  Only addresses 0-7 are supported, and the
       address may duplicate a device bus address without conflict, as the
       address is only used during the diagnostic when devices are disconnected.

    2. The simulator behaves as though card switch S8 is open, enabling the card
       to be the system controller.  This cannot be changed by the user.

    3. The simulator behaves as though card jumper W1 (DCPC pacing) is in
       position B.  This currently cannot be changed by the user.
*/



#include "hp2100_defs.h"
#include "hp2100_io.h"
#include "hp2100_di.h"



/* Program constants */

#define SW8_SYSCTL      1                       /* card is always the system controller (switch 8) */

#define IFC_TIMEOUT     157                     /* 157 instructions = ~ 100 microseconds */

#define CONTROLLER      31                      /* dummy unit number for DI */


/* Character constants */

#define LF              '\012'


/* Control Word Register */

#define CNTL_SRQ        0100000                 /* enable service request interrupt */
#define CNTL_IFC        0040000                 /* assert IFC or enable IFC interrupt */
#define CNTL_REN        0020000                 /* assert remote enable */
#define CNTL_IRL        0010000                 /* enable input-register-loaded interrupt */
#define CNTL_LBO        0004000                 /* enable last-byte-out interrupt */
#define CNTL_LF         0002000                 /* enable line feed terminator */
#define CNTL_EOI        0001000                 /* assert end or identify */
#define CNTL_ATN        0000400                 /* assert attention */
#define CNTL_DIAG       0000200                 /* diagnostic loopback */
#define CNTL_NRFD       0000100                 /* assert not ready for data */
#define CNTL_PPE        0000040                 /* parallel poll enable */
#define CNTL_ODD        0000020                 /* odd number of bytes */
#define CNTL_PACK       0000010                 /* packed data transfer */
#define CNTL_LSTN       0000004                 /* listen */
#define CNTL_TALK       0000002                 /* talk */
#define CNTL_CIC        0000001                 /* controller in charge */


/* Status Word Register */

#define STAT_SRQBUS     0100000                 /* service request bus state */
#define STAT_IFCBUS     0040000                 /* interface clear bus state */
#define STAT_RENBUS     0020000                 /* remote enable bus state */
#define STAT_IRL        0010000                 /* input register loaded */
#define STAT_LBO        0004000                 /* last byte out */
#define STAT_LBI        0002000                 /* last byte in */
#define STAT_EOIBUS     0001000                 /* end or identify bus state */
#define STAT_ATNBUS     0000400                 /* attention bus state */
#define STAT_IFC        0000200                 /* interface clear seen */
#define STAT_ODD        0000020                 /* odd number of bytes */
#define STAT_SYSCTL     0000010                 /* system controller */
#define STAT_LSTN       0000004                 /* listener */
#define STAT_TALK       0000002                 /* talker */
#define STAT_CIC        0000001                 /* controller in charge */


/* Data word */

#define DATA_LBO        0100000                 /* last byte out */
#define DATA_EOI        0001000                 /* end or identify */
#define DATA_ATN        0000400                 /* attention */


/* Tag word */

#define BUS_SHIFT       16                      /* left shift count to align BUS_ATN, EOI with tag */
#define DATA_SHIFT       8                      /* left shift count to align DATA_ATN, EOI with tag */

#define TAG_ATN         0000200000              /* bit 16: attention */
#define TAG_EOI         0000400000              /* bit 17: end or identify */
#define TAG_EDT         0001000000              /* bit 18: end of data transfer */
#define TAG_LBR         0002000000              /* bit 19: last byte received */

#define TAG_MASK        (TAG_ATN | TAG_EOI | TAG_EDT | TAG_LBR)

static const BITSET_NAME tag_names [] = {       /* Bus signal names */
    "ATN",                                      /*   bit 16 */
    "EOI",                                      /*   bit 17 */
    "EDT",                                      /*   bit 18 */
    "LBR"                                       /*   bit 19 */
    };

static const BITSET_FORMAT tag_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (tag_names, 16, lsb_first, no_alt, no_bar) };


/* Bus signals  */

static const BITSET_NAME bus_names [] = {       /* Bus signal names */
    "ATN",                                      /*   bit  0 = attention */
    "EOI",                                      /*   bit  1 = end or identify */
    "DAV",                                      /*   bit  2 = data available */
    "NRFD",                                     /*   bit  3 = not ready for data */
    "NDAC",                                     /*   bit  4 = not data accepted */
    "REN",                                      /*   bit  5 = remote enable */
    "IFC",                                      /*   bit  6 = interface clear */
    "SRQ"                                       /*   bit  7 = service request */
    };

static const BITSET_FORMAT bus_format =         /* names, offset, direction, alternates, bar */
    { FMT_INIT (bus_names, 0, lsb_first, no_alt, no_bar) };


/* FIFO access modes */

#define FIFO_EMPTY      (di_card->fifo_count == 0)          /* FIFO empty test */
#define FIFO_FULL       (di_card->fifo_count == FIFO_SIZE)  /* FIFO full test */

typedef enum {
    bus_access,                                 /* per-byte access */
    cpu_access,                                 /* per-word access */
    diag_access                                 /* mixed access */
    } FIFO_ACCESS;


/* Disc interface global state variables */

DI_STATE di [card_count];                       /* per-card state */


/* Disc interface local bus routines */

static t_bool di_bus_accept  (CARD_ID card, uint8 data);
static void   di_bus_respond (CARD_ID card, uint8 cntl);
static void   di_bus_poll    (CARD_ID card);

/* Disc interface local utility routines */

static void   master_reset (CARD_ID card);
static void   update_state (CARD_ID card);
static void   fifo_load    (CARD_ID card, uint16 data,  FIFO_ACCESS access);
static uint16 fifo_unload  (CARD_ID card, FIFO_ACCESS access);



/* Dummy DC device.

   This temporary dummy device allows the DI diagnostic to test inter-card
   signals.  Test 15 can only be performed if there are two DIs available.

   This device provides a second "bare" card.  Normally, it is disabled and
   cannot be enabled by the user.  Enabling or disabling DIAG mode on the DA
   device automatically enables or disables the DC device.  The select code of
   the DC device is fixed at 45B and cannot be changed.
*/

static DIB dc_dib = {
    &di_interface,                              /* the device's I/O interface function pointer */
    DI_DC,                                      /* the device's select code (02-77) */
    dc,                                         /* the card index */
    NULL,                                       /* the card description */
    NULL                                        /* the ROM description */
    };

static REG dc_reg [] = {
    { BRDATA (FIFO, di [dc].fifo, 8, 20, FIFO_SIZE), REG_CIRC },    /* needed for "qptr" */
    { NULL }
    };

DEVICE dc_dev = {
    "DC",                                       /* device name */
    NULL,                                       /* unit array */
    dc_reg,                                     /* register array */
    NULL,                                       /* modifier array */
    0,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &di_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &dc_dib,                                    /* device information block */
    DEV_DIS | DEV_DEBUG,                        /* device flags */
    0,                                          /* debug control flags */
    di_deb,                                     /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* DI data structures.

   *dptrs           device pointers
   *bus_accept      device acceptor function pointers
   *bus_respond     device responder function pointers

   di_deb           DI debug table

   The first three pointer arrays have elements that correspond one-for-one with
   the supported devices.  These allow the DI simulator to work with multiple
   cards.  The actual devices are defined in the individual device simulators.

   Note that the DC and MA devices are reserved for future use.  Until one or
   the other is fully implemented, a dummy DC device is provided above for use
   by the diagnostic only.
*/

extern DEVICE da_dev;

static DEVICE    *dptrs       [card_count] = { &da_dev,         &dc_dev, NULL };
static ACCEPTOR  *bus_accept  [card_count] = { &da_bus_accept,  NULL,    NULL };
static RESPONDER *bus_respond [card_count] = { &da_bus_respond, NULL,    NULL };


/* Global trace list */

DEBTAB di_deb [] = {
    { "RWSC",  DEB_RWSC    },
    { "CMDS",  DEB_CMDS    },
    { "CPU",   DEB_CPU     },
    { "BUF",   DEB_BUF     },
    { "XFER",  DEB_XFER    },
    { "SERV",  DEB_SERV    },
    { "IOBUS", TRACE_IOBUS },
    { NULL,    0           }
    };



/* Disc interface global VM routines */


/* Disc interface.

   The card has two input and two output registers.  The Input Data Register and
   Output Data Register are addressed when the control flip-flop is set.  The
   Status Word and the Control Word Register are addressed when the control
   flip-flop is clear.  The card has the usual control, flag buffer, flag, and
   SRQ flip-flops, though flag and SRQ are decoupled to allow the full DCPC
   transfer rate.

   In hardware, the presence of the card FIFO, which is necessary to obtain full
   DCPC bandwidth, implies a delay between CPU actions, such as outputting the
   last word in a data transfer, and device actions, such as accepting the last
   word of a disc write.  Four flip-flops are used to monitor FIFO status:

     - EDT (End of Data Transfer)
     - LBO (Last Byte Out)
     - LBI (Last Byte In)
     - EOR (End of Record)

   The EDT signal indicates that the final data word of a transfer is being
   written to the FIFO.  The flip-flop is set by the EDT backplane signal when
   the last cycle of a DCPC transfer is executing, or during programmed output
   transfers when CLF does not accompany IOO in packed mode, or when bit 15 of
   the data word is set in unpacked mode.  It remains set until it is cleared by
   a master reset.  The output of the EDT flip-flop drives the EDT tag input of
   the FIFO.

   The LBO signal indicates that the final data byte of a transfer has been
   sourced to the bus.  The flip-flop is set when the last byte of the entry
   tagged with EDT has been unloaded from the FIFO.  It is cleared by a master
   reset or when an entry not tagged with EDT is unloaded.  The output of the
   LBO flip-flop drives the LBO bit in the Status Word.

   The LBI signal indicates that the final byte of an input transfer has been
   accepted from the bus.  The flip-flop is set when a byte tagged with EOI is
   received and the EOI bit in the control register is set, or a line-feed byte
   is received and the LF bit in the control register is set.  It is cleared by
   a master reset or when neither of these conditions is true.  The input of the
   LBI flip-flop also drives the LBR (last byte received) tag input of the FIFO,
   and the output of the flip-flop drives the LBI bit in the Status Word.

   The EOR signal indicates that the final data word of a transfer is available
   in the Input Data Register.  The flip-flop is set when the last byte of the
   entry tagged with LBR has been unloaded from the FIFO and written to the IDR.
   It is cleared by a master reset or when an entry not tagged with LBR is
   unloaded and written to the IDR.  The output of the EOR flip-flop sets the
   flag flip-flop when the IDR is unloaded.


   Implementation notes:

    1. In hardware, the Status Word consists of individual flip-flops and status
       signals that are enabled onto the I/O backplane.  In simulation, the
       individual status values are collected into a Status Word Register, and
       the Output Data Register does not exist (output data is written directly
       to the FIFO buffer).

    2. The DIAG, T, and L control bits enable a data loopback path on the card.
       An IOO issued to the card unloads a word from the FIFO and then loads the
       lower byte back into both bytes of the FIFO.  The data word output with
       the IOO instruction is not used.

       In hardware, IOO triggers the FIFO unload and reload; T and L are
       required only for the loopback path.  If L is not asserted, then the FIFO
       is loaded with 177777 due to the floating bus.  If L is asserted and T is
       not, then the FIFO is loaded with 000000 due to pullups on the DIO lines.
       In simulation, we look only for DIAG and assume that T/L are set
       properly, i.e., unloaded data is reloaded.

    3. In hardware, the SRQ and NRFD lines are open-collector and may be driven
       simultaneously from several bus devices.  Simulating this fully would
       require keeping the state of the lines for each device and deriving the
       common bus signals from the logical OR of the state values.  Fortunately,
       some simplifications are possible.

       The DI asserts SRQ only if control word bit 15 is 1 and bit 0 is 0.
       Other bit combinations deny SRQ; as neither the Amigo nor CS/80 protocols
       use SRQ and serial polls, there will be no other driver.

       In hardware, every listener drives NRFD, but in practice there is only
       one listener at a time.  When the card is the listener, it asserts NRFD
       if the FIFO becomes full.  In simulation, we assert NRFD on the bus if
       NRFD is set in the control register, or we are listening and the FIFO is
       full.  We deny NRFD if NRFD had been set in the control register but is
       no longer, or if we had been a listener but are no longer.  That is, we
       assume that if we have forced NRFD or set it as a listener, then no one
       else will be asserting NRFD, so it's safe for us to deny NRFD when the
       override is removed or we are no longer a listener.

       We also deny NRFD when a CRS is issued if NRFD had been explicitly
       requested or the card had been listening.  The rationale is the same:
       only a listener can assert NRFD, so if we were listening, it's safe to
       deny it, because only we could have set it.

    4. In hardware, the IRL, LBO, LBI, and IFC status bits are driven by
       corresponding flip-flops.  In simulation, the status bits themselves hold
       the equivalent states and are set and cleared as indicated.

    5. The card state must be updated during status read (IOI) processing
       because the 7974 boot ROM watches the IFC line to determine when IFC
       assertion ends.

    6. DCPC performance is optimized by recognizing that the normal cases (an
       input that empties the FIFO or an output that fills the FIFO) do not
       alter the card state, and so the usual update_state call may be omitted.

    7. The gcc compiler (at least as of version 4.6.2) does not optimize
       repeated use of array-of-structures accesses.  Instead, it recalculates
       the index each time, even though the index is a constant within the
       function.  To avoid this performance penalty, we use a pointer to the
       selected DI_STATE structure.  Note that VC++ 2008 does perform this
       optimization.
*/

SIGNALS_VALUE di_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
static const char * const output_state [] = { "Control", "Data" };
static const char * const input_state  [] = { "Status",  "Data" };

const char * const hold_or_clear = (inbound_signals & ioCLF ? ",C" : "");
const CARD_ID card = (CARD_ID) (dibptr->card_index);
DI_STATE * const di_card = &di [card];

uint8          assert, deny;                            /* new bus control states */
t_bool         update_required = TRUE;                  /* TRUE if CLF must update the card state */

INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            di_card->flag_buffer = CLEAR;               /* clear the flag buffer */
            di_card->flag        = CLEAR;               /*   and flag flip-flops */

            tpprintf (dptrs [card], DEB_CMDS, "[CLF] Flag cleared\n");

            if (update_required)                            /* if the card state has changed */
                update_state (card);                        /*   then update the state */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            di_card->flag_buffer = SET;                 /* set the flag buffer flip-flop */

            tpprintf (dptrs [card], DEB_CMDS, "[STF] Flag set\n");
            break;


        case ioENF:                                     /* Enable Flag */
            if (di_card->flag_buffer == SET)            /* if the flag buffer flip-flop is set */
                di_card->flag = SET;                    /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (di_card->flag == CLEAR)                 /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (di_card->flag == SET)                   /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                             /* I/O data input */
            if (di_card->control == SET) {                      /* is the card in data mode? */
                outbound.value = di_card->input_data_register;  /* read the input data register */
                di_card->status_register &= ~STAT_IRL;          /* clear the input register loaded status */

                if (FIFO_EMPTY && di_card->eor == CLEAR) {  /* is the FIFO empty and end of record not seen? */
                    if (di_card->srq == SET)
                        tpprintf (dptrs [card], DEB_CMDS, "SRQ cleared\n");

                    di_card->srq = CLEAR;                   /* clear SRQ */
                    update_required = FALSE;                /* the card state does not change */
                    }
                }

            else {                                          /* the card is in status mode */
                di_card->status_register &=                 /* clear the values to be computed, */
                    STAT_IRL | STAT_LBO                     /*   preserving those set elsewhere */
                  | STAT_LBI | STAT_IFC;

                di_card->status_register |=                 /* set T/L/C status from control register */
                  di_card->cntl_register                    /* (T/L are ORed, as MTA or MLA can also set) */
                  & (CNTL_CIC | CNTL_TALK | CNTL_LSTN);


                if (SW8_SYSCTL)                                 /* if SW8 is set, */
                    di_card->status_register |= STAT_SYSCTL;    /*   the card is the system controller */

                if (di_card->ibp == lower)                  /* if lower byte input is next */
                    di_card->status_register |= STAT_ODD;   /*   then the last transfer was odd */

                di_card->status_register |=                 /* set the bus status bits */
                  (di_card->bus_cntl                        /*   from the corresponding bus control lines */
                  & (BUS_SRQ | BUS_IFC | BUS_REN
                    | BUS_EOI | BUS_ATN)) << DATA_SHIFT;

                outbound.value = di_card->status_register;  /* return the status word */
                }

            tpprintf (dptrs [card], DEB_CPU, "[LIx%s] %s = %06o\n",
                      hold_or_clear, input_state [di_card->control], outbound.value);

            if (update_required && !(inbound_signals & ioCLF))  /* if an update is required and CLF is not present, */
                update_state (card);                            /*   then update the state, else ioCLF will update it */
            break;


        case ioIOO:                                         /* I/O data output */
            tpprintf (dptrs [card], DEB_CPU, "[OTx%s] %s = %06o\n",
                      hold_or_clear, output_state [di_card->control], inbound_value);

            if (di_card->control == SET) {                      /* is the card in data mode? */
                if (inbound_signals & ioEDT)                    /* if end of DCPC transfer */
                    di_card->edt = SET;                         /*   set the EDT flip-flop */

                else if (di_card->cntl_register & CNTL_PACK) {  /* is this a packed transfer? */
                    if (!(inbound_signals & ioCLF))             /*   and CLF not given? */
                        di_card->edt = SET;                     /* set the EDT flip-flop */
                    }

                else                                            /* it's an unpacked transfer */
                    if (inbound_value & DATA_LBO)               /* is the last byte out? */
                        di_card->edt = SET;                     /* set the EDT flip-flop */

                if (di_card->cntl_register & CNTL_DIAG) {                   /* set for DIAG loopback? */
                    inbound_value = fifo_unload (card, diag_access);        /* unload data from the FIFO */
                    fifo_load (card, (uint16) inbound_value, diag_access);  /*   and load it back in */
                    }

                else {                                                      /* the card is set for normal operation */
                    fifo_load (card, (uint16) inbound_value, cpu_access);   /* load the data word into the FIFO */

                    if (FIFO_FULL && (di_card->bus_cntl & BUS_NRFD)) {  /* FIFO full and listener not ready? */
                        if (di_card->srq == SET)
                            tpprintf (dptrs [card], DEB_CMDS, "SRQ cleared\n");

                        di_card->srq = CLEAR;                           /* clear SRQ */
                        update_required = FALSE;                        /* the card state does not change */
                        }
                    }
                }

            else {                                          /* the card is in control mode */
                assert = 0;                                 /* initialize bus control assertions */
                deny = 0;                                   /*   and denials */

                if (!(inbound_value & CNTL_PACK))           /* unpacked mode always sets */
                    di_card->ibp = di_card->obp = lower;    /*   byte selectors to the lower byte */

                if (inbound_value & CNTL_TALK) {                /* talking enables ATN and EOI outputs */
                    if ((inbound_value & (CNTL_PPE | CNTL_CIC)) /* if parallel poll is enabled */
                      == (CNTL_PPE | CNTL_CIC))                 /*   and the card is CIC */
                        assert = BUS_PPOLL;                     /*   then conduct a parallel poll */

                    else if ((di_card->cntl_register        /* if PP was enabled */
                      & (CNTL_PPE | CNTL_CIC))              /*   but is not now */
                      == (CNTL_PPE | CNTL_CIC))
                        deny = BUS_PPOLL;                   /*     then end the parallel poll */

                    else if ((inbound_value                 /* if packed mode */
                      & (CNTL_PACK | CNTL_CIC | CNTL_ATN))  /*   and the card is CIC */
                      == (CNTL_PACK | CNTL_CIC | CNTL_ATN)) /* then the ATN control output */
                        assert = BUS_ATN;                   /*   is coupled to the bus */

                    else                                    /* if none of the above */
                        deny = BUS_ATN;                     /*   then ATN is not driven */
                    }

                else                                        /* the card is not talking */
                    deny = BUS_ATN | BUS_EOI;               /*   so ATN and EOI are disabled */


                if (inbound_value & CNTL_NRFD)                  /* is card not ready set explicitly? */
                    assert |= BUS_NRFD;                         /* assert NRFD on the bus */

                else if (di_card->cntl_register & CNTL_NRFD)    /* NRFD was set but is not now? */
                    deny |= BUS_NRFD;                           /* deny NRFD on the bus */

                if (FIFO_FULL)                                  /* is the FIFO full? */
                    if (inbound_value & CNTL_LSTN)              /* is card now listening? */
                        assert |= BUS_NRFD;                     /* listener and a full FIFO asserts NRFD */

                    else if (di_card->cntl_register & CNTL_LSTN)    /* was card a listener but is not now? */
                        deny |= BUS_NRFD;                           /* deny NRFD on the bus */


                if (SW8_SYSCTL) {                           /* system controller drives REN and IFC */
                    if (inbound_value & CNTL_REN)           /* REN control */
                        assert |= BUS_REN;                  /*   output is */
                    else                                    /*     coupled to */
                        deny |= BUS_REN;                    /*       the bus */

                    if (inbound_value & CNTL_IFC) {         /* is IFC set? */
                        assert |= BUS_IFC;                  /* assert IFC on the bus */

                        di_card->status_register =
                          di_card->status_register
                          & ~(STAT_LSTN | STAT_TALK)        /* clear listen and talk status */
                          | STAT_IFC;                       /*   and set IFC status */

                        di_card->ifc_timer =                /* start the IFC timer by calculating */
                          sim_gtime () + IFC_TIMEOUT;       /*   the IFC stop time (now + 100 microseconds) */
                        }
                    }

                if ((inbound_value                          /* if service request */
                  & (CNTL_SRQ | CNTL_CIC)) == CNTL_SRQ)     /*   and not the controller */
                    assert |= BUS_SRQ;                      /*     then assert SRQ on the bus */
                else                                        /*   else */
                    deny |= BUS_SRQ;                        /*     deny SRQ on the bus */

                di_card->cntl_register = (uint16) inbound_value;    /* save the control word */
                di_bus_control (card, CONTROLLER, assert, deny);    /* update the bus control state */
                }

            if (update_required && !(inbound_signals & ioCLF))  /* if update required and CLF is not present, */
                update_state (card);                            /*   update the state, else ioCLF will update it */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            di_card->flag_buffer = SET;                 /* set the flag buffer flip-flop */

            tpprintf (dptrs [card], DEB_CMDS, "[POPIO] Flag set\n");
            break;


        case ioCRS:                                     /* Control Reset */
            tpprintf (dptrs [card], DEB_CMDS, "[CRS] Master reset\n");

            di_card->status_register &=                     /* clear listen and talk status */
              ~(STAT_LSTN | STAT_TALK);

            deny = BUS_SRQ | BUS_REN | BUS_ATN | BUS_EOI;   /* clear the lines driven by the control register */

            if (di_card->cntl_register & (CNTL_NRFD | CNTL_LSTN))   /* if asserting NRFD or listening */
                deny |= BUS_NRFD;                                   /*   then deny because we're clearing */

            di_card->cntl_register = 0;                     /* clear the control word register */
            di_card->control = CLEAR;                       /* clear control */
            di_card->srq = CLEAR;                           /* clear SRQ */

            master_reset (card);                            /* perform a master reset */

            di_bus_control (card, CONTROLLER, 0, deny);     /* update the bus control state */
            update_state (card);                            /* update the card state */
            break;


        case ioCLC:                                         /* Clear Control flip-flop */
            di_card->control = CLEAR;                       /* clear the control flip-flop */

            tpprintf (dptrs [card], DEB_CMDS, "[CLC%s] Control cleared (configure mode)%s\n",
                      hold_or_clear, (inbound_signals & ioCLF ? ", master reset" : ""));

            if (inbound_signals & ioCLF)                    /* if ioCLF is given, */
                master_reset (card);                        /*   then do a master reset */
            break;                                          /*   (ioCLF will call update_state for us) */


        case ioSTC:                                         /* Set Control flip-flop */
            di_card->control = SET;                         /* set the control flip-flop */

            tpprintf (dptrs [card], DEB_CMDS, "[STC%s] Control set (data mode)\n",
                      hold_or_clear);
            break;


        case ioEDT:                                         /* End Data Transfer */
            tpprintf (dptrs [card], DEB_CPU, "[EDT] DCPC transfer ended\n");
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (di_card->control & di_card->flag)       /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (di_card->control & di_card->flag        /* if the control and flag */
              & di_card->flag_buffer)                   /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */

            if (di_card->control & di_card->srq)        /* if the control and srq flip-flops are set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            di_card->flag_buffer = CLEAR;               /* clear the flag buffer flip-flop */
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


/* Reset the simulator.

   During a hardware PRESET, POPIO sets the flag buffer and flag flip-flops, and
   CRS clears the control flip-flop and Control Word Register.  In addition, CRS
   performs a master reset on the card.

   PON is not used by the card.


   Implementation notes:

    1. During a power-on reset, a pointer to the FIFO simulation register is
       saved to allow access to the "qptr" field during FIFO loading and
       unloading.  This enables SCP to view the FIFO as a circular queue, so
       that the bottom word of the FIFO is always displayed as FIFO[0],
       regardless of where it is in the actual FIFO array.
*/

t_stat di_reset (DEVICE *dptr)
{
DIB *dibptr = (DIB *) dptr->ctxt;                       /* get the DIB pointer */
const CARD_ID card = (CARD_ID) (dibptr->card_index);    /* get the card number */

if (sim_switches & SWMASK ('P')) {                      /* is this a power-on reset? */
    di [card].fifo_reg = find_reg ("FIFO", NULL, dptr); /* find the FIFO register entry */

    if (di [card].fifo_reg == NULL)                     /* if not there */
        return SCPE_IERR;                               /*   then this is a programming error! */
    else                                                /* found it */
        di [card].fifo_reg->qptr = 0;                   /*   so reset the FIFO bottom index */

    di [card].status_register = 0;                      /* clear the status word */

    di [card].bus_cntl = 0;                             /* deny the HP-IB control lines */

    di [card].listeners = 0;                            /* clear the map of listeners */
    di [card].talker = 0;                               /* clear the map of talker */
    di [card].poll_response = 0;                        /* clear the map of parallel poll responses */

    di [card].ifc_timer = 0.0;                          /* clear the IFC timer */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

return SCPE_OK;
}



/* Disc interface global SCP routines */


/* Set a unit's bus address.

   Bus addresses range from 0-7 and are initialized to the unit number.  All
   units of a device must have unique bus addresses.  In addition, the card also
   has a bus address, although this is only used for the diagnostic.  The card
   address may be the same as a unit address, as all units are disconnected
   during a diagnostic run.

   The "value" parameter indicates whether the routine is setting a unit's bus
   address (0) or a card's bus address (1).


   Implementation notes:

    1. To ensure that each address is unique, a check is made of the other units
       for conflicting addresses.  An "invalid argument" error is returned if
       the desired address duplicates another.  This means that addresses cannot
       be exchanged without first assigning one of them to an unused address.
       Also, an address cannot be set that duplicates the address of a disabled
       unit (which cannot be displayed without enabling it).

       An alternate implementation would be to set the new assignments into a
       "shadow array" that is set into the unit flags (and checked for validity)
       only when a power-on reset is done.  This would follow the disc and tape
       controller hardware, which reads the HP-IB address switch settings only
       at power-up.
*/

t_stat di_set_address (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
t_stat status;
uint32 index, new_address;
uint32 old_address = GET_BUSADR (uptr->flags);
DEVICE *dptr = (DEVICE *) desc;

if (cptr == NULL)                                           /* if the address is not given */
    return SCPE_ARG;                                        /*   report a missing argument */

new_address = (uint32) get_uint (cptr, 10, 7, &status);     /* parse the address value */

if (status == SCPE_OK) {                                    /* is the parse OK? */
    if (value)                                              /* are we setting the card address? */
        dptr->flags = dptr->flags & ~DEV_BUSADR             /* store the new address in the device flags */
          | SET_DIADR (new_address);

    else {                                                  /* we are setting a unit address */
        for (index = 0; index < dptr->numunits; index++)    /* look through the units */
            if (new_address != old_address                  /*   to ensure that the address is unique */
              && new_address == GET_BUSADR (dptr->units [index].flags)) {
                printf ("Bus address conflict: DA%d\n", index);

                if (sim_log)
                    fprintf (sim_log, "Bus address conflict: DA%d\n", index);

                return SCPE_NOFNC;                          /* a duplicate address gives an error */
                }

        uptr->flags = uptr->flags & ~UNIT_BUSADR            /* the address is valid; change it */
          | SET_BUSADR (new_address);                       /*   in the unit flags */
        }
    }

return status;                                              /* return the result of the parse */
}


/* Show a unit's bus address.

   The "value" parameter indicates whether the routine is showing a unit's bus
   address (0) or a card's bus address (1).
*/

t_stat di_show_address (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const DEVICE *dptr = (const DEVICE *) desc;

if (value)                                                  /* do we want the card address? */
    fprintf (st, "address=%d", GET_DIADR (dptr->flags));    /* get it from the device flags */
else                                                        /* we want the unit address */
    fprintf (st, "bus=%d", GET_BUSADR (uptr->flags));       /* get it from the unit flags */

return SCPE_OK;
}


/* Set the bus cable connection.

   In normal use, the various tape and disc devices are connected together and
   to the disc interface card by HP-IB cables.  For the diagnostic, two disc
   interface cards are connected by a single cable.

   The "value" parameter indicates whether the routine is connecting the
   cable to devices for normal use (0) or to another card for diagnostics (1).


   Implementation notes:

    1. Initially, only one card and peripheral set is simulated: the ICD disc
       family (DA device).  For diagnostic use, a second, dummy card is enabled
       (DC device).  Once a second card simulation is implemented, this code
       will no longer be necessary.
*/

t_stat di_set_cable (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
DEVICE *dptr = (DEVICE *) desc;

if (value) {                                            /* is the diagnostic cable selected? */
    dptr->flags |= DEV_DIAG;                            /* set the diagnostic flag */
    dc_dev.flags &= ~DEV_DIS;                           /* enable the dummy device */
    dc_dev.flags |= DEV_DIAG;                           /*   and set its flag as well */
    }
else {                                                  /* the peripheral cable is selected */
    dptr->flags &= ~DEV_DIAG;                           /* clear the diagnostic flag */
    dc_dev.flags |= DEV_DIS;                            /* disable the dummy device */
    dc_dev.flags &= ~DEV_DIAG;                          /*  and clear its flag */
    }

return SCPE_OK;
}


/* Show the bus cable connection.

   The "value" parameter indicates whether the cable is connected to devices for
   normal use (0) or to another card for diagnostics (1).
*/

t_stat di_show_cable (FILE *st, UNIT *uptr, int32 value, CONST void *desc)
{
const DEVICE *dptr = (const DEVICE *) desc;

if (dptr->flags & DEV_DIAG)                             /* is the cable connected for diagnostics? */
    fputs ("diagnostic cable", st);                     /* report it */
else                                                    /* the cable is connected for device use */
    fputs ("HP-IB cable", st);                          /* report the condition */

return SCPE_OK;
}



/* Disc interface global bus routines.

   In hardware, the HP-IB bus consists of eight control lines and eight data
   lines.  Signals are asserted on the control lines to establish communication
   between a source and one or more acceptors.  For commands, the source is
   always the controller (the 12821A card), and the acceptors are all of the
   connected devices.  For data, the source is the current talker, and the
   acceptors are one or more current listeners.  A three-wire interlocking
   handshake enables communication at the rate of the slowest of the multiple
   acceptors.  The controller conducts a parallel poll by asserting ATN and EOI
   together.  Devices whose parallel poll responses are enabled each assert one
   of the data lines to indicate that service is required.

   In simulation, a disabled or detached unit logically is not connected to the
   bus.  The card maintains a bitmap of acceptors (all devices currently
   attached), listeners (all devices currently addressed to listen), the talker
   (the device currently addressed to talk), and the enabled parallel poll
   responses.  Changes in control line state are communicated to all acceptors
   via control/respond function calls, and data is exchanged between talker and
   listeners via source/acceptor function calls.  Data bytes are sent to all
   current listeners in bus-address order.  The card conducts a parallel poll by
   checking the response bitmap; devices must set and clear their poll responses
   appropriately in advance of the poll.

   Not all of the HP-IB control lines are simulated.  The DAV and NDAC handshake
   lines are never asserted; instead, they are simulated by the bus source
   function calling one or more bus acceptor functions.  SRQ and REN are
   asserted as directed by the system controller but are not otherwise used (no
   HP disc or tape devices assert SRQ or respond to REN).  IFC, ATN, EOI, and
   NRFD are asserted and tested by the controller and devices.  In particular,
   asserting NRFD will hold off a pending data transmission until it is denied.

   The functions that simulate the HP-IB (where "*" is "di", "da", etc.) are:

    di_bus_source --    Source a data byte to the bus.  Returns TRUE if the
                        byte was accepted (i.e., there were one or more
                        listeners) and FALSE if it was not.  Called by the
                        controller to send commands to devices, and called by
                        the current talker to send data to the listener(s).  ATN
                        and EOI should be asserted as required on the bus before
                        calling.

    *_bus_accept --     Accept a data byte from the bus.  Returns TRUE if the
                        byte was accepted and FALSE if it was not.  Called by
                        di_bus_source to handshake between source and acceptor.
                        If ATN is asserted on the bus, the byte is a command;
                        otherwise, it is data.  If EOI is asserted for a data
                        byte, it is the last byte of a transmission.

    di_bus_control --   Set the control lines on the bus.  Called by the system
                        controller to assert or deny REN or IFC, by the current
                        controller to assert or deny SRQ, NRFD, or ATN and EOI
                        (to conduct or conclude a parallel poll), and by the
                        current listener to assert or deny NRFD.  All connected
                        devices on the bus are notified of the changes.  It is
                        not necessary to call di_bus_control for changes to ATN
                        and EOI that accompany a command or data byte.

    *_bus_respond --    Respond to changes in the control lines on the bus.
                        Called by di_bus_control to inform each connected device
                        of a change in control state.

    di_poll_response -- Set a device's poll response.  Called by a device to
                        enable or disable its response to a future parallel
                        poll.
*/


/* Source a byte to the bus.

   This routine is called to send bytes to devices on the bus connected to the
   specified card.  If the card is in diagnostic mode, which simulate two cards
   connected by an HP-IB cable, then the byte is sent to another card in the
   card cage that is also in diagnostic mode and enabled to receive.  If the
   card is not in diagnostic mode, then the byte is sent to all acceptors (if a
   command) or to all listeners (if data) on the bus.

   The return value indicates whether or not there were any acceptors on the
   bus.


   Implementation notes:

    1. If the responses from a previously conducted parallel poll are not
       cleared from the FIFO before enabling the card to transmit, the card will
       appear to conduct a new parallel poll because the FIFO tags cause ATN and
       EOI to be asserted.  This "fake" parallel poll is ignored (a real
       parallel poll does not source data onto the bus).
*/

t_bool di_bus_source (CARD_ID card, uint8 data)
{
CARD_ID other;
uint32 acceptors, unit;
t_bool accepted = FALSE;

tpprintf (dptrs [card], DEB_XFER, "HP-IB DIO %03o signals %s available\n",
          data, fmt_bitset (di [card].bus_cntl, bus_format));

if (dptrs [card]->flags & DEV_DIAG)                         /* is this a diagnostic run? */
    for (other = first_card; other <= last_card; other++) { /* look through the list of cards */
        if (other != card && dptrs [other]                  /*   for the other card */
          && (dptrs [other]->flags & DEV_DIAG)              /*   that is configured for diagnostic mode */
          && (di [other].cntl_register & CNTL_LSTN))        /*   and is listening */
            accepted = di_bus_accept (other, data);         /* call the interface acceptor for the other card */
        }

else if ((di [card].bus_cntl & BUS_PPOLL) != BUS_PPOLL) {   /* this is a normal run; not a fake poll? */
    if (di [card].cntl_register & CNTL_LSTN)                /* is the card a listener? */
        accepted = di_bus_accept (card, data);              /* call the interface acceptor for this card */

    acceptors = di [card].acceptors;                        /* get the map of acceptors */

    if (!(di [card].bus_cntl & BUS_ATN)                     /* if a data transfer, */
      || (data & BUS_COMMAND) == BUS_ACG)                   /*   or an addressed command, e.g., SDC */
        acceptors = di [card].listeners;                    /* then limit just to listeners */

    for (unit = 0; acceptors; unit++) {                     /* loop through the units */
        if (acceptors & 1)                                  /* is the current unit accepting? */
            accepted |= (*bus_accept [card]) (unit, data);  /* call the acceptor for this card */

        acceptors = acceptors >> 1;                         /* move to the next acceptor */
        }
    }

if (!accepted)
    tpprintf (dptrs [card], DEB_XFER, "HP-IB no acceptors\n");

return accepted;
}


/* Assert or deny control on the bus.

   This routine is called by the indicated unit to assert or deny the HP-IB
   control lines on the bus connected to the specified card.  Separate sets of
   signals to assert and deny are provided.

   If the bus state after modification did not change, the routine returns with
   no further action.  Otherwise, if the card is in diagnostic mode, then
   notification of the bus change is sent to another card in the card cage that
   is also in diagnostic mode.

   If the card is not in diagnostic mode, then the set of control lines that
   are changing is checked to determine whether notification is necessary.  If
   not, then the change is not broadcast to improve performance.  However, if
   notification is required, then all acceptors on the bus are informed of the
   change.


   Implementation notes:

    1. If a signal is asserted and denied in the same call, the assertion takes
       precedence.

    2. Of the sixteen potential control line state changes, only IFC assertion
       and ATN and NRFD denial must be broadcast.  Asserting IFC unaddresses all
       devices, and denying ATN or NRFD allows a waiting talker to source a data
       byte to the bus.  Devices do not act upon the remaining thirteen state
       changes, and a considerable performance improvement is obtained by
       omitting the notification calls.

    3. All control line state notifications are sent in diagnostic mode, as the
       responses of the other card are specifically tested by the diagnostic.

    4. Asserting ATN and EOI will conduct a parallel poll.  Devices are not
       notified of the poll.  Instead, the previously stored parallel poll
       responses will be used.
*/

#define ASSERT_SET      (BUS_IFC)
#define DENY_SET        (BUS_ATN | BUS_NRFD)

void di_bus_control (CARD_ID card, uint32 unit, uint8 assert, uint8 deny)
{
CARD_ID other;
uint32 acceptors, responder;
t_bool responded;
uint8 new_state, new_assertions, new_denials;

new_state = di [card].bus_cntl & ~deny | assert;        /* set up the new control state */

if (new_state == di [card].bus_cntl)                    /* if the control state did not change */
    return;                                             /*   return now */

new_assertions = ~di [card].bus_cntl & assert;          /* get the changing assertions */
new_denials    =  di [card].bus_cntl & deny;            /* get the changing denials */

di [card].bus_cntl = new_state;                         /* establish the new control state */

if (unit == CONTROLLER)
    tpprintf (dptrs [card], DEB_XFER, "HP-IB card %d asserted %s denied %s bus is %s\n",
              card,
              fmt_bitset (new_assertions, bus_format),
              fmt_bitset (new_denials, bus_format),
              fmt_bitset (new_state, bus_format));
else
    tpprintf (dptrs [card], DEB_XFER, "HP-IB address %d asserted %s denied %s bus is %s\n",
              GET_BUSADR (dptrs [card]->units [unit].flags),
              fmt_bitset (new_assertions, bus_format),
              fmt_bitset (new_denials, bus_format),
              fmt_bitset (new_state, bus_format));

if ((dptrs [card]->flags & DEV_DIAG)                            /* is the card in diagnostic mode? */
  || (new_assertions & ASSERT_SET)                              /*   or are changed signals in the */
  || (new_denials & DENY_SET)) {                                /*     set that must be broadcast? */
    responded = FALSE;                                          /* assume no response was received */

    if (dptrs [card]->flags & DEV_DIAG) {                       /* is this a diagnostic run? */
        for (other = first_card; other <= last_card; other++)   /* look through the list of cards */
            if (other != card && dptrs [other]                  /*   for the other card */
              && (dptrs [other]->flags & DEV_DIAG)) {           /*     that is configured for diagnostic */
                di_bus_respond (other, new_state);              /* notify the other card of the new control state */
                responded = TRUE;                               /*   and note that there was a responder */
                }
        }

    else {                                                      /* this is a normal run */
        update_state (card);                                    /* update the card for the new control state */

        acceptors = di [card].acceptors;                        /* get the map of acceptors */
        responded = (acceptors != 0);                           /* set response if there are any acceptors */

        for (responder = 0; acceptors; responder++) {               /* loop the through units */
            if ((acceptors & 1) && responder != unit)               /* is the current unit accepting? */
                (*bus_respond [card]) (card, responder, new_state); /* call the responder for this card */

            acceptors = acceptors >> 1;                             /* move to the next acceptor */
            }
        }

    if (!responded)
        tpprintf (dptrs [card], DEB_XFER, "HP-IB no responders\n");
}

if ((new_state & BUS_PPOLL) == BUS_PPOLL)               /* was a parallel poll requested? */
    di_bus_poll (card);                                 /* conduct the poll */

return;
}


/* Enable or disable a unit's parallel poll response.

   The poll response for a unit connected to a specified card is set or cleared
   as indicated.  If a parallel poll is in progress when a poll response is set,
   the poll is conducted again to reflect the new response.
*/

void di_poll_response (CARD_ID card, uint32 unit, FLIP_FLOP response)
{
const uint32 address = GET_BUSADR (dptrs [card]->units [unit].flags);
uint32 previous_response = di [card].poll_response;

if (response == SET) {                                  /* enable the poll response? */
    di [card].poll_response |= PPR (address);           /* set the response bit */

    if ((di [card].bus_cntl & BUS_PPOLL) == BUS_PPOLL)  /* is a parallel poll in progress? */
        di_bus_poll (card);                             /* conduct again with the new response */
    }
else                                                    /* disable the poll response */
    di [card].poll_response &= ~PPR (address);          /*   by clearing the response bit */

if (previous_response != di [card].poll_response)
    tpprintf (dptrs [card], DEB_XFER, "HP-IB address %d parallel poll response %s\n",
              address, (response == SET ? "enabled" : "disabled"));

return;
}



/* Disc interface local bus routines */


/* Conduct a parallel poll on the bus.

   A controller asserting ATN and EOI simultaneously on the bus is conducting a
   parallel poll.  In hardware, each device whose poll response is enabled
   asserts the data line corresponding to its bus address.  The controller
   terminates the poll by denying ATN and EOI.

   Setting the CIC (controller in charge) and PPE (parallel poll enable) bits in
   the Control Word Register direct the disc interface to conduct a poll.
   Setting PPE without CIC enables the poll response for the interface.

   In the diagnostic mode, one card is set to conduct the poll, and the other is
   set to respond to it.  In the normal mode, connected devices have set or
   cleared their respective poll responses before this routine is called.


   Implementation notes:

    1. The card hardware fills the upper and lower bytes of the FIFO with the
       response byte.  In simulation, we use the diag_access mode to do the same
       thing (diagnostic loopback also fills both bytes with the lower byte).
*/

static void di_bus_poll (CARD_ID card)
{
CARD_ID other;
uint8   response;

if ((di [card].cntl_register
  & (CNTL_PPE | CNTL_CIC)) == CNTL_PPE)                     /* is the card's poll response enabled? */
    response = di [card].poll_response                      /* add the card's response */
      | PPR (GET_DIADR (dptrs [card]->flags));              /*   to the devices' responses */
else
    response = di [card].poll_response;                     /* the card response is disabled, so just use devices */

if (dptrs [card]->flags & DEV_DIAG)                         /* is this a diagnostic run? */
    for (other = first_card; other <= last_card; other++)   /* look through the list of cards */
        if (other != card && dptrs [other]                  /*   for another card */
          && (dptrs [other]->flags & DEV_DIAG)              /*     that is configured for the diagnostic */
          && (di [other].cntl_register                      /*       and has PPE asserted */
          & (CNTL_PPE | CNTL_CIC)) == CNTL_PPE)
            response |=                                     /* merge its poll response */
              PPR (GET_DIADR (dptrs [other]->flags));

if (response) {                                             /* is a poll response indicated? */
    tpprintf (dptrs [card], DEB_XFER, "HP-IB parallel poll DIO %03o\n",
              response);

    while (di [card].fifo_count != FIFO_SIZE)               /* fill the card FIFO with the responses */
        fifo_load (card, (uint16) response, diag_access);   /*   (hardware feature) */

    update_state (card);                                    /* update the card state */
    }

return;
}


/* Accept a data byte from the bus.

   The indicated card accepts a byte that has been sourced to the bus.  The byte
   is loaded into the FIFO, and the card state is updated to reflect the load.

   Bus acceptors return TRUE to indicate that the byte was accepted.  A card
   always accepts a byte, so the routine always returns TRUE.
*/

static t_bool di_bus_accept (CARD_ID card, uint8 data)
{
tpprintf (dptrs [card], DEB_XFER, "HP-IB card %d accepted data %03o\n",
          card, data);

fifo_load (card, data, bus_access);                     /* load the data byte into the FIFO */
update_state (card);                                    /*   and update the card state */
return TRUE;                                            /* indicate that the byte was accepted */
}


/* Respond to the bus control lines.

   The indicated card is notified of the new control state on the bus.  The
   routine establishes the new bus state and updates the card state to reflect
   the change.
*/

static void di_bus_respond (CARD_ID card, uint8 new_cntl)
{
di [card].bus_cntl = new_cntl;                          /* update the bus control lines */
update_state (card);                                    /* update the card state */
return;
}



/* Disc interface local utility routines */


/* Master reset the interface.

   This is the programmed card master reset, not the simulator reset routine.
   Master reset initializes a number of flip-flops and data paths on the card.
   The primary use, other than during a PRESET, is to clear the FIFO in
   preparation to changing the card from a listener to a talker or vice versa.
   This ensures that unneeded FIFO data is not transmitted inadvertently to the
   bus or to the CPU.  It is also used when changing the data mode from unpacked
   to packed to release the byte pointer flip-flops, which are held in the
   "lower byte" position during unpacked transfers.

   In hardware, a master reset:
    - clears the EDT, EOR, IRL, LBO, LBI, and IFC flip-flops
    - clears the Input Data Register
    - clears the FIFO
    - sets or clears the odd/even input and output byte pointer flip-flops,
      depending on whether the P (packed transfer) bit is set in the Control
      Word Register
*/

static void master_reset (CARD_ID card)
{
di [card].edt = CLEAR;                                  /* clear the EDT flip-flop */
di [card].eor = CLEAR;                                  /* clear the EOR flip-flop */

if (di [card].cntl_register & CNTL_PACK)                /* if packed mode is set, */
    di [card].ibp = di [card].obp = upper;              /*   MR sets the selectors to the upper byte */
else                                                    /* otherwise, unpacked mode overrides */
    di [card].ibp = di [card].obp = lower;              /*   and sets the selectors to the lower byte */

di [card].status_register &=                            /* clear the status flip-flops */
  ~(STAT_IRL | STAT_LBO | STAT_LBI | STAT_IFC);

di [card].input_data_register = 0;                      /* clear the input data register */
di [card].fifo_count = 0;                               /* clear the FIFO */

tpprintf (dptrs [card], DEB_BUF, "FIFO cleared\n");

return;
}


/* Update the interface state.

   In hardware, certain external operations cause automatic responses by the
   disc interface card.  For example, when the Input Data Register is unloaded
   by an LIx instruction, it is automatically reloaded with the next word from
   the FIFO.  Also, the card may be set to interrupt in response to the
   assertion of certain bus control lines.

   In simulation, this routine must be called whenever the FIFO, card control,
   or bus control state changes.  It determines whether:

    1. ...the next word from the FIFO should be unloaded into the IDR.  If the
       card is listening, and the IDR is empty, and the FIFO contains data, then
       a word is unloaded and stored in the IDR, and the Input Register Loaded
       status bit is set.

    2. ...the next word from the FIFO should be unloaded and sourced to the bus.
       If the card is talking (but not polling), and the listener is ready to
       accept data, and the last byte has not been sent, and the FIFO contains
       data, then a word is unloaded and sourced to the bus.  This occurs
       regardless of whether or not there are any listeners.

    3. ...an interface clear operation has completed.  If IFC is asserted, and
       the current simulation time is later than the IFC expiration time, then
       IFC is denied, and the timer is reset.

    4. ...the card should assert NRFD to prevent FIFO overflow.  If the card is
       listening, and the FIFO is full, or the last byte has been received, or a
       pause has been explicitly requested, then NRFD is asserted.

    5. ...the SRQ flip-flop should be set or cleared.  If the card is listening
       and the Input Data Register has been loaded, or the card is talking and
       the FIFO is not full, then SRQ is asserted to request a DCPC transfer.

    6. ...the flag flip-flop should be set or cleared.  If the Input Data
       Register has been loaded or the Last Byte Out flip-flop is set and the
       corresponding Control Word Register IRL or LBO bits are set, or the End
       of Record flip-flop is set and the Input Data Register has been unloaded,
       or SRQ is asserted on the bus and the corresponding Control Word Register
       bit is set when the card is not the controller-in-charge, or REN or IFC
       is asserted on the bus and the corresponding Control Word Register bits
       are set when the card is not the system controller, then the flag is set
       to request an interrupt.


   Implementation notes:

    1. The fifo_unload routine may set STAT_LBO, so the flag test must be done
       after unloading.

    2. The gcc compiler (at least as of version 4.6.2) does not optimize
       repeated use of array-of-structures accesses.  Instead, it recalculates
       the index each time, even though the index is a constant within the
       function.  To avoid this performance penalty, we use a pointer to the
       selected DI_STATE structure.  Note that VC++ 2008 does perform this
       optimization.
 */

static void update_state (CARD_ID card)
{
DI_STATE * const di_card = &di [card];
uint8 assert = 0;
uint8 deny = 0;
uint16 data;
FLIP_FLOP previous_state;

if (di_card->cntl_register & CNTL_LSTN) {               /* is the card a listener? */
    if (!(di_card->status_register & STAT_IRL)          /* is the IDR empty? */
      && ! FIFO_EMPTY) {                                /*   and data remains in the FIFO? */
        data = fifo_unload (card, cpu_access);          /* unload the FIFO */
        di_card->input_data_register = data;            /*   into the IDR */
        di_card->status_register |= STAT_IRL;           /* set the input register loaded status */
        }
    }

else if ((di_card->cntl_register                        /* is the card a talker? */
  & (CNTL_TALK | CNTL_PPE)) == CNTL_TALK)               /*   and not polling? */
    while (! FIFO_EMPTY                                 /* is data remaining in FIFO? */
      && !(di_card->bus_cntl & BUS_NRFD)                /*   and NRFD is denied? */
      && !(di_card->status_register & STAT_LBO)) {      /*   and the last byte has not been sent? */
        data = fifo_unload (card, bus_access);          /* unload a FIFO byte */
        di_bus_source (card, (uint8) data);             /* source it to the bus */
        }


if (di_card->bus_cntl & BUS_IFC                         /* is an IFC in progress? */
  && di_card->ifc_timer != 0.0                          /*   and I am timing? */
  && sim_gtime () > di_card->ifc_timer) {               /*   and has the timeout elapsed? */
    deny = BUS_IFC;                                     /* deny IFC on the bus */
    di_card->ifc_timer = 0.0;                           /* clear the IFC timer */
    di_card->status_register &= ~STAT_IFC;              /*   and clear IFC status */
    }


if (di_card->cntl_register & CNTL_LSTN)                 /* is the card a listener? */
    if (di_card->cntl_register & CNTL_NRFD              /* if explicitly requested */
      || di_card->status_register & STAT_LBI            /*   or the last byte is in */
      || FIFO_FULL)                                     /*   or the FIFO is full */
        assert = BUS_NRFD;                              /*   then assert NRFD */
    else                                                /* otherwise the card is ready for data */
        deny |= BUS_NRFD;                               /*   so deny NRFD */

if (assert != deny)                                     /* was there any change in bus state? */
    di_bus_control (card, CONTROLLER, assert, deny);    /* update the bus control */


previous_state = di_card->srq;                          /* save the current SRQ state */

if (di_card->cntl_register & CNTL_LSTN                  /* if the card is a listener */
  && di_card->status_register & STAT_IRL                /*   and the input register is loaded, */
  || di_card->cntl_register & CNTL_TALK                 /* or the card is a talker */
  && ! FIFO_FULL)                                       /*   and the FIFO is not full */
    di_card->srq = SET;                                 /* then request a DCPC cycle */
else
    di_card->srq = CLEAR;                               /* otherwise, DCPC service is not needed */


if (di_card->srq != previous_state)
    tpprintf (dptrs [card], DEB_CMDS, "SRQ %s\n",
              di_card->srq == SET ? "set" : "cleared");


if (di_card->status_register & STAT_IRL                 /* is the input register loaded */
    && di_card->cntl_register & CNTL_IRL                /*   and notification is wanted? */
  || di_card->status_register & STAT_LBO                /* or is the last byte out */
    && di_card->cntl_register & CNTL_LBO                /*   and notification is wanted? */
  || di_card->eor == SET                                /* or was the end of record seen */
    && !(di_card->status_register & STAT_IRL)           /*   and the input register was unloaded? */
  || di_card->bus_cntl & BUS_SRQ                        /* or is SRQ asserted on the bus */
    && di_card->cntl_register & CNTL_SRQ                /*   and notification is wanted */
    && di_card->cntl_register & CNTL_CIC                /*   and the card is not controller? */
  || !SW8_SYSCTL                                        /* or is the card not the system controller */
    && di_card->bus_cntl & BUS_REN                      /*   and REN is asserted on the bus */
    && di_card->cntl_register & CNTL_REN                /*   and notification is wanted? */
  || !SW8_SYSCTL                                        /* or is the card not the system controller */
    && di_card->status_register & STAT_IFC              /*   and IFC is asserted on the bus */
    && di_card->cntl_register & CNTL_IFC) {             /*   and notification is wanted? */

    tpprintf (dptrs [card], DEB_CMDS, "Flag set\n");

    di_card->flag_buffer = SET;                         /* set the flag buffer */
    io_assert (dptrs [card], ioa_ENF);                  /*   and flag flip-flops and recalculate interrupts */
    }

else if (di_card->srq != previous_state)                /* if SRQ changed state, */
    io_assert (dptrs [card], ioa_SIR);                  /*   then recalculate interrupts */

return;
}


/* Load a word or byte into the FIFO.

   A word or byte is loaded into the next available location in the FIFO.  The
   significance of the data parameter is indicated by the access mode as
   follows:

     - For CPU access, the parameter is a 16-bit value.

     - For bus access, the parameter is an 8-bit value in the lower byte and a
       zero in the upper byte.

     - For diagnostic access, the parameter is an 8-bit value in the lower byte
       that will be duplicated in the upper byte.

   For bus access, byte loading into the FIFO is controlled by the value of the
   Input Buffer Pointer (IBP) selector.

   In addition to data words, the FIFO holds tags that mark the last byte
   received or to be transmitted and that indicate the state of the ATN and EOI
   bus lines (if listening) or the states to assert (if talking).  The tag is
   assembled into the upper word, the data is assembled into the lower word, and
   then the 32-bit value is stored in the next available FIFO location.

   If data is coming from the CPU, the 16-bit value is loaded into the next FIFO
   location, and the occupancy count is incremented.

   If the data is coming from the bus, and the input mode is unpacked, the 8-bit
   value is loaded into the lower byte of the next FIFO location, and the
   occupancy count is incremented.  In hardware, the upper FIFO is not clocked;
   in simulation, the upper byte is set to zero.  The IBP always points at the
   lower byte in unpacked mode.

   If the data is coming from the bus, and the input mode is packed, the 8-bit
   value is loaded into either the upper or lower byte of the next FIFO
   location, depending on the value of the IBP, and the IBP is toggled.  If the
   value was stored in the lower byte, the occupancy count is incremented.

   A special case occurs when the value is to be stored in the upper byte, and
   the LBR tag is set to indicate that this is the last byte to be received.  In
   this case, the value is stored in both bytes of the next FIFO location, and
   the occupancy counter is incremented.

   If data is coming from the diagnostic FIFO loopback, the 8-bit value in the
   lower byte is copied to the upper byte, the resulting 16-bit value is loaded
   into the next FIFO location, and the occupancy count is incremented.


   Implementation notes:

    1. Four tag bits are loaded into the upper word of each FIFO entry:

        - Last Byte Received (while receiving, a line feed is received and the
          LF bit is set in the Control Word Register, or a byte with EOI
          asserted is received and the EOI bit is set).

        - End of Data Transfer (while transmitting, DCPC asserts the EDT
          backplane signal, or an unpacked-mode data word has the LBO bit set,
          or a packed-mode OTx is issued without an accompanying CLF).

        - ATN (the state of ATN on the bus if receiving, or the ATN bit in the
          unpacked data word if transmitting).

        - EOI (the state of EOI on the bus if receiving, or the EOI bit in the
          unpacked data word if transmitting).

    2. The FIFO is implemented as circular queue to take advantage of REG_CIRC
       EXAMINE semantics.  REG->qptr is the index of the first word currently in
       the FIFO.  By specifying REG_CIRC, examining FIFO[0-n] will always
       display the words in load order, regardless of the actual array index of
       the start of the list.  The number of words currently present in the FIFO
       is kept in fifo_count (0 = empty, 1-16 = number of words available).

       If fifo_count < FIFO_SIZE, (REG->qptr + fifo_count) mod FIFO_SIZE is the
       index of the new word location.  Loading stores the word there and then
       increments fifo_count.

    3. Because the load and unload routines need access to qptr in the REG
       structure for the FIFO array, pointers to the REG for each card are
       stored in the fifo_reg array during device reset.

    4. The gcc compiler (at least as of version 4.6.2) does not optimize
       repeated use of array-of-structures accesses.  Instead, it recalculates
       the index each time, even though the index is a constant within the
       function.  To avoid this performance penalty, we use a pointer to the
       selected DI_STATE structure.  Note that VC++ 2008 does perform this
       optimization.
*/

static void fifo_load (CARD_ID card, uint16 data, FIFO_ACCESS access)
{
uint32 tag, index;
t_bool add_word = TRUE;
DI_STATE * const di_card = &di [card];

if (FIFO_FULL) {                                        /* is the FIFO already full? */
    tpprintf (dptrs [card], DEB_BUF, "Attempted load to full FIFO, data %0*o\n",
              (access == bus_access ? 3 : 6), data);

    return;                                             /* return with the load ignored */
    }

if (di_card->cntl_register & CNTL_LSTN) {               /* is the card receiving? */
    tag = (di_card->bus_cntl                            /* set the tag from the bus signals */
      & (BUS_ATN | BUS_EOI)) << BUS_SHIFT;              /*   shifted to the tag locations */

    if ((di_card->cntl_register & CNTL_EOI              /* EOI detection is enabled, */
      && di_card->bus_cntl & BUS_EOI)                   /*   and data was tagged with EOI? */
      || (di_card->cntl_register & CNTL_LF              /* or LF detection is enabled, */
      && LOWER_BYTE (data) == LF)) {                    /*   and the byte is a line feed? */
        tag = tag | TAG_LBR;                            /* tag as the last byte received */
        di_card->status_register |= STAT_LBI;           /* set the last byte in status */
        }
    else                                                /* neither termination condition was seen */
        di_card->status_register &= ~STAT_LBI;          /*   so clear the last byte in status */
    }

else                                                    /* the card is transmitting */
    tag = (data & (DATA_ATN | DATA_EOI)) << DATA_SHIFT; /* set the tag from the data shifted to the tag location */

if (di_card->edt == SET)                                /* is this the end of the data transfer? */
    tag = tag | TAG_EDT;                                /* set the EDT tag */


index = (di_card->fifo_reg->qptr                        /* calculate the index */
          + di_card->fifo_count) % FIFO_SIZE;           /*   of the next available location */

if (access == bus_access) {                             /* is this a bus access */
    if (di_card->ibp == upper) {                        /*   in packed mode for the upper byte? */
        di_card->ibp = lower;                           /* set the lower byte as next */

        if (tag & TAG_LBR)                              /* is this the last byte? */
            di_card->fifo [index] =                     /* copy to both bytes of the FIFO */
              tag | TO_WORD (data, data);               /*   and store with the tag */
        else {                                          /* more bytes are expected */
            di_card->fifo [index] =                     /*   so position this byte */
              tag | TO_WORD (data, 0);                  /*   and store it with the tag */
            add_word = FALSE;                           /* wait for the second byte before adding */
            }
        }

    else                                                /* this is the lower byte */
        if (di_card->cntl_register & CNTL_PACK) {       /* is the card in packed mode? */
            di_card->ibp = upper;                       /* set the upper byte as next */

            di_card->fifo [index] =                     /* merge the data and tag values */
              tag | di_card->fifo [index] | TO_WORD (0, data);
            }
        else                                            /* the card is in unpacked mode */
            di_card->fifo [index] =                     /* position this byte */
              tag | TO_WORD (0, data);                  /*   and store with the tag */
    }

else if (access == cpu_access)                          /* is this a cpu access? */
    di_card->fifo [index] = tag | data;                 /* store the tag and full word in the FIFO */

else {                                                  /* must be diagnostic access */
    data = TO_WORD (data, data);                        /* copy the lower byte to the upper byte */
    di_card->fifo [index] = tag | data;                 /*   and store the tag and full word in the FIFO */
    }

if (add_word)                                           /* did we add a word to the FIFO? */
    di_card->fifo_count = di_card->fifo_count + 1;      /* increment the count of words stored */

tpprintf (dptrs [card], DEB_XFER, "Data %0*o tag %s loaded into FIFO (%d)\n",
          (access == bus_access ? 3 : 6), data,
          fmt_bitset (tag, tag_format), di_card->fifo_count);

return;
}


/* Unload a word or byte from the FIFO.

   A word or byte is unloaded from the first location in the FIFO.  The
   significance of the returned value is indicated by the access mode as
   follows:

     - For CPU access, a 16-bit value is unloaded and returned.

     - For bus access, an 8-bit value is unloaded and returned.

     - For diagnostic access, an 16-bit value is unloaded, and the lower byte
       is returned.

   For bus access, byte unloading from the FIFO is controlled by the value of
   the Output Buffer Pointer (OBP) selector.

   If the FIFO is not empty, the first entry is obtained and split into tag and
   data words.  The LBR tag value is loaded into the EOR flip-flop if the CPU is
   accessing.  The EDT tag sets Last Byte Out status if the last byte is being
   unloaded.

   If the data is going to the CPU, the 16-bit packed data value is returned as
   is, or the lower byte of the unpacked value is merged with the tags for ATN
   and EOI and returned.  The occupancy count is decremented to unload the FIFO
   entry.

   If the data is going to the bus, and the input mode is unpacked, the 8-bit
   value is returned in the lower byte, and the occupancy count is decremented.
   In hardware, the upper FIFO is not clocked; in simulation, the upper byte is
   ignored.  The OBP always points at the lower byte in unpacked mode.

   If the data is going to the bus, and the input mode is packed, the 8-bit
   value is unloaded from either the upper or lower byte of the data word,
   depending on the value of the OBP, and returned in the lower byte.  The OBP
   value is toggled.  If the value was obtained from the lower byte, the
   occupancy count is decremented to unload the FIFO.  Otherwise, the count is
   not altered, so that the lower-byte access will be from the same FIFO entry.

   If data is going to the diagnostic FIFO loopback, the lower byte of the
   16-bit value is returned; the upper byte of the returned value is zero.


   Implementation notes:

    1. Four tag bits are unloaded from the upper word of each FIFO entry:

        - Last Byte Received (sets the End of Record flip-flop when the last
          byte received is loaded into the Input Data Register).

        - End of Data Transfer (sets the LBO bit in the Status Word Register
          when the last byte is unloaded from the FIFO).

        - ATN (in unpacked mode, sets the ATN bit in the returned data word
          if listening, or controls the bus ATN line if talking; in packed mode,
          the tag is ignored).

        - EOI (in unpacked mode, sets the EOI bit in the returned data word if
          listening, or asserts the bus EOI line if talking; in packed mode, the
          tag is ignored).

       ATN and EOI tag handling is complex.  If the card is listening in the
       unpacked mode, the ATN tag substitutes for bit 8 of the data word, and
       the EOI tag substitutes for bit 9.  In the packed mode, bits 8 and 9 are
       as stored in the FIFO (they are upper-byte data bits).

       If the card is talking in the unpacked mode, the ATN tag asserts or
       denies ATN on the bus if the card is the CIC, and the EOI tag asserts or
       denies EOI on the bus.  In the packed mode, the ATN bit in the Control
       Word Register asserts or denies ATN on the bus if the card is the CIC,
       and the EOI bit asserts EOI on the bus if the last byte of the entry
       tagged with EDT has been unloaded from the FIFO (which sets LBO status)
       or denies EOI otherwise.

    2. In hardware, the EOR flip-flop is clocked with the Input Data Register.
       Therefore, when the card is listening, EOR is set not when the last byte
       is unloaded from the FIFO, but rather when that byte is loaded into the
       IDR.  These two actions occur together when the IDR is empty.

       However, during diagnostic access, data unloaded from the FIFO is
       reloaded, and the IDR is never clocked.  As the T and L bits must be set
       with DIAG in the Control Word Register to enable the loopback path, the
       LBR tag will be entered into the FIFO if EOI or LF detection is enabled,
       but the EOR flip-flop will not be set when that word falls through to be
       unloaded.

       In simulation, EOR is set whenever the LBR tag is unloaded from the FIFO
       during CPU access, as a CPU unload is always followed by an IDR store.

    3. If fifo_count > 0, REG->qptr is the index of the word to remove.  Removal
       gets the word and then increments qptr (mod FIFO_SIZE) and decrements
       fifo_count.

    4. The gcc compiler (at least as of version 4.6.2) does not optimize
       repeated use of array-of-structures accesses.  Instead, it recalculates
       the index each time, even though the index is a constant within the
       function.  To avoid this performance penalty, we use a pointer to the
       selected DI_STATE structure.  Note that VC++ 2008 does perform this
       optimization.
*/

static uint16 fifo_unload (CARD_ID card, FIFO_ACCESS access)
{
uint32 data, tag;
t_bool remove_word = TRUE;
DI_STATE * const di_card = &di [card];

if (FIFO_EMPTY) {                                       /* is the FIFO already empty? */
    tpprintf (dptrs [card], DEB_BUF, "Attempted unload from empty FIFO\n");
    return 0;                                           /* return with no data */
    }

data = di_card->fifo [di_card->fifo_reg->qptr];         /* get the tag and data from the FIFO */

tag = data & TAG_MASK;                                  /* mask the tag to just the tag bits */
data = data & D16_MASK;                                 /*   and the data to just the data bits */

if (tag & TAG_EDT                                       /* is this the end of a data transfer */
  && (di_card->obp == lower                             /*   and the lower byte is next */
  || di_card->cntl_register & CNTL_ODD))                /*   or we are sending an odd number of bytes? */
    di_card->status_register |= STAT_LBO;               /* set the last byte out status */


if (access == cpu_access) {                             /* is this a cpu access? */
    if (!(di_card->cntl_register & CNTL_PACK))          /*   in unpacked mode? */
        data = data & ~(DATA_ATN | DATA_EOI)            /* substitute the ATN/EOI tag values */
          | (tag & (TAG_ATN | TAG_EOI)) >> DATA_SHIFT;  /*   into the data word */

    if (tag & TAG_LBR)                                  /* is this the last byte? */
        di_card->eor = SET;                             /* set */
    else                                                /*   or clear */
        di_card->eor = CLEAR;                           /*     the end-of-record flip-flop */
    }

else if (access == bus_access)                          /* is this a bus access? */
    if (di_card->obp == upper) {                        /* is this the upper byte? */
        di_card->obp = lower;                           /* set the lower byte as next */
        data = UPPER_BYTE (data);                       /* mask and position the upper byte in the data word */
        remove_word = FALSE;                            /* do not unload the FIFO until the next byte */
        }

    else {                                              /* this is the lower byte */
        data = LOWER_BYTE (data);                       /* mask and position it in the data word */

        if (di_card->cntl_register & CNTL_PACK)         /* is the card in the packed mode? */
            di_card->obp = upper;                       /* set the upper byte as next */
        }

else                                                    /* must be a diagnostic access */
    data = LOWER_BYTE (data);                           /* access is to the lower byte only */


if (remove_word) {                                      /* remove the word from the FIFO? */
    di_card->fifo_reg->qptr =                           /* update the FIFO queue pointer */
      (di_card->fifo_reg->qptr + 1) % FIFO_SIZE;        /*   and wrap around as needed */

    di_card->fifo_count = di_card->fifo_count - 1;      /* decrement the FIFO count */
    }


tpprintf (dptrs [card], DEB_BUF, "Data %0*o tag %s unloaded from FIFO (%d)\n",
          (access == cpu_access ? 6 : 3), data,
          fmt_bitset (tag, tag_format), di_card->fifo_count);


if (di_card->cntl_register & CNTL_TALK)                 /* is the card talking? */
    if (di_card->cntl_register & CNTL_PACK)             /* is it in the packed mode? */
        if (di_card->status_register & STAT_LBO         /* yes, is the last byte out? */
          && di_card->cntl_register & CNTL_EOI)         /*   and is EOI control enabled? */
            di_card->bus_cntl |= BUS_EOI;               /* assert EOI on the bus */
        else
            di_card->bus_cntl &= ~BUS_EOI;              /* deny EOI on the bus */

    else {                                              /* the card is in the unpacked mode */
        if (di_card->cntl_register & CNTL_CIC)          /* is the card the controller in charge? */
            di_card->bus_cntl =                         /* assert or deny the ATN bus line */
              di_card->bus_cntl & ~BUS_ATN              /*   from the ATN tag value */
              | (uint8) ((tag & TAG_ATN) >> BUS_SHIFT);

        di_card->bus_cntl =                             /* assert or deny the EOI bus line */
          di_card->bus_cntl & ~BUS_EOI                  /*   from the EOI tag value */
          | (uint8) ((tag & TAG_EOI) >> BUS_SHIFT);
        }

return (uint16) data;                                   /* return the data value */
}
