/* hp2100_dma.c: HP 21xx/1000 Direct Memory Access/Dual-Channel Port Controller simulator

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

   DMA1,DMA2    12607B/12578A/12895A Direct Memory Access
   DCPC1,DCPC2  12897B Dual Channel Port Controller

   20-Jul-18    JDB     Split out from hp2100_cpu.c

   References:
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
     - 12607A Direct Memory Access Operating and Service Manual
         (12607-90002, January 1970)
     - 12578A/12578A-01 Direct Memory Access Operating and Service Manual
         (12578-9001, March 1972)


   This module simulates the 12578A/12607B/12895A Direct Memory Access and
   12897B Dual-Channel Port Controller devices (hereafter, "DMA").  These
   controllers permit the CPU to transfer data directly between an I/O device
   and memory on a cycle-stealing basis.  Depending on the CPU, the device
   interface, and main memory speed, DMA is capable of transferring data blocks
   from 1 to 32,768 words in length at rates between 500,000 and 1,000,000 words
   per second.  The 2114 supports a single DMA channel.  All other CPUs support
   two DMA channels.

   DMA is configured for transfers by setting control words via two select
   codes: 2 and 6 for channel 1, and 3 and 7 for channel 2.  During simultaneous
   transfers, channel 1 has priority over channel 2.  Otherwise, the channels
   are identical.  Channel programming involves setting three control words, as
   follows.

   SC 06/07 Control Word 1 format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | B | C | -   -   -   -   -   -  -  |  device select code   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = assert STC during each cycle
     B = enable byte packing and unpacking (12578A only)
     C = assert CLC at the end of the block transfer

   The 12607B (2114) stores only bits 2-0 of the device select code, which are
   decoded to assert SCL0-SCL6 on the I/O backplane.  Along with unconditional
   assertion of SCM1, the 12607B can control interfaces assigned to select codes
   10-16, corresponding to the seven I/O slots available in the 2114 chassis.

   SC 02/03 Control Words 2 and 3 format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | D |                  starting memory address                  | word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      negative word count                      | word 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     D = transfer direction is out of/into memory (0/1)

   Control word 2 is stored if the control flip-flop of select code 2 is clear,
   i.e., if the OTA/B is preceded by CLC; control word 3 is stored if the
   flip-flop is set by a preceding STC.

   The 12607B supports 14-bit addresses and 13-bit word counts.  The 12578A
   supports 15-bit addresses and 14-bit word counts.  The 12895A and 12897B
   support 15-bit addresses and 16-bit word counts.

   DMA is started by setting the control flip-flop on select code 6.  DMA
   completion is indicated when the flag flip-flop sets on select code 8, which
   causes an interrupt if enabled.  Clearing the control flip-flop does not stop
   DMA; instead, it inhibits the DMA completion interrupt.  DMA is aborted by
   setting the flag on select code 6.  DMA activity may be checked by testing
   the flag on select code 6.

   The remaining word count may be obtained at any time by reading from select
   code 2, as follows.

   SC 02/03 Word Count 2 and 3 format (LIA, LIB, MIA, and MIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0 |           negative remaining word count           | 12607
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0 |             negative remaining word count             | 12578
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 negative remaining word count                 | 12895/12897
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Because the I/O bus floats to zero on 211x computers, an IOI (read word
   count) returns zeros in the unused bit locations, even though the word count
   itself is a two's-complement negative value.


   Implementation notes:

    1. The DMA simulation transfers one word per DMA cycle, with cycles
       interleaved with machine instruction execution.  The alternative
       implementation of transferring the entire data block between one
       instruction and the next and then delaying DMA completion for the
       appropriate block-transfer time will not work.  The HP diagnostics check
       for word-at-a-time transfers by watching the word count.  Other HP
       software (e.g., the RTE disc driver for the 7905/06/20/25 units) can
       abort a transfer in progress and then use the remaining word count as an
       indication of the error location.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* DMA program constants */

#define DMA_CHAN_COUNT      2                   /* number of DMA channels */

typedef enum {                                  /* channel number */
    ch1,                                        /*   channel 1 */
    ch2                                         /*   channel 2 */
    } CHANNEL;

#define DMA_1_REQ           (1u << ch1)         /* channel 1 request */
#define DMA_2_REQ           (1u << ch2)         /* channel 2 request */

#define TO_REQ(c)           (1u << (c))


/* DMA control words.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | B | C | -   -   -   -   -   -  -  |  device select code   | word 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | D |                  starting memory address                  | word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      negative word count                      | word 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define CN_STC              0100000u            /* (S) assert STC during each cycle */
#define CN_PACK             0040000u            /* (B) enable byte packing and unpacking (12578A only) */
#define CN_CLC              0020000u            /* (C) assert CLC at the end of the block transfer */
#define CN_SC               0000077u            /* device select code mask (all but 12607) */
#define CN_SC_12607         0000007u            /* device select code mask (12607 only) */

#define CN_12607_MASK       (CN_STC | CN_CLC | CN_SC_12607)

#define CN_XFRIN            0100000u            /* (D) transfer direction is out of/into memory (0/1) */
#define CN_ADDRESS          0077777u            /* memory address mask (all but 12607, 15 bits) */
#define CN_ADDRESS_12607    0037777u            /* memory address mask (12607, 14 bits) */

#define CN_COUNT_12607      0017777u            /* word count mask (12607, 13 bits) */
#define CN_COUNT_12578      0037777u            /* word count mask (12578, 14 bits) */

static const BITSET_NAME dma_cw1_names [] = {   /* DMA control word 1 names */
    "STC",                                      /*   bit 15 */
    "byte packing",                             /*   bit 14 */
    "CLC"                                       /*   bit 13 */
    };

static const BITSET_FORMAT dma_cw1_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (dma_cw1_names, 13, msb_first, no_alt, append_bar) };


/* DMA global state declarations */

uint32 dma_request_set = 0;                     /* the channels that are requesting service */


/* DMA local state declarations */

typedef struct {
    FLIP_FLOP control;                          /* control flip-flop */
    FLIP_FLOP flag;                             /* flag flip-flop */
    FLIP_FLOP flag_buffer;                      /* flag buffer flip-flop */
    FLIP_FLOP select;                           /* register select flip-flop */
    uint32    xfer_sc;                          /* transfer enable flip-flop */

    HP_WORD   cw1;                              /* device select */
    HP_WORD   cw2;                              /* direction, address */
    HP_WORD   cw3;                              /* word count */
    uint8     packer;                           /* byte-packer holding register */
    t_bool    occupied;                         /* TRUE if packing register is occupied */
    } DMA_STATE;

static DMA_STATE dma [DMA_CHAN_COUNT];          /* per-channel state */


/* DMA I/O interface routine declarations */

static INTERFACE dma_interface;
static INTERFACE dmc_interface;


/* DMA local SCP support routine declarations */

static t_stat dma_reset (DEVICE *dptr);


/* DMA local utility routine declarations */

static void dma_cycle (CHANNEL chan, ACCESS_CLASS class);


/* DMA SCP data declarations */

/* Unit list */

static UNIT dma_unit [DMA_CHAN_COUNT] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (NULL,              0,         0)           },  /* channel 1 dummy unit */
    { UDATA (NULL,              0,         0)           }   /* channel 2 dummy unit */
    };


/* Device information blocks.

   Each DMA device uses two DIBs, corresponding to the two select codes assigned
   to each channel.  During I/O initialization, the DEVICE pointers for select
   codes 2 and 6 are both set to the "dma1_dev" device, while the DIB pointers
   are set to "dma1_dib [1]" and "dma1_dib [0]", respectively (and similarly for
   select codes 3 and 7).


   Implementation notes:

    1. The DIBs for each channel must be contained in a two-element array, as
       the DIB pointer for the lower select code is obtained by incrementing the
       pointer stored in the DEVICE structure that points to the DIB for the
       upper select code.
*/

static DIB dma1_dib [] = {                      /* DMA channel 1 (select code 6) */
    { &dma_interface,                           /*   the device's I/O interface function pointer */
      DMA1,                                     /*   the device's select code (02-77) */
      ch1,                                      /*   the card index */
      NULL,                                     /*   the card description */
      NULL },                                   /*   the ROM description */
                                                /* DMA channel 1 (select code 2) */
    { &dmc_interface,                           /*   the device's I/O interface function pointer */
      DMALT1,                                   /*   the device's select code (02-77) */
      ch1,                                      /*   the card index */
      NULL,                                     /*   the card description */
      NULL }                                    /*   the ROM description */
    };

static DIB dma2_dib [] = {                      /* DMA channel 2 (select code 7) */
    { &dma_interface,                           /*   the device's I/O interface function pointer */
      DMA2,                                     /*   the device's select code (02-77) */
      ch2,                                      /*   the card index */
      NULL,                                     /*   the card description */
      NULL },                                   /*   the ROM description */
                                                /* DMA channel 2 (select code 3) */
    { &dmc_interface,                           /*   the device's I/O interface function pointer */
      DMALT2,                                   /*   the device's select code (02-77) */
      ch2,                                      /*   the card index */
      NULL,                                     /*   the card description */
      NULL }                                    /*   the ROM description */
    };


/* Register lists */

static REG dma1_reg [] = {
/*    Macro   Name     Location               Width  Flags */
/*    ------  -------  ---------------------  -----  ----- */
    { ORDATA (XFR,     dma [ch1].xfer_sc,       6)         },
    { FLDATA (CTL,     dma [ch1].control,       0)         },
    { FLDATA (FLG,     dma [ch1].flag,          0)         },
    { FLDATA (FBF,     dma [ch1].flag_buffer,   0)         },
    { FLDATA (CTL2,    dma [ch1].select,        0)         },
    { ORDATA (CW1,     dma [ch1].cw1,          16)         },
    { ORDATA (CW2,     dma [ch1].cw2,          16)         },
    { ORDATA (CW3,     dma [ch1].cw3,          16)         },
    { FLDATA (BYTE,    dma [ch1].occupied,      0)         },
    { ORDATA (PACKER,  dma [ch1].packer,        8),  REG_A },
    { NULL }
    };

static REG dma2_reg [] = {
/*    Macro   Name     Location               Width  Flags */
/*    ------  -------  ---------------------  -----  ----- */
    { ORDATA (XFR,     dma [ch2].xfer_sc,       6)         },
    { FLDATA (CTL,     dma [ch2].control,       0)         },
    { FLDATA (FLG,     dma [ch2].flag,          0)         },
    { FLDATA (FBF,     dma [ch2].flag_buffer,   0)         },
    { FLDATA (CTL2,    dma [ch2].select,        0)         },
    { ORDATA (CW1,     dma [ch2].cw1,          16)         },
    { ORDATA (CW2,     dma [ch2].cw2,          16)         },
    { ORDATA (CW3,     dma [ch2].cw3,          16)         },
    { FLDATA (BYTE,    dma [ch2].occupied,      0)         },
    { ORDATA (PACKER,  dma [ch2].packer,        8),  REG_A },
    { NULL }
    };


/* Trace list */

static DEBTAB dma_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "SR",    TRACE_SR    },                   /* trace service requests received */
    { "DATA",  TRACE_DATA  },                   /* trace memory data accesses */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0 }
    };


/* Device descriptors */

DEVICE dma1_dev = {
    "DMA1",                                     /* device name */
    &dma_unit [ch1],                            /* unit array */
    dma1_reg,                                   /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &dma_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    dma1_dib,                                   /* device information block array */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    dma_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

DEVICE dma2_dev = {
    "DMA2",                                     /* device name */
    &dma_unit [ch2],                            /* unit array */
    dma2_reg,                                   /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &dma_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    dma2_dib,                                   /* device information block array */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    dma_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dma_dptrs [] = {
    &dma1_dev,
    &dma2_dev
    };



/* DMA I/O interface routines */


/* DMA interface (select codes 06 and 07).

   I/O operations directed to select code 6 for channel 1 or select code 7 for
   channel 2 configure Control Word 1 and start and stop DMA transfers.  Each
   channel has a transfer enable, a control, a flag, and a flag buffer
   flip-flop.  Transfer enable must be set via STC to start DMA.  The control
   flip-flop is used only to enable the DMA completion interrupt; it is set by
   STC and cleared by CLC.  The flag and flag buffer flip-flops are set at
   transfer completion to signal an interrupt.  STF may be issued to abort a
   transfer in progress, and SFS and SFC test whether a transfer is active.

   There are hardware differences between the various DMA cards.  The 12607B
   (2114) stores only bits 2-0 of the select code and interprets them as select
   codes 10-16 (SRQ17 is not decoded).  The 12578A (2115/16), 12895A (2100), and
   12897B (1000) support the full range of select codes (10-77 octal).  The
   12578A supports byte-sized transfers by setting bit 14.  Bit 14 is ignored by
   all other DMA cards, which support word transfers only.


   Implementation notes:

     1. An IOI reads the floating S-bus (high on the 1000, low on the 21xx).

     2. Asserting CRS resets the Control Word 2/3 select flip-flops.  Although
        the select flip-flops are controlled by the lower select code
        interfaces, CRS is asserted only to select codes 6 and up, so we reset
        the flip-flops here.

     3. The 12578A simulation uses a byte-packing/unpacking register to hold one
        byte while the other is read or written during the DMA cycle.

     4. The transfer enable flip-flop is simulated by the "xfer_sc" state
        variable, which holds the select code of the interface controlled by the
        DMA channel (i.e., set by Control Word 1), or the value 100000 octal if
        the channel is inactive.  These values correspond to the transfer enable
        flip-flop being set or cleared, respectively.  This implementation
        permits a fast activity check when an interface asserts SRQ, which
        virtually all interfaces do regardless of whether or not they are under
        DMA control.  The alternative is to test that the transfer enable
        flip-flop is set and then that Control Word 1 masked just to the select
        code matches that of the interface asserting SRQ.

     5. The transfer enable flip-flop will not set if the flag buffer flip-flop
        is set; the latter asserts an asynchronous clear to the former.  In
        hardware, the STC and CLF signals assert concurrently, so transfer
        enable will set when the flag buffer is cleared asynchronously.  In
        simulation, these signals are processed sequentially, so we must test
        for concurrent CLF assertion in the STC handler.

     6. When starting a DMA transfer, we must assert SIR to the target interface
        to see if SRQ is already asserted and therefore to set the appropriate
        channel bit in "dma_request_set".  This is required because the
        interface may assert SRQ before DMA is started, which will NOT set the
        channel request bit if the transfer enable flip-flop is clear.
*/

static SIGNALS_VALUE dma_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const  CHANNEL ch = (CHANNEL) dibptr->card_index;       /* the DMA channel number */
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            dma [ch].flag_buffer = CLEAR;               /* reset the flag buffer */
            dma [ch].flag        = CLEAR;               /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            dma [ch].flag_buffer = SET;                 /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (dma [ch].flag_buffer == SET) {          /* if the flag buffer flip-flop is set */
                dma [ch].flag = SET;                    /*   then set the flag flip-flop */

                if (dma [ch].xfer_sc <= SC_MAX) {       /* if the channel is active */
                    dma [ch].xfer_sc = D16_SIGN;        /*   then clear transfer enable to stop the transfer */

                    tpprintf (dma_dptrs [ch], TRACE_CMD, "Channel transfer %s\n",
                              (dma [ch].cw3 == 0 ? "completed" : "aborted"));
                    }

                dma_request_set &= ~TO_REQ (ch);        /* clear any pending channel service request */
                }
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (dma [ch].flag == CLEAR)                 /* if a transfer is in progress */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (dma [ch].flag == SET)                   /* if transfer is complete */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O Data Input */
            if (cpu_configuration & CPU_1000)           /* if the CPU is a 1000-series machine */
                outbound.value = D16_UMAX;              /*   then return all ones */
            else                                        /* otherise  */
                outbound.value = 0;                     /*   return all zeros for the other models */
            break;


        case ioIOO:                                                 /* I/O Data Output */
            if (cpu_configuration & CPU_2114)                       /* if this is a 12607 */
                dma [ch].cw1 = inbound_value & CN_12607_MASK | 010; /*   then convert to SC 0-6 to 10-16 */
            else if (cpu_configuration & (CPU_2115 | CPU_2116))     /* otherwise if this is a 12578 */
                dma [ch].cw1 = inbound_value;                       /*   then store the control word verbatim */
            else                                                    /* otherwise */
                dma [ch].cw1 = inbound_value & ~CN_PACK;            /*   remove the byte-packing flag */

            tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 1 is %sselect code %02o\n",
                      fmt_bitset (inbound_value, dma_cw1_format), inbound_value & CN_SC);
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            dma [ch].flag_buffer = SET;                 /* set the flag buffer flip-flop */
            break;


        case ioCRS:                                     /* Control Reset */
            dma [ch].control = CLEAR;                   /* clear the control flip-flop */
            dma [ch].select  = CLEAR;                   /*   and the control word select flip-flop */

            dma [ch].xfer_sc = D16_SIGN;                /* clear transfer enable to abort any in-progress transfer */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            dma [ch].control = CLEAR;                   /* clear the control flip-flop */

            if (dma [ch].xfer_sc <= SC_MAX)             /* if the channel is active */
                tpprintf (dma_dptrs [ch], TRACE_CMD, "Channel completion interrupt is inhibited\n");
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            dma [ch].control = SET;                     /* set the control flip-flop */

            dma [ch].packer = 0;                        /* clear the packing register */
            dma [ch].occupied = FALSE;                  /*   and the occupied flag */

            if (dma [ch].flag_buffer == CLEAR               /* if the flag buffer is clear */
              || inbound_signals & ioCLF) {                 /*   or will be cleared in this cycle */
                dma [ch].xfer_sc = dma [ch].cw1 & CN_SC;    /*     then set the transfer enable flip-flop */

                if (dma [ch].cw2 & CN_XFRIN)                /* if this is an input transfer */
                    tpprintf (dma_dptrs [ch], TRACE_CMD,
                              "Channel transfer of %u words from select code %02o to address %05o started\n",
                              NEG16 (dma [ch].cw3), dma [ch].cw1 & CN_SC, dma [ch].cw2 & LA_MASK);
                else                                        /* otherwise it's an output transfer */
                    tpprintf (dma_dptrs [ch], TRACE_CMD,
                              "Channel transfer of %u words from address %05o to select code %02o started\n",
                              NEG16 (dma [ch].cw3), dma [ch].cw2 & LA_MASK, dma [ch].cw1 & CN_SC);

                io_dispatch (dma [ch].xfer_sc, ioSIR, 0);   /* update the target interface's SRQ state */
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (dma [ch].control & dma [ch].flag)       /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (dma [ch].control & dma [ch].flag        /* if the control and flag */
              & dma [ch].flag_buffer)                   /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            dma [ch].flag_buffer = CLEAR;               /* clear the flag buffer flip-flop */
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


/* DMA configuration interface (select codes 02 and 03).

   I/O operations directed to select code 2 for channel 1 or select code 3 for
   channel 2 configure Control Words 2 and 3.  CLC and STC manipulate the
   register select flip-flop, which determines whether IOO writes to the
   transfer address (CW2) or word count (CW3) registers, respectively.  IOI
   reads the current content of the word count register.  There are no control,
   flag, or flag buffer flip-flops for these select codes, and CLF, STF, SFC,
   and SFS are ignored.

   There are hardware differences in the implementations of the memory address
   and word count registers among the various cards.  The 12607B (2114) supports
   14-bit addresses and 13-bit word counts.  The 12578A (2115/6) supports 15-bit
   addresses and 14-bit word counts.  The 12895A (2100) and 12897B (1000)
   support 15-bit addresses and 16-bit word counts.


   Implementation notes:

    1. Because the I/O bus floats to zero on 211x computers, an IOI (read word
       count) returns zeros in the unused bit locations, even though the word
       count itself is a negative value.

    2. Select codes 2 and 3 cannot interrupt, so there is no SIR handler.
*/

static SIGNALS_VALUE dmc_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const  CHANNEL ch = (CHANNEL) dibptr->card_index;       /* the DMA channel number */
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLC:                                     /* Clear Control flip-flop */
            dma [ch].select = CLEAR;                    /* select the word count register */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            dma [ch].select = SET;                      /* select the memory address register */
            break;


        case ioIOI:                                             /* I/O Data Input */
            if (cpu_configuration & CPU_2114)                   /* if this is a 12607 */
                outbound.value = dma [ch].cw3 & CN_COUNT_12607; /*   then return only 13 bits of the count */
            else if (cpu_configuration & (CPU_2115 | CPU_2116)) /* otherwise if this is a 12578 */
                outbound.value = dma [ch].cw3 & CN_COUNT_12578; /*   then return only 14 bits of the count */
            else                                                /* otherwise */
                outbound.value = dma [ch].cw3;                  /*   return the full value of the count */

            tpprintf (dma_dptrs [ch], TRACE_CSRW, "Remaining word count is %u\n",
                      NEG16 (dma [ch].cw3));
            break;


        case ioIOO:                                     /* I/O Data Output */
            if (dma [ch].select) {                      /* if the word count register is selected */
                dma [ch].cw3 = inbound_value;           /*   then save the (negative) count */

                tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 3 is word count %u\n",
                          NEG16 (dma [ch].cw3));
                }

            else {                                                      /* otherwise the address register is selected */
                if (cpu_configuration & CPU_2114)                       /* if this is a 12607 */
                    dma [ch].cw2 = inbound_value                        /*   then store only 14 bits of the address */
                                     & (CN_XFRIN | CN_ADDRESS_12607);   /*     while preserving the transfer direction */
                else                                                    /* otherwise */
                    dma [ch].cw2 = inbound_value;                       /*   store the full address */

                tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 2 is %s address %05o\n",
                          (dma [ch].cw2 & CN_XFRIN ? "input to" : "output from"),
                          dma [ch].cw2 & LA_MASK);
                }
            break;


        case ioPRH:                                     /* Priority High */
            outbound.signals |= ioPRL;                  /* assert PRL */
            break;


        case ioSTF:                                     /* not used by this interface */
        case ioCLF:                                     /* not used by this interface */
        case ioSFS:                                     /* not used by this interface */
        case ioSFC:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioPOPIO:                                   /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
        case ioIAK:                                     /* not used by this interface */
        case ioENF:                                     /* not used by this interface */
        case ioIEN:                                     /* not used by this interface */
        case ioSIR:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}



/* DMA global utility routines */


/* Configure DMA for one or two channels.

   This routine configures DMA for the specific card being simulated, based on
   the CPU model currently selected.  The 12607B, which is used with the 2114
   CPU, has a single DMA channel.  All other CPUs use cards that have two
   channels.

   On entry, the routine adds or removes the "device can be disabled" and
   "device is currently enabled" flags from the DEVICE structure for DMA channel
   2, depending on whether or not the current CPU model is a 2114.  This ensures
   that the user is restricted to configurations that were actually supported on
   the current CPU.

   In addition, if the CPU is a 1000, it assigns the logical names "DCPC1" and
   "DCPC2" to the two DMA channels.  This allows 1000-series users to refer to
   the channels using the HP-preferred device names (i.e., "Dual-Channel Port
   Controller").


   Implementation notes:

    1. It is OK to deassign the logical name from a device even if one has not
       been assigned yet, as the "deassign_device" routine protects against
       this.  Therefore, it is not necessary to check if the name exists first.
       However, assigning a logical name does not check first, so we must ensure
       that it has not been assigned before setting the new name.

    2. As this routine is called during a CPU model change, we unconditionally
       enable DMA channel 1 (and channel 2, if not a 2114), so that setting the
       CPU model starts with a known device configuration.
*/

void dma_configure (void)
{
dma1_dev.flags &= ~DEV_DIS;                             /* enable DMA channel 1 */

if (cpu_configuration & CPU_2114)                       /* if the current CPU is a 2114 */
    dma2_dev.flags = dma2_dev.flags & ~DEV_DISABLE      /*   then make channel 2 unalterable */
                        | DEV_DIS;                      /*     and disable it */
else                                                    /* otherwise */
    dma2_dev.flags = dma2_dev.flags & ~DEV_DIS          /*   enable channel 2 */
                       | DEV_DISABLE;                   /*     and make it alterable */

if (cpu_configuration & CPU_1000) {                     /* if the current CPU family is 1000 */
    if (dma1_dev.lname == NULL) {                       /*   then if the logical names have not been set */
        assign_device (&dma1_dev, "DCPC1");             /*     then change the device names */
        assign_device (&dma2_dev, "DCPC2");             /*       from DMA to DCPC for familiarity */
        }
    }

else {                                                  /* otherwise the current model is 21xx */
    deassign_device (&dma1_dev);                        /*   so delete the DCPC names */
    deassign_device (&dma2_dev);                        /*     to restore the original DMA names */
    }

return;
}


/* Assert a DMA service request.

   This routine is called to assert the SRQ signal for a specified interface to
   the DMA device.  Interfaces typically assert SRQ when their flag flip-flops
   are set.  SRQ is asserted regardless of whether or not DMA is active for the
   interface.  In simulation, this routine is called when any interface returns
   SRQ and takes action only if DMA is actively controlling the interface.
   Otherwise, it returns with no action taken.

   On entry, "select_code" contains the select code of the interface asserting
   SRQ.  If either DMA channel is currently controlling the interface, the
   corresponding channel service request is set; otherwise, the routine simply
   returns.  On the next pass through the instruction execution loop, the
   request will be serviced by initiating a DMA cycle.
*/

void dma_assert_SRQ (uint32 select_code)
{
if (select_code == dma [ch1].xfer_sc) {                 /* if DMA channel 1 controls this device */
    dma_request_set |= DMA_1_REQ;                       /*   then request service for channel 1 */

    tprintf (dma1_dev, TRACE_SR, "Select code %02o asserted SRQ\n",
             dma [ch1].cw1 & CN_SC);
    }

if (select_code == dma [ch2].xfer_sc) {                 /* if DMA channel 2 controls this device */
    dma_request_set |= DMA_2_REQ;                       /*   then request service for channel 2 */

    tprintf (dma2_dev, TRACE_SR, "Select code %02o asserted SRQ\n",
             dma [ch2].cw1 & CN_SC);
    }

return;
}


/* Service DMA requests.

   This routine is called to initiate DMA cycles on one or both channels.  It is
   called as part of the instruction execution loop whenever a DMA request is
   pending.

   In hardware, the two DMA channels contend independently for memory and I/O
   cycles, with channel 1 having priority over channel 2 if they both request
   cycles concurrently (i.e., if both controlled devices assert SRQ
   concurrently).  In simulation, we process a channel 1 request and then, if
   channel 1 is NOT requesting but channel 2 is, we process the channel 2
   request.  If, after servicing, channel 1 immediately requests another DMA
   cycle, any pending channel 2 request is held off until channel 1 is serviced
   again.  This allows channel 1 to steal all available memory cycles as long as
   SRQ is continuously asserted.

   With properly designed interface cards, DMA is capable of taking consecutive
   I/O cycles.  On all machines except the 1000 M-Series, a DMA cycle freezes
   the CPU for the duration of the cycle.  On the M-Series, a DMA cycle freezes
   the CPU if it attempts an I/O cycle (including IAK) or a directly-interfering
   memory cycle.  An interleaved memory cycle is allowed.  Otherwise, the
   control processor is allowed to run.  Therefore, during consecutive DMA
   cycles, the M-Series CPU will run until an IOG instruction is attempted,
   whereas the other CPUs will freeze completely.  This is simulated by skipping
   instruction execution if "dma_request_set" is still non-zero after servicing
   the current request, i.e., if the device asserted SRQ again as a result of
   the DMA cycle.

   Most I/O cards assert SRQ no more than 50% of the time.  A few buffered
   cards, such as the 12821A and 13175A Disc Interfaces, are capable of
   asserting SRQ continuously while filling or emptying the buffer.  If SRQ for
   channel 1 is asserted continuously when both channels are active, then no
   channel 2 cycles will occur until channel 1 completes.
*/

void dma_service (void)
{
if (dma_request_set & DMA_1_REQ)                        /* if the request is for channel 1 */
    dma_cycle (ch1, DMA_Channel_1);                     /*   then do one DMA cycle using the port A map */

if ((dma_request_set & (DMA_1_REQ | DMA_2_REQ)) == DMA_2_REQ)   /* if channel 1 is idle and channel 2 is requesting */
    dma_cycle (ch2, DMA_Channel_2);                             /*   then do one DMA cycle using the port B map */

return;
}



/* DMA local SCP support routines */


/* Reset DMA.

   This routine is called for a RESET, RESET DMAn, RUN, or BOOT command.  It is
   the simulation equivalent of an initial power-on condition (corresponding to
   PON, POPIO, and CRS signal assertion) or a front-panel PRESET button press
   (corresponding to POPIO and CRS assertion).  SCP delivers a power-on reset to
   all devices when the simulator is started.
*/


static t_stat dma_reset (DEVICE *dptr)
{
const DIB *dibptr = (DIB *) dptr->ctxt;                 /* the DIB pointer */
const CHANNEL ch = (CHANNEL) dibptr->card_index;        /* the DMA channel number */

if (! (cpu_configuration & CPU_2114))                       /* if this is not a 12607 */
    hp_enbdis_pair (dma_dptrs [ch], dma_dptrs [ch ^ 1]);    /*   then make the two channels consistent */

if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    dma [ch].cw1 = 0;                                   /*   then clear */
    dma [ch].cw2 = 0;                                   /*     the control */
    dma [ch].cw3 = 0;                                   /*       word registers */
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

dma [ch].packer = 0;                                    /* clear the packing register */
dma [ch].occupied = FALSE;                              /*   and the occupied flag */

return SCPE_OK;
}



/* DMA local utility routines */


/* Execute a DMA cycle.

   This routine performs one DMA input or output cycle using the indicated DMA
   channel number and DMS map.  When the transfer word count reaches zero, the
   flag is set on the corresponding DMA channel to indicate completion.

   The 12578A card supports byte-packing.  If bit 14 in Control Word 1 is set,
   each transfer will involve one read/write from memory and two output/input
   operations in order to transfer sequential bytes to/from the device.

   DMA I/O cycles differ from programmed I/O cycles in that multiple I/O control
   backplane signals may be asserted simultaneously.  With programmed I/O, only
   CLF may be asserted with other signals, specifically with STC, CLC, SFS, SFC,
   IOI, or IOO.  With DMA, as many as five signals may be asserted concurrently.

   DMA I/O timing looks like this:

           ------------ Input ------------   ----------- Output ------------
     Sig    Normal Cycle      Last Cycle      Normal Cycle      Last Cycle
     ===   ==============   ==============   ==============   ==============
     IOI   T2-T3            T2-T3
     IOO                                        T3-T4            T3-T4
     STC *    T3                                T3               T3
     CLC *                     T3-T4                             T3-T4
     CLF      T3                                T3               T3
     EDT                          T4                                T4

      * if enabled by control word 1

   Under simulation, this routine dispatches one set of I/O signals per DMA
   cycle to the target device's I/O interface.  The signals correspond to the
   table above, except that all signals for a given cycle are concurrent (e.g.,
   the last input cycle has IOI, EDT, and optionally CLC asserted, even though
   IOI and EDT are not coincident in hardware).  The I/O interfaces will process
   these signals sequentially, in the order listed above, before returning.

   On entry, "ch" indicates the DMA channel that has received a service request
   from the controlled interface, and "class" indicates the memory access
   classification to use when reading from or writing to memory.  The routine
   first determines the set of I/O signals to assert to the interface, as
   described above.  Then an I/O cycle and a memory cycle are performed to
   transfer data (the memory cycle is skipped if byte packing is enabled and
   the byte to transfer either resides in or is to be saved in the byte packing
   register).  Finally, unless the first byte of a byte transfer was just
   made, the address is incremented and the word count is decremented; if the
   word count is zero, the DMA channel flag is set to terminate the transfer.


   Implementation notes:

    1. The address increment and word count decrement is done only after the I/O
       cycle has completed successfully.  This allows a failed transfer to be
       retried after correcting the I/O error.
*/

static void dma_cycle (CHANNEL ch, ACCESS_CLASS class)
{
const uint32  selcode = dma [ch].cw1 & CN_SC;           /* the device select code */
const uint32  packing = dma [ch].cw1 & CN_PACK;         /* the packing bytes flag */
const uint32  input   = dma [ch].cw2 & CN_XFRIN;        /* the input flag */
const HP_WORD MA      = dma [ch].cw2 & CN_ADDRESS;      /* the memory address */
HP_WORD       data;
SKPF_DATA     result;
INBOUND_SET   signals;

dma_request_set &= ~TO_REQ (ch);                        /* clear the channel service request */

if (dma [ch].cw3 != D16_UMAX                            /* if this is a normal (not last) cycle */
  || packing && !dma [ch].occupied) {                   /*   or it's the first of two byte packing cycles */
    if (input)                                          /*     then if this is an input cycle */
        signals = ioIOI | ioCLF | ioSIR;                /*       then assert IOI and CLF */
    else                                                /*     otherwise */
        signals = ioIOO | ioCLF | ioSIR;                /*       assert IOO and CLF */

    if (dma [ch].cw1 & CN_STC)                          /* if STC is wanted */
        signals |= ioSTC;                               /*   then assert STC */
    }

else {                                                  /* otherwise this is the last cycle */
    if (input)                                          /*   so if it's an input cycle */
        signals = ioIOI | ioEDT;                        /*     then assert IOI and EDT */

    else {                                              /*   otherwise */
        signals = ioIOO | ioCLF | ioEDT | ioSIR;        /*     assert IOO and CLF and EDT */

        if (dma [ch].cw1 & CN_STC)                      /* if STC is wanted */
            signals |= ioSTC;                           /*   then assert STC */
        }

    if (dma [ch].cw1 & CN_CLC)                          /* if CLC is wanted on the last cycle */
        signals |= ioCLC | ioSIR;                       /*   then assert CLC */
    }


if (input) {                                                /* if this is an input cycle */
    result = io_dispatch (selcode, signals, 0);             /*   then read a byte or word from the interface */

    data = result.data;                                     /* extract the returned data value */

    if (packing) {                                          /* if byte packing is enabled */
        if (dma [ch].occupied) {                            /*   then if this is the second byte */
            data = TO_WORD (dma [ch].packer, data);         /*     then merge the stored byte */
            mem_write (dma_dptrs [ch], class, MA, data);    /*       and write the data word to memory */
            }

        else                                                /*   otherwise it is the first byte */
            dma [ch].packer = LOWER_BYTE (data);            /*     so save it for later packing */

        dma [ch].occupied = ! dma [ch].occupied;            /* flip the packing register occupation state */
        }

    else                                                    /* otherwise we are doing word transfers */
        mem_write (dma_dptrs [ch], class, MA, data);        /*   so write the data word to memory */
    }

else {                                                      /* otherwise this is an output cycle */
    if (packing) {                                          /*   so if byte packing is enabled */
        if (dma [ch].occupied)                              /*     then if this is the second byte */
            data = dma [ch].packer;                         /*       then retrieve it */

        else {                                              /*   otherwise this is the first byte */
            data = mem_read (dma_dptrs [ch], class, MA);    /*     so read the data word from memory */

            dma [ch].packer = LOWER_BYTE (data);            /* save the second byte in the packing register */
            data = UPPER_BYTE (data);                       /*   and send the first byte to the interface */
            }

        dma [ch].occupied = ! dma [ch].occupied;            /* flip the packing register occupation state */
        }

    else                                                    /*   otherwise we are doing word transfers */
        data = mem_read (dma_dptrs [ch], class, MA);        /*     so read the data word from memory */

    result = io_dispatch (selcode, signals, data);          /* output the byte or word to the interface */
    }


if (! (packing && dma [ch].occupied)) {                     /* if this is not the first byte of a byte transfer */
    dma [ch].cw2 = input | dma [ch].cw2 + 1 & CN_ADDRESS;   /*   then increment the address part of CW2 */
    dma [ch].cw3 = dma [ch].cw3 + 1 & D16_MASK;             /*     and the (negative) word count */

    if (dma [ch].cw3 == 0) {                                /* if the transfer is complete */
        dma [ch].flag_buffer = SET;                         /*   then set the DMA channel flag buffer */
        io_assert (dma_dptrs [ch], ioa_ENF);                /*     and the flag */
        }
    }

return;
}
