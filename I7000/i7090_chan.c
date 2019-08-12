/* i7090_chan.c: IBM 7090 Channel simulator

   Copyright (c) 2005-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   channel

   The system state for the IBM 7090 channel is:
   There are 4 types of channel:
        704:            Basic polled mode transfer. Channel only manages
                        status and disconnect of devices.
       7607:            Basic channel.
       7909:            Enhanced channel for disk, hypertape and com controlers.
       7289:            Special CTSS channel, like 7607, but first command
                        is drum address.

   Common registers to all but 704 channels.
   ADDR<0:16>           Address of next command.
   CMD<0:6>             Channel command.
   WC<0:15>             Word count remaining.
   ASM<0:35>            Assembled data from devices.
   LOCATION<0:16>       Location to read or write next word from.

   7909 adds following two registers.
   SMS<0:6>             Select register.
   COUNT<0:6>           Counter.

   Simulation registers to handle device handshake.
   STATUS<0:16>         Simulated register for basic channel status.
   SENSE<0:16>          Additional flags for 7909 channels.
*/

#include "i7090_defs.h"

extern uint16       iotraps;
extern uint8        iocheck;
extern uint8        dualcore;
extern UNIT         cpu_unit;
extern uint16       IC;
extern t_uint64     MQ;
extern uint32       drum_addr;
extern t_uint64     hsdrm_addr;

t_stat              chan_reset(DEVICE * dptr);
void                chan_fetch(int chan);
t_stat              chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *chan_description (DEVICE *dptr);
void                chan9_seqcheck(int chan);
uint32              dly_cmd(UNIT *, uint16, uint16);

/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

uint16              caddr[NUM_CHAN];          /* Channel memory address */
uint8               bcnt[NUM_CHAN];           /* Channel character count */
uint8               cmd[NUM_CHAN];            /* Current command */
uint16              wcount[NUM_CHAN];         /* Word count */
t_uint64            assembly[NUM_CHAN];       /* Assembly register */
uint16              location[NUM_CHAN];       /* Pointer to next opcode */
uint32              chan_flags[NUM_CHAN];    /* Unit status */
uint16              chan_info[NUM_CHAN];      /* Private channel info */
uint8               counter[NUM_CHAN];        /* Channel counter */
uint8               sms[NUM_CHAN];            /* Channel mode infomation */
uint8               chan_irq[NUM_CHAN];       /* Channel has a irq pending */

/* 7607 channel commands */
#define IOCD    000
#define TCH     010
#define IORP    020
#define IORT    030
#define IOCP    040
#define IOCT    050
#define IOSP    060
#define IOST    070

/* 7909 channel commands */
#define WTR     000
#define WTRX    004
#define XMT     001
#define XMTX    005
#define TCH9    010
#define TCHX    014
#define LIPT    011
#define LIPTX   015
#define CTL     020
#define CTLR    021
#define CTLW    024
#define SNS     025
#define LAR     030
#define SAR     031
#define TWT     034
#define XXXX    035
#define CPYP    040
#define CPYP2   041
#define CPYP3   044
#define CPYP4   045
#define CPYD    050
#define TCM     051
#define CPYDX   054
#define TCMX    055
#define XXXZ    060
#define LIP     061
#define TDC     064
#define LCC     065
#define SMS     070
#define ICC     071
#define ICCX    075

/* Values for chan_info */
#define CHAINF_START    1       /* Channel started */
#define CHAINF_RUN      2       /* Transfer in progress */

#define nxt_chan_addr(chan) caddr[chan] = \
                        ((dualcore) ? (0100000 & caddr[chan]) : 0) | \
                                ((caddr[chan] + 1) & MEMMASK);

/* Globally visible flags */

const char     *chan_type_name[] = {
    "Polled", "Unit Record", "7607", "7909", "7289"};

/* Delay device for IOD instruction */
DIB                 dly_dib =
    { CH_TYP_PIO, 1, 0333, 07777, &dly_cmd, NULL };


UNIT                chan_unit[] = {
    /* Puesdo channel for 704 devices */
    {UDATA(NULL, UNIT_DISABLE | CHAN_SET |
                        CHAN_S_TYPE(CHAN_PIO)|UNIT_S_CHAN(0), 0)},
    /* Normal channels */
#if NUM_CHAN > 1
    {UDATA(NULL, CHAN_AUTO | CHAN_SET | CHAN_S_TYPE(CHAN_7607)|
                                        UNIT_S_CHAN(CHAN_A), 0)},       /* A */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_B), 0)},     /* B */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_C), 0)},     /* C */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_D), 0)},     /* D */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_E), 0)},     /* E */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_F), 0)},     /* F */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_G), 0)},     /* G */
    {UDATA(NULL, UNIT_DISABLE | CHAN_AUTO|UNIT_S_CHAN(CHAN_H), 0)}      /* H */
#endif
};

REG                 chan_reg[] = {
    {BRDATA(ADDR, caddr, 8, 16, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(CMD, cmd, 8, 6, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(WC, wcount, 8, 15, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(ASM, assembly, 8, 36, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(LOCATION, location, 8, 16, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(FLAGS, chan_flags, 2, 32, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(COUNTER, counter, 8, 6, NUM_CHAN), REG_RO|REG_FIT },
    {BRDATA(SMS,  sms, 2, 6, NUM_CHAN), REG_RO|REG_FIT},
    {NULL}
};

MTAB                chan_mod[] = {
#ifdef I7090
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_PIO), "704 Channel", NULL, NULL, NULL, NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7607), "7607", "7607", NULL, NULL, NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7909), "7909", "7909", NULL, NULL, NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7289), "7289", "7289", NULL, NULL, NULL},
    {CHAN_AUTO, 0, "FIXED", "FIXED", NULL, NULL, NULL},
    {CHAN_AUTO, CHAN_AUTO, "AUTO", "AUTO", NULL, NULL, NULL},
    {CHAN_SET, CHAN_SET, "set", NULL, NULL, NULL, NULL},
    {MTAB_VUN, 0,  "Units",  NULL, NULL, &print_chan, NULL},
#endif
    {0}
};

/* Simulator debug controls */
DEBTAB              chn_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"TRAP", DEBUG_TRAP},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"SENSE", DEBUG_SNS},
    {"CH0", 0x0100 << 0},
    {"CHA", 0x0100 << 1},
    {"CHB", 0x0100 << 2},
    {"CHC", 0x0100 << 3},
    {"CHD", 0x0100 << 4},
    {"CHE", 0x0100 << 5},
    {"CHF", 0x0100 << 6},
    {"CHG", 0x0100 << 7},
    {"CHH", 0x0100 << 8},
    {0, 0}
};

DEVICE              chan_dev = {
    "CH", chan_unit, chan_reg, chan_mod,
    NUM_CHAN, 8, 15, 1, 8, 36,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    &dly_dib, DEV_DEBUG, 0, chn_debug,
    NULL, NULL, &chan_help, NULL, NULL, &chan_description
};


/* Nothing special to do, just return true if cmd is write and we got here */
uint32 dly_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    if (cmd == IO_WRS)
        return SCPE_OK;
    return SCPE_NODEV;
}



t_stat
chan_reset(DEVICE * dptr)
{
    int                 i;

    /* Clear channel assignment */
    for (i = 0; i < NUM_CHAN; i++) {
        if (chan_unit[i].flags & CHAN_AUTO)
            chan_unit[i].flags &= ~CHAN_SET;
        else
            chan_unit[i].flags |= CHAN_SET;
        chan_flags[i] = 0;
        chan_info[i] = 0;
        caddr[i] = 0;
        cmd[i] = 0;
        sms[i] = 0;
        bcnt[i] = 6;
        chan_irq[i] = 0;
        wcount[i] = 0;
        location[i] = 0;
        counter[i] = 0;
    }
    return chan_set_devs(dptr);
}

/* Boot from given device */
t_stat
chan_boot(int32 unit_num, DEVICE * dptr)
{
    /* Tell device to do a read, 3 records */
    /* Set channel address = 0, wc = 3, location = 0, CMD=0 */
    /* Set M[1] = TCO? 1, IC = 1 */
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags);

    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO) {
        IC = 0;
    } else {
        IC = 1;
        /* Grab next channel command */
        location[chan] = 0;
        chan_fetch(chan);
    }
    chan_flags[chan] |= STA_ACTIVE;
    chan_flags[chan] &= ~STA_PEND;
    return SCPE_OK;
}

/* Preform BCD to binary translation for 7909 channel */
void
bcd_xlat(int chan, int direction)
{
    int                 i;
    t_uint64            na = 0;

    for (i = 30; i >= 0; i -= 6) {
        uint8               ch = (uint8)(assembly[chan] >> i) & 077;

        if (direction) {        /* D->M Read */
            switch (ch & 060) {
            case 000:
                if (ch == 0)
                    ch = 060;
                else if (ch == 012)
                    ch = 0;
                break;
            case 020:
            case 060:
                ch ^= 040;
            case 040:
                break;
            }
        } else {                /* M->D Write */
            switch (ch & 060) {
            case 000:
                if (ch == 0)
                    ch = 012;
                else if (ch == 012)
                    ch = 020;
                break;
            case 060:
                if (ch == 060)
                    ch = 060;   /* => 000 */
            case 020:
                ch ^= 040;
            case 040:
                break;
            }
        }
        na |= ((t_uint64) ch) << i;
    }
    assembly[chan] = na;
}

/* Execute the next channel instruction. */
void
chan_proc()
{
    int                 chan;
    int                 cmask;

    /* Scan channels looking for work */
    for (chan = 0; chan < NUM_CHAN; chan++) {
        /* Skip if channel is disabled */
        if (chan_unit[chan].flags & UNIT_DIS)
            continue;

        /* If channel is disconnecting, do nothing */
        if (chan_flags[chan] & DEV_DISCO)
            continue;

        cmask = 0x0100 << chan;
        switch (CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_PIO:
            if ((chan_flags[chan] & (DEV_REOR|DEV_SEL|DEV_FULL)) ==
                        (DEV_SEL|DEV_REOR))  {
                sim_debug(DEBUG_DETAIL, &chan_dev, "chan got EOR\n");
                chan_flags[chan] |= (DEV_DISCO);
            }

            break;
#ifdef I7090
        case CHAN_7289: /* Special channel for HS drum */
            /* On first command, copy it to drum address and load another */
            if ((chan_info[chan] & (CHAINF_RUN | CHAINF_START)) ==
                CHAINF_START) {
                hsdrm_addr = M[location[chan] - 1];
                chan_info[chan] |= CHAINF_RUN;
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d HDaddr %012llo\n",
                              chan, hsdrm_addr);
                chan_fetch(chan);
                continue;
            }
            if ((chan_info[chan] & CHAINF_START) == 0)
                continue;
            /* Fall through and behave like 7607 from now on */
        case CHAN_7607:
            /* If no select, stop channel */
            if ((chan_flags[chan] & DEV_SEL) == 0
                && (chan_flags[chan] & STA_TWAIT)) {
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_TRAP, &chan_dev, "chan %d Trap\n",
                              chan);
                iotraps |= 1 << chan;
                chan_flags[chan] &=
                    ~(STA_START | STA_ACTIVE | STA_WAIT | STA_TWAIT);
                chan_info[chan] = 0;
                continue;
            }

            /* If device requested attention, abort current command */
            if (chan_flags[chan] & CHS_ATTN) {
                if (chan_flags[chan] & DEV_SEL)
                    chan_flags[chan] |= (DEV_DISCO);
                chan_flags[chan] &=
                    ~(CHS_ATTN | STA_START | STA_ACTIVE | STA_WAIT);
                chan_info[chan] = 0;
                switch(cmd[chan]) {
                case IORT:
                case IOCT:
                case IOST:
                        iotraps |= 1 << chan;
                        break;
                }
                if (chan_dev.dctrl & cmask)
                     sim_debug(DEBUG_DETAIL, &chan_dev,
                        "chan %d attn< %o\n", chan, cmd[chan] & 070);
                continue;
            }

            /* If we are waiting and get EOR, then continue along */
            if ((chan_flags[chan] & (STA_WAIT|DEV_REOR|DEV_FULL)) ==
                        (STA_WAIT|DEV_REOR))  {
                chan_flags[chan] &= ~(STA_WAIT|DEV_WEOR);
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d clr wait EOR\n",
                                 chan);
            }

            /* All done if waiting for EOR */
            if (chan_flags[chan] & STA_WAIT)
                continue;

            /* No activity, nothing happening here folks, move along */
            if ((chan_flags[chan] & (STA_ACTIVE | STA_WAIT)) == 0) {
                /* Check if Trap wait and no pending LCHx, force disconnect */
                if ((chan_flags[chan] & (STA_TWAIT|STA_PEND|DEV_SEL))
                         == (STA_TWAIT|DEV_SEL))
                    chan_flags[chan] |= DEV_DISCO|DEV_WEOR;
                continue;
            }

            /* If command is a transfer, Do transfer */
            if ((cmd[chan] & 070) == TCH) {
                location[chan] = caddr[chan];
                chan_fetch(chan);
                /* Give up bus if next command is a tranfer. */
                if ((cmd[chan] & 070) == TCH)
                    continue;
            }

            /* None disabled, active channel is if transfering */
            switch (chan_flags[chan] & (DEV_WRITE | DEV_FULL)) {
                /* Device has given us a dataword */
            case DEV_FULL:
                /* If we are not waiting EOR save it in memory */
                if ((cmd[chan] & 1) == 0) {
                    if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_DATA, &chan_dev, "chan %d data < %012llo\n",
                               chan, assembly[chan]);
                    M[caddr[chan]] = assembly[chan];
                } else {
                    if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_DATA, &chan_dev, "chan %d data * %012llo\n",
                               chan, assembly[chan]);
                }
                nxt_chan_addr(chan);
                assembly[chan] = 0;
                bcnt[chan] = 6;
                wcount[chan]--;
                chan_flags[chan] &= ~DEV_FULL;

                /* Device does not need a word and has not given us one */
            case 0:
                /* Device idle, expecting data from it */

                /* Check if got EOR */
                if (chan_flags[chan] & DEV_REOR) {
                    switch (cmd[chan] & 070) {
                    case IORP:
                    case IOSP:
                        chan_flags[chan] &= ~(DEV_REOR|DEV_WEOR/* | STA_WAIT*/);
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                         "chan %d EOR< %o\n",
                                  chan, cmd[chan] & 070);
                        chan_fetch(chan);
                        chan_flags[chan] |= STA_ACTIVE;
                        continue;       /* Handle new command next time */
                    case IORT:
                    case IOST:
                        chan_flags[chan] &= ~(DEV_REOR|DEV_WEOR);
                        chan_flags[chan] &= ~(STA_ACTIVE/*|STA_WAIT*/);
                        chan_flags[chan] |= STA_TWAIT;
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                        "chan %d EOR< %o\n",
                                        chan, cmd[chan] & 070);
                        continue;
                    }
                }

                /* Done with transfer */
                if (wcount[chan] == 0
                   /* && (chan_flags[chan] & STA_WAIT) == 0*/) {
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DETAIL, &chan_dev,
                                  "chan %d < WC0 %o\n", chan,
                                  cmd[chan] & 070);
                    switch (cmd[chan] & 070) {
                    case IOCD:  /* Transfer and disconnect */
                        chan_flags[chan] |= DEV_DISCO | DEV_WEOR;
                        chan_flags[chan] &=
                            ~(STA_START | STA_ACTIVE | STA_PEND);
                        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7289)  {
                            iotraps |= 1 << chan;
                            sim_debug(DEBUG_TRAP, &chan_dev, "chan %d Trap\n",
                                     chan);
                        }
                        chan_info[chan] = 0;
                        break;

                    case IORP:  /* Transfer until end of record */
                        chan_flags[chan] |= STA_WAIT | DEV_WEOR;
                        break;
                    case IOSP:  /* Transfer and proceed */
                    case IOCP:  /* Transfer and proceed, no eor */
                        chan_fetch(chan);
                        break;

                    case IORT:  /* Transfer, continue if LCH pending, */
                        /* else trap, Skip rest of record */
                        chan_flags[chan] |= STA_WAIT | DEV_WEOR /*| STA_TWAIT*/;
                        break;
                    case IOST:  /* Transfer, continue if LCH, else trap */
                    case IOCT:  /* Transfer but no end of record, else trap */
                        chan_flags[chan] &= ~(STA_ACTIVE/*|STA_WAIT*/);
                        chan_flags[chan] |= STA_TWAIT;
                        break;
                    }
                }

                /* Check if device left us */
                if ((chan_flags[chan] & DEV_SEL) == 0) {
                    switch (cmd[chan] & 070) {
                    case IOCP:
                    case IORP:
                    case IOSP:
                    case IOCD:
                        chan_flags[chan] &= ~(STA_START|STA_ACTIVE|STA_WAIT);
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d -Sel< %o\n", chan, cmd[chan] & 070);
                        continue;       /* Handle new command next time */
                    case IOCT:
                    case IORT:
                    case IOST:  /* Behave like EOR */
                        chan_flags[chan] &= ~(STA_ACTIVE|STA_WAIT);
                        chan_flags[chan] |= STA_TWAIT;
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d -Sel< %o\n", chan, cmd[chan] & 070);
                        continue;
                    }
                }

                break;

                /* Device has word, but has not taken it yet */
            case DEV_WRITE | DEV_FULL:
                if (chan_flags[chan] & DEV_REOR) {
                    switch (cmd[chan] & 070) {
                    case IORP:
                    case IORT:
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d EOR>+ %o\n", chan, cmd[chan] & 070);
                        chan_flags[chan] &= ~DEV_FULL;
                    }
                }
                continue;       /* Do nothing if no data xfer pending */

                /* Device needs a word of data */
            case DEV_WRITE:     /* Device needs data word */
                /* Check if device left us */
                if ((chan_flags[chan] & DEV_SEL) == 0) {
                    switch (cmd[chan] & 070) {
                    case IOCP:
                    case IORP:
                    case IOSP:
                        chan_fetch(chan);
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d -Sel< %o\n", chan, cmd[chan] & 070);
                        continue;       /* Handle new command next time */
                    case IOCD:
                        chan_flags[chan] &= ~(STA_START|STA_ACTIVE);
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d -Sel< %o\n", chan, cmd[chan] & 070);
                        continue;
                    case IOCT:
                    case IORT:
                    case IOST:
                        chan_flags[chan] &= ~(STA_ACTIVE);
                        chan_flags[chan] |= STA_TWAIT;
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                "chan %d -Sel< %o\n", chan, cmd[chan] & 070);
                        continue;
                    }
                }

                /* Wait for device to recognize EOR */
                if (chan_flags[chan] & DEV_WEOR)
                    continue;

                    /* Check if got EOR */
                    if (chan_flags[chan] & DEV_REOR) {
                        switch (cmd[chan] & 070) {
                        case IORP:
                            chan_flags[chan] &= ~(DEV_REOR);
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev,
                                    "chan %d EOR> %o\n", chan, cmd[chan] & 070);
                            chan_fetch(chan);
                            chan_flags[chan] |= STA_ACTIVE;
                            break;
                        case IORT:
                            chan_flags[chan] &= ~(DEV_REOR|STA_ACTIVE);
                            chan_flags[chan] |= STA_TWAIT;
                            if (chan_dev.dctrl & cmask)
                                sim_debug(DEBUG_DETAIL, &chan_dev,
                                    "chan %d EOR> %o\n", chan, cmd[chan] & 070);
                            continue;
                        }
                    }

                /* Give device new word if we have one */
                if (wcount[chan] != 0) {

                    if (cmd[chan] & 1) {
                        assembly[chan] = 0;
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DATA, &chan_dev,
                                      "chan %d data > *\n", chan);
                    } else {
                        assembly[chan] = M[caddr[chan]];
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DATA, &chan_dev,
                                      "chan %d data > %012llo\n", chan,
                                      assembly[chan]);
                    }
                    nxt_chan_addr(chan);
                    bcnt[chan] = 6;
                    wcount[chan]--;
                    chan_flags[chan] |= DEV_FULL;
                    continue;   /* Don't start next command until data taken */
                }

                /* Get here if wcount == 0 */
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "chan %d > WC0 %o stat=%08x\n",
                              chan, cmd[chan] & 070, chan_flags[chan]);

                switch (cmd[chan] & 070) {
                case IOCD:      /* Transfer and disconnect */
                    chan_flags[chan] |= DEV_DISCO | DEV_WEOR;
                    chan_flags[chan] &=
                        ~(STA_START | STA_ACTIVE | STA_PEND);
                    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7289)
                        iotraps |= 1 << chan;
                    chan_info[chan] = 0;
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DETAIL, &chan_dev,
                                  "chan %d > DISCO\n", chan);
                    break;

                case IORP:      /* Transfer until end of record */
                    chan_flags[chan] |= DEV_WEOR|STA_WAIT;
                    break;
                case IOSP:      /* Transfer and proceed */
                case IOCP:      /* Transfer and proceed, no eor */
                    chan_fetch(chan);
                    break;

                case IORT:      /* Transfer, continue if LCH pending, */
                    /* else trap, Skip rest of record */
                    chan_flags[chan] |= DEV_WEOR|STA_WAIT;
                    break;
                case IOST:      /* Transfer, continue if LCH, else trap */
                case IOCT:      /* Transfer but no end of record, else trap */
                    chan_flags[chan] &= ~STA_ACTIVE;
                    chan_flags[chan] |= STA_TWAIT;
                    break;
                }
            }
            break;

        case CHAN_7909:
        again:
            /* If waiting for EOR just spin */
            if (chan_flags[chan] & STA_WAIT) {
                if (chan_flags[chan] & DEV_REOR) {
                    chan_flags[chan] &=
                                ~(STA_WAIT|DEV_REOR|CTL_SNS|CTL_READ|CTL_WRITE);
                    if (chan_flags[chan] & DEV_SEL)
                        chan_flags[chan] |= DEV_DISCO;
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DETAIL, &chan_dev,
                            "chan %d EOR Continue\n", chan);
                }
                continue;
            }

            /* Nothing more to do if not active. */
            if (chan_flags[chan] & STA_ACTIVE) {
                /* Execute the next command */
                switch (cmd[chan]) {
                case XXXZ:
                case XXXX:
                case TWT:
                    /* Check if command not allowed */
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_TRAP, &chan_dev, "chan %d CPU Trap\n",
                                  chan);
                    iotraps |= 1 << chan;
                    chan_flags[chan] |= CTL_INHB;
                case WTR:
                case WTRX:
                    /* Check if command not allowed */
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    /* Go into a wait state */
                    chan_flags[chan] &= ~STA_ACTIVE;
                    location[chan]--;
                    break;
                case XMT:
                case XMTX:
                    /* Check if command not allowed */
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    if (wcount[chan] == 0)
                        break;
                    wcount[chan]--;
                    M[caddr[chan]] = M[location[chan]];
                    nxt_chan_addr(chan);
                    bcnt[chan] = 6;
                    location[chan]++;
                    continue;
                case LIPT:
                case LIPTX:
                    chan_flags[chan] &= ~(SNS_IRQ | SNS_IMSK | SNS_UEND);
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_TRAP, &chan_dev, "chan %d %02o LIPT\n",
                                 chan, chan_flags[chan] & 077);
                    /* Fall through */

                case TCH9:
                case TCHX:
                    location[chan] = caddr[chan];
                    break;
                case LIP:
                    chan_flags[chan] &= ~(SNS_IRQ | SNS_IMSK | SNS_UEND);
                    location[chan] = (uint16)M[040 + (2 * chan)] & MEMMASK;
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_TRAP, &chan_dev, "chan %d %02o LIP\n",
                                 chan, chan_flags[chan] & 077);
                    break;
                case CTL:
                    if (chan_flags[chan] & CTL_CNTL)
                        goto xfer;
                    if (chan_flags[chan] & (CTL_READ | CTL_WRITE | CTL_SNS)) {
                        chan9_seqcheck(chan);
                        continue;
                    }
                    chan_flags[chan] |= CTL_CNTL;
                    goto finddev;
                case CTLR:
                    if (chan_flags[chan] & CTL_CNTL)
                        goto xfer;
                    if (chan_flags[chan] & (CTL_READ | CTL_WRITE | CTL_SNS)) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    chan_flags[chan] |= CTL_CNTL | CTL_PREAD;
                    goto finddev;
                case CTLW:
                    if (chan_flags[chan] & CTL_CNTL)
                        goto xfer;
                    if (chan_flags[chan] & (CTL_READ | CTL_WRITE | CTL_SNS)) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    chan_flags[chan] |= CTL_CNTL | CTL_PWRITE;
                    goto finddev;
                case SNS:
                    if (chan_flags[chan] & (CTL_CNTL | CTL_READ | CTL_WRITE)) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    chan_flags[chan] |= CTL_SNS;
                  finddev:
                    chan_flags[chan] &= ~(DEV_REOR|CTL_END|DEV_WEOR);
                    {
                        DEVICE            **dptr;
                        UNIT               *uptr;
                        DIB                *dibp;

                        for (dptr = sim_devices; *dptr != NULL; dptr++) {
                            int                 num = (*dptr)->numunits;
                            int                 j;

                            dibp = (DIB *) (*dptr)->ctxt;
                            /* If not device or 7909 type, just skip */
                            if (dibp == 0 || (dibp->ctype & CH_TYP_79XX) == 0)
                                continue;
                            uptr = (*dptr)->units;
                            for (j = 0; j < num; j++, uptr++) {
                                if ((uptr->flags & UNIT_DIS) == 0 &&
                                    UNIT_G_CHAN(uptr->flags) ==
                                          (unsigned int)chan &&
                                    (sms[chan] & 1) ==
                                          ((UNIT_SELECT & uptr->flags) != 0)) {
                                    goto found;
                                }
                            }
                        }
                        /* If no device, stop right now */
                        chan9_set_error(chan, SNS_ADCHECK);
                        chan_flags[chan] &= ~(CTL_PREAD | CTL_PWRITE | CTL_SNS |
                              CTL_CNTL);
                        iotraps |= 1 << chan;
                        chan_flags[chan] &= ~STA_ACTIVE;
                        break;
                      found:
                        /* Get channel ready to transfer */
                        chan_flags[chan] &=
                                ~(CTL_END|CTL_SEL|DEV_REOR|DEV_FULL);
                        bcnt[chan] = 6;

                        /* Call device to start it running */
                        if (sms[chan] & 1)
                            chan_flags[chan] |= CTL_SEL;
                        switch (dibp->cmd(uptr, cmd[chan], sms[chan])) {
                        case SCPE_IOERR:
                        case SCPE_NODEV:
                            chan9_set_error(chan, SNS_IOCHECK);
                            iotraps |= 1 << chan;
                            chan_flags[chan] &= ~(CTL_PREAD|CTL_PWRITE|CTL_SNS|
                                 CTL_CNTL|STA_ACTIVE);
                            continue;
                        case SCPE_BUSY: /* Device not ready yet, wait */
                            continue;
                        case SCPE_OK:   /* Device will be waiting for command */
                            break;
                        }
                    }
                    /* Special out for sense command */
                    if (cmd[chan] == SNS) {
                        chan_flags[chan] &= ~DEV_WRITE;
                        chan_flags[chan] |= DEV_SEL;
                        break;
                    }
                    chan_flags[chan] |= DEV_WRITE;
                  xfer:
                    /* Check if comand tranfer done */
                    if (chan_flags[chan] & DEV_REOR) {
                        chan_flags[chan] &=
                            ~(DEV_WRITE | DEV_REOR | DEV_FULL);
                        chan_flags[chan] &= ~(CTL_READ | CTL_WRITE);
                        if ((chan_flags[chan] & CTL_END) == 0)
                            chan_flags[chan] |= (chan_flags[chan] &
                                                 (CTL_PREAD | CTL_PWRITE)) >> 2;
                        if ((chan_flags[chan] & (SNS_UEND|CTL_END)) ==
                                (SNS_UEND|CTL_END) && (sms[chan] & 010) == 0)
                            chan_flags[chan] &= ~STA_ACTIVE;
                        chan_flags[chan] &= ~(CTL_CNTL | CTL_PREAD |
                                              CTL_PWRITE | CTL_END);
                        if (chan_flags[chan] & CTL_WRITE)
                            chan_flags[chan] |= DEV_WRITE;
                        bcnt[chan] = 6;
                        break;
                    }

                    /* Check if device ready for next command word */
                    if ((chan_flags[chan] & (DEV_WRITE | DEV_FULL)) ==
                        DEV_WRITE) {
                        assembly[chan] = M[caddr[chan]];
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_CMD, &chan_dev,
                                      "chan %d cmd > %012llo\n",
                                      chan, assembly[chan]);
                        nxt_chan_addr(chan);
                        bcnt[chan] = 6;
                        chan_flags[chan] |= DEV_FULL;
                    }
                    continue;

                case LAR:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    assembly[chan] = M[caddr[chan]];
                    if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_CMD, &chan_dev,
                                      "chan %d LAR > %012llo\n",
                                      chan, assembly[chan]);
                    break;
                case SAR:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_CMD, &chan_dev,
                                      "chan %d SAR < %012llo\n",
                                      chan, assembly[chan]);
                    M[caddr[chan]] = assembly[chan];
                    break;
                case CPYP:
                case CPYP2:
                case CPYP3:
                case CPYP4:
                    if (chan_flags[chan] & (DEV_REOR|CTL_END)) {
                        if (sms[chan] & 0100) {
                            chan9_set_error(chan, SNS_UEND);
                            if (chan_flags[chan] & DEV_SEL)
                                chan_flags[chan] |= (DEV_DISCO | DEV_WEOR);
                            chan_flags[chan] &=
                                ~(STA_WAIT|DEV_REOR|CTL_SNS|CTL_READ|CTL_WRITE);
                            break;
                        }
                        if (wcount[chan] != 0)
                            chan_flags[chan] &= ~(DEV_REOR);
                    }
                case CPYD:
                case CPYDX:
                    if ((chan_flags[chan] & (CTL_READ|CTL_WRITE|CTL_SNS))==0) {
                         chan9_seqcheck(chan);
                         break;
                    }


                    if ((chan_flags[chan] & DEV_FULL) == 0) {
                        /* Check if we still have a select signal */
                        if (wcount[chan] != 0 &&
                            (chan_flags[chan] & DEV_SEL) == 0) {
                            chan9_seqcheck(chan);
                            break;
                        }

                        /* Check if last word transfered */
                        if (wcount[chan] == 0) {
                            if (cmd[chan] == CPYD || cmd[chan] == CPYDX ||
                                chan_flags[chan] & SNS_UEND) {
                                if (chan_dev.dctrl & cmask)
                                    sim_debug(DEBUG_DETAIL, &chan_dev,
                                         "chan %d DISC %o\n", chan, cmd[chan] & 070);
                                if (sms[chan] & 0100 &&
                                        (chan_flags[chan] & DEV_REOR) == 0)
                                     chan9_set_error(chan, SNS_UEND);
                                chan_flags[chan] |= (DEV_WEOR);
                                if (chan_flags[chan] & DEV_SEL)
                                    chan_flags[chan] |= DEV_DISCO;
                                chan_flags[chan] &=
                                        ~(CTL_SNS | CTL_READ | CTL_WRITE);
                                if ((chan_flags[chan] & (SNS_UEND|CTL_END)) ==
                                        (SNS_UEND|CTL_END) &&
                                                 (sms[chan] & 010) == 0)
                                    chan_flags[chan] &= ~STA_ACTIVE;
                            } else {
                                if (chan_flags[chan] & DEV_REOR)
                                    chan_flags[chan] &= ~DEV_REOR;
                            }
                            break;
                        }

                        /* Check for record end in non-concurrent IRQ mode*/
                        if (chan_flags[chan] & DEV_REOR && sms[chan] & 0100) {
                            chan9_set_error(chan, SNS_UEND);
                            chan_flags[chan] &= ~(CTL_SNS|CTL_READ|CTL_WRITE);
                            if (chan_flags[chan] & DEV_SEL)
                                chan_flags[chan] |= (DEV_DISCO | DEV_WEOR);
                            break;
                        }
                    }

                    /* Check if ready to transfer something */
                    switch (chan_flags[chan] & (DEV_WRITE | DEV_FULL)) {
                    case DEV_WRITE | DEV_FULL:
                    case 0:
                        /* If device ended, quit transfer */
                        if (chan_flags[chan] & CTL_END) {
                            /* Disconnect channel if select still active */
                            if (chan_flags[chan] & DEV_SEL) {
                                chan_flags[chan] |= (DEV_DISCO);
                                chan_flags[chan] &= ~(STA_WAIT);
                            }
                            if (sms[chan] & 0100 && wcount[chan] != 0)
                                chan9_set_error(chan, SNS_UEND);
                            chan_flags[chan] &= ~(DEV_WRITE|DEV_FULL|DEV_REOR|
                                CTL_SNS|CTL_READ|CTL_WRITE|CTL_END);
                            /* Get new command ready after disco */
                            chan_fetch(chan);
                        }
                        continue;       /* Do nothing if no data xfer */
                    case DEV_WRITE:     /* Device needs data word */
                        assembly[chan] = M[caddr[chan]];
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DATA, &chan_dev,
                                      "chan %d data > %012llo\n",
                                      chan, assembly[chan]);
                        if (sms[chan] & 020)    /* BCD Xlat mode */
                            bcd_xlat(chan, 0);
                        if (sms[chan] & 040) {  /* Read backward */
                            caddr[chan] =
                                ((dualcore) ? (0100000 & caddr[chan]) : 0) |
                                ((caddr[chan] - 1) & MEMMASK);
                        } else {
                            nxt_chan_addr(chan);
                        }
                        bcnt[chan] = 6;
                        wcount[chan]--;
                        chan_flags[chan] |= DEV_FULL;
                        break;
                    case DEV_FULL:      /* Device has given us a dataword */
                        if (bcnt[chan] != 0)
                            assembly[chan] <<= 6 * bcnt[chan];
                        if (sms[chan] & 020)    /* BCD Xlat mode */
                            bcd_xlat(chan, 1);
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DATA, &chan_dev,
                                      "chan %d data < %012llo\n",
                                      chan, assembly[chan]);
                        M[caddr[chan]] = assembly[chan];
                        if (sms[chan] & 040) {  /* Read backward */
                            caddr[chan] =
                                ((dualcore) ? (0100000 & caddr[chan]) : 0) |
                                ((caddr[chan] - 1) & MEMMASK);
                        } else {
                            nxt_chan_addr(chan);
                        }
                        assembly[chan] = 0;
                        bcnt[chan] = 6;
                        wcount[chan]--;
                        chan_flags[chan] &= ~DEV_FULL;
                        break;
                    }

                    continue;

                case TCM:
                case TCMX:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    } else {
                        /* Compare wordcount high to wordcound low */
                        /* 0 = chan check, 1-6 = assmebly, 7 = 0 */
                        uint8               v;
                        uint8               ch = wcount[chan] >> 12;
                        uint8               mask = wcount[chan] & 077;
                        uint8               flag = wcount[chan] & 0100;

                        if (ch == 0) {
                            v = (chan_flags[chan] >> 5) & 077;
                        } else if (ch == 7) {
                            v = 0;
                        } else {
                            v = (uint8)(077 & (assembly[chan] >> (6 * (6-ch))));
                        }
                        if (chan_dev.dctrl & cmask)
                            sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "TCM %d:%02o & %02o\n\r", ch, v, mask);
                        if ((v == mask && flag == 0)
                            || ((v & mask) == mask && flag != 0))
                            location[chan] = caddr[chan];
                    }
                    break;
                case TDC:
                    if (counter[chan] != 0) {
                        location[chan] = caddr[chan];
                        counter[chan]--;
                    }
                    break;
                case LCC:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    counter[chan] = caddr[chan] & 077;
                    break;
                case SMS:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DETAIL, &chan_dev,
                                      "chan %d SMS %03o -> %03o %03o ",
                                chan, sms[chan], caddr[chan] & 0177,
                                (SNS_IRQS & chan_flags[chan])>>5);
                    sms[chan] = caddr[chan] & 0177;
                    /* Check to see if IRQ still pending */
                    if ((chan_flags[chan] & CTL_INHB) == 0 &&
                        chan_flags[chan] & SNS_IRQS &
                        (~((sms[chan] << 5) & (SNS_IMSK ^ SNS_IRQS)))) {
                        chan_irq[chan] = 1;
                    }
                    if (chan_dev.dctrl & cmask)
                        sim_debug(DEBUG_DETAIL, &chan_dev, "Irqs = %03o %o\n",
                                ((chan_flags[chan] & SNS_IRQS)>>5) &
                                     ((sms[chan] ^ 016) | 061), chan_irq[chan]);
                    break;
                case ICC:
                case ICCX:
                    if (chan_flags[chan] & DEV_SEL) {
                        chan9_seqcheck(chan);
                        break;
                    }
                    /* transfer counter from wordcount high to assembly */
                    /* 0 = SMS to 6, 1-6 = assmebly, 7 = nop */
                    {
                        t_uint64            v = counter[chan] & 077;
                        uint8               ch = wcount[chan] >> 12;

                        if (ch == 0) {
                            /* Not what POO says, but what diags want */
                            /* POO says other digits not affected. */
                            assembly[chan] = sms[chan] & 00137;
                        } else if (ch != 7) {
                            assembly[chan] &= ~(077L << (6 * (6 - ch)));
                            assembly[chan] |= (v << (6 * (6 - ch)));
                        }
                    }
                    break;
                }
            }

            /* Check for intrupts */
            if (chan_irq[chan] ||
                /* Can only interupt when channel inactive */
                ((chan_flags[chan] & (DEV_SEL | STA_ACTIVE | CTL_CNTL | CTL_SNS
                         | SNS_IRQ | CTL_INHB | CTL_READ | CTL_WRITE)) == 0 &&
                 cmd[chan] != TWT  &&
                (chan_flags[chan] & SNS_IRQS &
                        (((sms[chan] ^ 016) | 061) << 5)))) {
                uint8   ocmd = cmd[chan];
                M[040 + (chan * 2)] = location[chan] & MEMMASK;
                M[040 + (chan * 2)] |= ((t_uint64) caddr[chan]) << 18;
                chan_flags[chan] |= STA_ACTIVE|CTL_INHB;
                location[chan] = 041 + (chan * 2);
                chan_irq[chan] = 0;
                if (chan_dev.dctrl & cmask)
                     sim_debug(DEBUG_TRAP, &chan_dev, "chan irq %d\n\r", chan);
                chan_fetch(chan);
                /* Fake a xec type trap */
                if ((ocmd & 073) == WTR || ocmd == TWT)
                   location[chan] = (uint16)(M[040 + (chan * 2)] + 1)& MEMMASK;
                else
                   location[chan] = (uint16)M[040 + (chan * 2)] & MEMMASK;
                goto again;
            }

            if (chan_flags[chan] & STA_ACTIVE) {
                uint8   c = cmd[chan];
                chan_fetch(chan);
                /* Check if we should interupt during unusual end */
                if (sms[chan] & 0100 && (c & 070) == CPYP &&
                    (cmd[chan] & 071) == CPYD && wcount[chan] == 0) {
                    if (chan_dev.dctrl & cmask)
                          sim_debug(DEBUG_DETAIL, &chan_dev,
                                        "chan non-concur %d\n\r", chan);
                    chan9_set_error(chan, SNS_UEND);
                    chan_flags[chan] &= ~(CTL_SNS|CTL_READ|CTL_WRITE);
                    if (chan_flags[chan] & DEV_SEL)
                        chan_flags[chan] |= DEV_WEOR|DEV_DISCO;
                    chan_fetch(chan);
                }
                if (cmd[chan] != TCM && (chan_flags[chan] & DEV_DISCO) == 0)
                    goto again;
            }
#endif
        }
    }
}

void
chan_fetch(int chan)
{
    uint16              loc;
    t_uint64            temp;

    sim_interval--;
    loc = location[chan] & MEMMASK;
    if (dualcore)
        loc |= location[chan] & 0100000;
    temp = M[loc];
    location[chan] = ((loc + 1) & MEMMASK) | (loc & 0100000);
    cmd[chan] = (uint8)(((temp >> 30) & 074) | ((temp >> 16) & 1));
    wcount[chan] = (uint16)(temp >> 18) & 077777;
    caddr[chan] = (uint16)temp & MEMMASK;
    if (dualcore)
        caddr[chan] |= temp & 0100000;
    /* Check indirect bit */
    if (temp & 0400000) {
        caddr[chan] = (uint16)M[caddr[chan]];
        if (dualcore)
            caddr[chan] &= 0100000 | MEMMASK;
        else
            caddr[chan] &= MEMMASK;
        sim_interval--;
    }
    /* Clear pending IO traps for channel */
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_CHAN, &chan_dev,
                  "chan %d fetch adr=%05o cmd=%03o caddr=%05o wcount=%05o\n",
                  chan, location[chan], cmd[chan], caddr[chan],
                  wcount[chan]);
}

/* Reset the channel, clear any pending device */
void
chan_rst(int chan, int type)
{
    /* Issure reset command to device */
    if (type == 0 && CHAN_G_TYPE(chan_unit[chan].flags) != CHAN_7909)
        return;
    if (type != 0 && CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7909)
        return;
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_CHAN, &chan_dev, "Reset channel\n");
    /* Clear outstanding traps on reset */
    if (type)
        iotraps &= ~(1 << chan);
    chan_info[chan] &= ~(CHAINF_START | CHAINF_RUN);
    chan_flags[chan] &= (CHS_EOF|CHS_BOT|CHS_EOT|DEV_DISCO|DEV_SEL);
    caddr[chan] = 0;
    cmd[chan] = 0;
    sms[chan] = 0;
    chan_irq[chan] = 0;
    wcount[chan] = 0;
    location[chan] = 0;
    counter[chan] = 0;          /* Channel memory address */
}

/* Issue a command to a channel */
int
chan_cmd(uint16 dev, uint16 dcmd)
{
    UNIT               *uptr;
    uint32              chan;
    DEVICE            **dptr;
    DIB                *dibp;
    int                 j;

    /* Find device on given channel and give it the command */
    chan = (dev >> 9) & 017;
    /* If no channel device, quick exit */
    if (chan_unit[chan].flags & UNIT_DIS)
        return SCPE_IOERR;
    /* On 704 device new command aborts current operation */
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO &&
        (chan_flags[chan] & (DEV_SEL | DEV_DISCO)) == DEV_SEL) {
        /* Check if device picked up last transfer */
        if ((chan_flags[chan] & (DEV_FULL|DEV_WRITE)) == (DEV_FULL|DEV_WRITE))
            return SCPE_BUSY;
        /* Yes, disconnect device and tell it to write a EOR */
        if ((chan_flags[chan] & (DEV_WRITE)) == (DEV_WRITE) ||
            (chan_flags[chan] & (DEV_FULL)) == (DEV_FULL))
            chan_flags[chan] |= DEV_DISCO | DEV_WEOR;
        return SCPE_BUSY;
    }
    /* Unit is busy doing something, wait */
    if (chan_flags[chan] & (DEV_SEL | DEV_DISCO | STA_TWAIT | STA_WAIT))
        return SCPE_BUSY;
    chan_flags[chan] &= ~(DEV_REOR|DEV_WEOR|DEV_FULL|DEV_WRITE|STA_WAIT);
    /* Ok, try and find the unit */
    dev &= 07777;
    for (dptr = sim_devices; *dptr != NULL; dptr++) {
        int                 r;

        dibp = (DIB *) (*dptr)->ctxt;
        /* If no DIB, not channel device */
        if (dibp == NULL || dibp->ctype == CHAN_7909 ||
            (dibp->addr & dibp->mask) != (dev & dibp->mask))
            continue;
        uptr = (*dptr)->units;
        if (dibp->upc == 1) {
            int                 num = (*dptr)->numunits;

            for (j = 0; j < num; j++) {
                if (UNIT_G_CHAN(uptr->flags) == chan) {
                    r = dibp->cmd(uptr, dcmd, dev);
                    if (r != SCPE_NODEV) {
                        bcnt[chan] = 6;
                        cmd[chan] = 0;
                        caddr[chan] = 0;
                        location[chan] = 0;
                        return r;
                    }
                }
                uptr++;
            }
        } else {
            if (UNIT_G_CHAN(uptr->flags) == chan) {
                r = dibp->cmd(uptr, dcmd, dev);
                if (r != SCPE_NODEV) {
                    bcnt[chan] = 6;
                    cmd[chan] = 0;
                    caddr[chan] = 0;
                    location[chan] = 0;
                    return r;
                }
            }
        }
    }
    return SCPE_NODEV;
}


/* Give channel a new address to start working at */
int
chan_start(int chan, uint16 addr)
{
    /* Hold this command until after channel has disconnected */
    if (chan_flags[chan] & DEV_DISCO)
        return SCPE_BUSY;

    /* Depending on channel type controls how command works */
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7909) {
        if (chan_flags[chan] & STA_ACTIVE)
            return SCPE_BUSY;
        chan_flags[chan] &=
            ~(CTL_CNTL | CTL_SNS | CTL_READ | CTL_PREAD | CTL_INHB |
              CTL_WRITE | CTL_PWRITE | SNS_UEND | SNS_IOCHECK);
    } else {
        /* All clear, start ball rolling on new command */
        /* Force iocheck if attempt to load inactive channel with command */
        if ((chan_flags[chan] & DEV_SEL) == 0) {
            /* Fetch next command */
            location[chan] = addr;
            chan_fetch(chan);
            return SCPE_IOERR;
        }
    }
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_CHAN, &chan_dev,
                  "chan %d start IC=%05o addr=%o\n\r", chan, IC - 1, addr);
    /* Fetch next command */
    location[chan] = addr;
    chan_fetch(chan);
    chan_flags[chan] &= ~(STA_PEND|STA_TWAIT|STA_WAIT|DEV_WEOR|DEV_FULL);
    chan_flags[chan] |= STA_START | STA_ACTIVE;
    chan_info[chan] |= CHAINF_START;
    return SCPE_OK;
}

/* Give channel a new address to start working at */
int
chan_load(int chan, uint16 addr)
{
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7909) {
        if (chan_flags[chan] & STA_ACTIVE)
            return SCPE_BUSY;
        /* Ignore command if this waiting on IRQ */
        if (cmd[chan] == TWT && iotraps & (1 << chan))
                return SCPE_OK;
        chan_flags[chan] &= ~(CTL_INHB);
        location[chan] = caddr[chan];
    } else {
        /* Force iocheck if attempt to load channel with command,
           that has not been started or is not in select state */
        if ((chan_flags[chan] & (DEV_SEL | STA_START)) != (DEV_SEL|STA_START))
            return SCPE_IOERR;

        /* If channel active, or waiting EOR, should hold CPU */
        if (chan_flags[chan] & (STA_ACTIVE | STA_WAIT)) {
            chan_flags[chan] |= STA_PEND;
            return SCPE_BUSY;
        }
        chan_flags[chan] &= ~(STA_PEND|STA_TWAIT);
        location[chan] = addr;
    }
    /* All clear, start ball rolling on new command */
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_CHAN, &chan_dev,
                  "chan %d load IC=%05o addr=%o stat=%08x\n\r", chan, IC - 1,
                         addr, chan_flags[chan]);
    chan_fetch(chan);
    chan_flags[chan] |= STA_ACTIVE;
    return SCPE_OK;
}

/* return the channels current command address */
void
chan_store(int chan, uint16 loc)
{
    t_uint64            reg = 0LL;

    /* Check if channel has units attached */
    if (chan_unit[chan].flags & CHAN_SET) {
        /* Return command/addr/xcmd/location */
        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7909) {
            reg = location[chan] & MEMMASK;
            reg |= ((t_uint64) caddr[chan]) << 18;
            /* Check if channel has units attached */
        } else {
            /* If doing a TCH, process it */
            if ((cmd[chan] & 070) == TCH)
                chan_proc();
            reg = caddr[chan];
            reg |= ((t_uint64) (location[chan] & MEMMASK)) << 18;
            reg |= ((t_uint64) (cmd[chan] & 070)) << 30;
            reg |= ((t_uint64) (cmd[chan] & 01)) << 16;
        }
    }
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_SNS, &chan_dev, "chan %d status %012llo\n\r",
                 chan,reg);
    M[loc & (MEMMASK | 0100000)] = reg;
}

/* Store channel diagnostic bits */
void
chan_store_diag(int chan, uint16 loc)
{
    t_uint64            reg;
    int                 results;

    /* Counter[6], iocheck,seq check, unusal end, attn 1,
     * attn 2, adpter check, prepare to read, prepare to write,
     * read status, write status, interupt */
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7909) {
        reg = ((t_uint64) counter[chan]) << 30;
        results = SNS_MASK & chan_flags[chan];
        if (results & (((sms[chan] ^ 016) | 061) << 5))
            results |= 1;
        reg |= ((t_uint64) (results)) << 19;
        if (chan_dev.dctrl & (0x0100 << chan))
            sim_debug(DEBUG_SNS, &chan_dev, "chan %d diags %012llo\n\r",
                         chan,reg);
        M[loc & (MEMMASK | 0100000)] = reg;
    }
}

/*
 * Write a word to the assembly register.
 */
int
chan_write(int chan, t_uint64 * data, int flags)
{

    /* Check if last data still not taken */
    if (chan_flags[chan] & DEV_FULL) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_REOR;
            chan_flags[chan] &= ~(DEV_WEOR|DEV_FULL);
            return END_RECORD;
        }

        /* If active set attention, report IO error if
          device does not want to disconnect  */
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }

        /* If requested, force disconnect */
        chan_flags[chan] |= DEV_DISCO & flags;

        return TIME_ERROR;
    } else {
        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO)
            MQ = *data;
        assembly[chan] = *data;
        bcnt[chan] = 6;
        chan_flags[chan] |= DEV_FULL;
        chan_flags[chan] &= ~DEV_WRITE;
        if (flags & DEV_REOR) {
            chan_flags[chan] |= DEV_REOR;
        }
    }

    return DATA_OK;
}

/*
 * Read next word from assembly register.
 */
int
chan_read(int chan, t_uint64 * data, int flags)
{

    /* Return END_RECORD if requested */
    if (flags & DEV_WEOR) {
        chan_flags[chan] |= DEV_REOR;
        chan_flags[chan] &= ~(DEV_WEOR);
        return END_RECORD;
    }

    /* Check if no data waiting */
    if ((chan_flags[chan] & DEV_FULL) == 0) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_WRITE;
            chan_flags[chan] &= ~(DEV_WEOR);
            return END_RECORD;
        }

        /* If active set attention, report IO error if
          device does not want to disconnect */
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }

        /* If requested, force disconnect */
        chan_flags[chan] |= DEV_DISCO & flags;

        return TIME_ERROR;
    } else {
        *data = assembly[chan];
        bcnt[chan] = 6;
        chan_flags[chan] &= ~DEV_FULL;
        /* If end of record, don't transfer any data */
        if (flags & DEV_REOR) {
            chan_flags[chan] &= ~(DEV_WRITE);
            chan_flags[chan] |= DEV_REOR;
        } else
            chan_flags[chan] |= DEV_WRITE;
    }
    return DATA_OK;
}

/*
 * Write a char to the assembly register.
 */
int
chan_write_char(int chan, uint8 * data, int flags)
{
    /* If Writing end of record, abort */
    if (chan_flags[chan] & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_FULL | DEV_WEOR);
        return END_RECORD;
    }

    /* Check if last data still not taken */
    if (chan_flags[chan] & DEV_FULL) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_REOR;
            chan_flags[chan] &= ~(DEV_WEOR|DEV_FULL);
            return END_RECORD;
        }

        /* If active set attention, report IO error if
          device does not want to disconnect */
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }

        /* If requested, force disconnect */
        chan_flags[chan] |= DEV_DISCO & flags;

        return TIME_ERROR;
    } else {
        int     cnt = --bcnt[chan];
        t_uint64        wd;
        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO)
           wd = MQ;
        else
           wd = assembly[chan];
        wd &= 0007777777777LL;
        wd <<= 6;
        wd |= (*data) & 077;
        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO)
            MQ = wd;
        else
            assembly[chan] = wd;

        if (cnt == 0) {
            chan_flags[chan] |= DEV_FULL;
            chan_flags[chan] &= ~DEV_WRITE;
        }
        if (flags & DEV_REOR) {
            chan_flags[chan] |= DEV_FULL|DEV_REOR;
            chan_flags[chan] &= ~DEV_WRITE;
        }
    }


    return DATA_OK;
}

/*
 * Read next char from assembly register.
 */
int
chan_read_char(int chan, uint8 * data, int flags)
{

    /* Return END_RECORD if requested */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_WEOR);
        return END_RECORD;
    }

    /* Check if he write out last data */
    if ((chan_flags[chan] & DEV_FULL) == 0) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_WRITE|DEV_REOR;
            chan_flags[chan] &= ~(DEV_WEOR);
            return END_RECORD;
        }

        /* If active set attention, report IO error if
          device does not want to disconnect */
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }

        /* If requested, force disconnect */
        chan_flags[chan] |= DEV_DISCO & flags;

        return TIME_ERROR;
    } else {
        int     cnt = --bcnt[chan];
        t_uint64        wd = assembly[chan];
        *data = (uint8)(077 & (wd >> 30));
        wd <<= 6;
        wd |= 077 & (wd >> 36);
        wd &= 0777777777777LL;
        if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO)
            MQ = wd;
        assembly[chan] = wd;
        if (cnt == 0) {
            chan_flags[chan] &= ~DEV_FULL;
            bcnt[chan] = 6;
        }
        /* If end of record, don't transfer any data */
        if (flags & DEV_REOR) {
            chan_flags[chan] &= ~(DEV_WRITE|DEV_FULL);
            chan_flags[chan] |= DEV_REOR;
        } else
            chan_flags[chan] |= DEV_WRITE;
    }
    return DATA_OK;
}

void
chan9_seqcheck(int chan)
{
    /* Disconnect channel if active */
    if (chan_flags[chan] & DEV_SEL)
        chan_flags[chan] |= DEV_DISCO;
    chan_flags[chan] &= ~(CTL_READ|CTL_WRITE|CTL_SNS|STA_ACTIVE);
    if (chan_dev.dctrl & (0x0100 << chan))
        sim_debug(DEBUG_EXP, &chan_dev, "chan %d seq\n", chan);
    chan9_set_error(chan, SNS_SEQCHECK);
}

void
chan9_set_error(int chan, uint32 mask)
{
    if (chan_flags[chan] & mask)
        return;
    chan_flags[chan] |= mask;
    if (mask & (~((sms[chan] << 5) & (SNS_IMSK ^ SNS_IRQS)))) {
        chan_irq[chan] = 1;
    }
}

t_stat
chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
#ifdef I7090
   fprintf(st, "%s\n\n", chan_description(dptr));
   fprintf (st, "The 7090 supports up to 8 channels. Channel models include\n\n");
   fprintf (st, "        Unit record     Polled mode I/O devices\n");
   fprintf (st, "        7607            standard multiplexor channel\n");
   fprintf (st, "        7909            advanced capabilities channel\n");
   fprintf (st, "        7289            special channel for high speed drum\n\n");
   fprintf (st, "Channels can be reconfigured on the 7090, this generally ");
   fprintf (st, "happens automatically.\nHowever at times it can be useful to ");
   fprintf (st, "force a channel to a specific device. If\ndevices are attached");
   fprintf (st, "to incorrect channel types an error will be reported at sim\n");
   fprintf (st, "start. The first channel is fixed for Polled mode devices.\n\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
#else
   fprintf(st, "IBM 704 Channel\n\n");
   fprintf(st, "Psuedo device to display IBM 704 I/O. The IBM 704 used polled");
   fprintf(st, " I/O,\nThe assembly register and the flags can be displayed\n");
   fprintf(st, "There are no options for the this device\n");
#endif
return SCPE_OK;
}

const char *
chan_description(DEVICE *dptr)
{
    return "IBM 7090 channel controller";
}
