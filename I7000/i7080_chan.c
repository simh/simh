/* i7080_chan.c: IBM 7080 Channel simulator

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

   The channel state for the IBM 705 channel is:
        705:    Polled mode transfer, unit record devices.
                Each chan_cmd will transfer one record.
        7621:   Basic data channel for 729 tapes.
        7908:   Channel to talk to disk, hypertape and data com.

   The 705 has 4 7621 channels.
   The status for these are kept in bank 2. Form as follow:
      Word 3 digits 7-6: 0
      Word 3 digit 5:  chan control digit.
      Word 3 digits 4-0: data buffer A.
      Word 2 digits 7-6: 0
      Word 2 digit 5:  chan control digit.
      Word 2 digits 4-0: data buffer B.
      Word 1 digits 7-4: 0
      Word 1 digits 3-0: Data Memory Address SMAC.
      Word 0 digits 7-4: Channel Program Status.
      Word 0 digits 3-0: Record Count/Program location.

   The 705 has 2 7908 channels.
   The status for these are kept in bank 4. Form as follow:
      Word 3 digits 7-0: 0
      Word 2 digits 7-0: 0
      Word 1 digits 7-4: 0
      Word 1 digits 3-0: Data Memory Address SMAC.
      Word 0 digits 7-4: Channel Program Status.
      Word 0 digits 3-0: Program location.

*/

#include "i7080_defs.h"

extern uint16       iotraps;
extern uint8        iocheck;
extern UNIT         cpu_unit;
extern uint16       IC;
extern uint8        AC[6 * 512];
extern int          chwait;
extern uint16       selreg;
extern uint16       selreg2;
extern uint16       flags;
extern uint32       MAC2;
extern uint16       irqflags;
extern uint8        ioflags[5000/8];

#define UNIT_V_MOD      (UNIT_V_UF + 4)
#define UNIT_V_HS       (UNIT_V_MOD + 1)
#define CHAN_MOD        (1 << UNIT_V_MOD)
#define CHAN_HS         (1 << UNIT_V_HS)



t_stat          set_chan_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat          chan_reset(DEVICE * dptr);
t_stat          chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char      *chan_description (DEVICE *dptr);


/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

uint32              caddr[NUM_CHAN];            /* Channel memory address */
uint8               bcnt[NUM_CHAN];             /* Channel character count */
uint16              cmd[NUM_CHAN];              /* Current command */
uint16              irqdev[NUM_CHAN];           /* Device to generate interupts
                                                   for channel */
uint32              assembly[NUM_CHAN];         /* Assembly register */
uint32              chan_flags[NUM_CHAN];       /* Unit status */
extern uint8        inquiry;


#define READ_WRD        1
#define WRITE_WRD       2

const char     *chan_type_name[] = {
    "Polled", "Unit Record", "7621", "7908", "754"};


UNIT                chan_unit[] = {
    {UDATA(NULL, CHAN_SET | CHAN_S_TYPE(CHAN_UREC), 0)},
    /* Tape devices */
    {UDATA(NULL, CHAN_MOD|CHAN_SET|CHAN_S_TYPE(CHAN_7621), 0), 0, 0},   /* 20 */
    {UDATA(NULL, CHAN_MOD|CHAN_SET|CHAN_S_TYPE(CHAN_7621), 0), 0, 1},   /* 21 */
    {UDATA(NULL, CHAN_MOD|CHAN_SET|CHAN_S_TYPE(CHAN_7621), 0), 0, 2},   /* 22 */
    {UDATA(NULL, CHAN_MOD|CHAN_SET|CHAN_S_TYPE(CHAN_7621), 0), 0, 3},   /* 23 */
    /* 7080 High speed data channels */
    {UDATA(NULL, CHAN_HS | CHAN_SET | CHAN_S_TYPE(CHAN_7908), 0), 0, 0}, /* 40 */
    {UDATA(NULL, CHAN_HS | CHAN_SET | CHAN_S_TYPE(CHAN_7908), 0), 0, 1}, /* 41 */
    {UDATA(NULL, CHAN_SET | CHAN_S_TYPE(CHAN_7621), 0), 0, 4},  /* 44 */
    {UDATA(NULL, CHAN_SET | CHAN_S_TYPE(CHAN_7621), 0), 0, 5},  /* 45 */
    {UDATA(NULL, CHAN_SET | CHAN_S_TYPE(CHAN_7621), 0), 0, 6},  /* 46 */
    {UDATA(NULL, CHAN_SET | CHAN_S_TYPE(CHAN_7621), 0), 0, 7},  /* 47 */
};

REG                 chan_reg[] = {
    {BRDATA(ADDR, caddr, 10, 18, NUM_CHAN), REG_RO},
    {BRDATA(CMD, cmd, 8, 6, NUM_CHAN), REG_RO},
    {BRDATA(FLAGS, chan_flags, 2, 32, NUM_CHAN), REG_RO},
    {NULL}
};

MTAB                chan_mod[] = {
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_UREC), "UREC", "UREC", &set_chan_type,
                NULL,NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_754), "754", "754", &set_chan_type,
                NULL,NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7621), "7621", "7621", &set_chan_type,
                NULL,NULL},
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7908), "7908", NULL, NULL,NULL,NULL},
    {CHAN_HS, CHAN_HS, "HS", "HS", NULL,NULL,NULL},
    {MTAB_VUN, 0, "UNITS", NULL, NULL, &print_chan, NULL},
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
    {"CH20", 0x0100 << 1},
    {"CH21", 0x0100 << 2},
    {"CH22", 0x0100 << 3},
    {"CH23", 0x0100 << 4},
    {"CH40", 0x0100 << 5},
    {"CH41", 0x0100 << 6},
    {"CH44", 0x0100 << 7},
    {"CH45", 0x0100 << 8},
    {"CH46", 0x0100 << 9},
    {"CH47", 0x0100 << 10},
    {0, 0}
};

DEVICE              chan_dev = {
    "CH", chan_unit, chan_reg, chan_mod,
    NUM_CHAN, 8, 15, 1, 8, 36,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, chn_debug,
    NULL, NULL, &chan_help, NULL, NULL, &chan_description
};


t_stat
set_chan_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
    if ((uptr->flags & CHAN_MOD) == 0)
        return SCPE_ARG;
    uptr->flags &= ~CHAN_MODEL;
    uptr->flags |= val;
    return SCPE_OK;
}

t_stat
chan_reset(DEVICE * dptr)
{
    int                 i;

    /* Clear channel assignment */
    for (i = 0; i < NUM_CHAN; i++) {
        chan_flags[i] = 0;
        caddr[i] = 0;
        cmd[i] = 0;
        bcnt[i] = 0;
    }
    return chan_set_devs(dptr);
}

/* Map device to channel */
int
chan_mapdev(uint16 dev) {

    switch((dev >> 8) & 0xff) {
    case 0x02: return 1 + ((dev >> 4) & 0xf);   /* Map tapes to 20-23 */
    case 0x20:
               if (CHAN_G_TYPE(chan_unit[1].flags) == CHAN_754)
                   return -1;
               return 1;        /* Channel 20 */
    case 0x21:
               if (CHAN_G_TYPE(chan_unit[2].flags) == CHAN_754)
                   return -1;
               return 2;        /* Channel 21 */
    case 0x22:
               if (CHAN_G_TYPE(chan_unit[3].flags) == CHAN_754)
                   return -1;
               return 3;        /* Channel 22 */
    case 0x23:
               if (CHAN_G_TYPE(chan_unit[4].flags) == CHAN_754)
                   return -1;
               return 4;        /* Channel 23 */
    case 0x40: return 5;        /* HS Channel 40 */
    case 0x41: return 6;        /* HS Channel 41 */
    case 0x44: return 7;        /* Channel 44 */
    case 0x45: return 8;        /* Channel 45 */
    case 0x46: return 9;        /* Channel 46 */
    case 0x47: return 10;       /* Channel 47 */
    default:
             if (dev > 0x2000)  /* Invalid if over 2000 and not selected */
                return -1;
             return 0;
    }
}

/* Boot from given device */
t_stat
chan_boot(int32 unit_num, DEVICE * dptr)
{
    /* Set IAR = 1 (done by reset), channel to read one
        record to location 1 */
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags);
    extern t_stat       cpu_reset(DEVICE *);

    cpu_reset(&cpu_dev);
    selreg = ((DIB *) dptr->ctxt)->addr + unit_num;
    chwait = chan + 1;  /* Force wait for channel */
    chan_flags[chan] |= STA_ACTIVE;
    chan_flags[chan] &= ~STA_PEND;
    cmd[chan] = 0;
    caddr[chan] = 0;
    return SCPE_OK;
}

t_stat
chan_issue_cmd(uint16 chan, uint16 dcmd, uint16 dev) {
    DEVICE            **dptr;
    DIB                *dibp;
    unsigned int        j;
    UNIT               *uptr;

    for (dptr = sim_devices; *dptr != NULL; dptr++) {
        int                 r;

        dibp = (DIB *) (*dptr)->ctxt;
        /* If no DIB, not channel device */
        if (dibp == 0)
            continue;
        uptr = (*dptr)->units;
        /* If this is a 7907 device, check it */
        if (dibp->ctype & CH_TYP_79XX) {
            for (j = 0; j < (*dptr)->numunits; j++, uptr++) {
                if (UNIT_G_CHAN(uptr->flags) == chan &&
                    (UNIT_SELECT & uptr->flags) == 0 &&
                    (dibp->addr & dibp->mask) == (dev & dibp->mask)) {
                    r = dibp->cmd(uptr, dcmd, dev);
                    if (r != SCPE_NODEV)
                        return r;
                }
            }
        /* Handle 7621 DS units */
        } else if (dibp->ctype & CH_TYP_76XX &&
                        (UNIT_G_CHAN(uptr->flags) == chan)) {
             r = dibp->cmd(uptr, dcmd, dev);
             if (r != SCPE_NODEV)
                 return r;
        /* Handle 754 and unit record devices */
        } else if ((dibp->addr & dibp->mask) == (dev & dibp->mask)) {
            if (dibp->upc == 1) {
                for (j = 0; j < (*dptr)->numunits; j++) {
                    if (UNIT_G_CHAN(uptr->flags) == chan) {
                        r = dibp->cmd(uptr, dcmd, dev);
                        if (r != SCPE_NODEV)
                            return r;
                    }
                    uptr++;
                }
            } else {
                if (UNIT_G_CHAN(uptr->flags) == chan) {
                    r = dibp->cmd(uptr, dcmd, dev);
                    if (r != SCPE_NODEV)
                        return r;
                }
            }
        }
    }
    return SCPE_NODEV;
}

/* Decrement the record count for a given channel, return 1 when
   no more records to send */
int chan_decr_reccnt(int chan) {
    int unit;

    unit = 512 + chan_unit[chan].u3 * 32;
    if (chan_dev.dctrl & (0x0100 << chan))
           sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d reccnt %02o %02o %02o\n",
                 chan, AC[unit + 3], AC[unit + 2], AC[unit + 1]);
   if (AC[unit + 1] == 10 && AC[unit + 2] == 10 && AC[unit + 3] == 10)
        return 1;
    if (AC[unit + 1] != 10) {
        AC[unit + 1]--;
        if (AC[unit + 1] == 0)
           AC[unit + 1] = 10;
    } else {
        AC[unit + 1] = 9;
        if (AC[unit + 2] != 10) {
            AC[unit + 2]--;
            if (AC[unit + 2] == 0)
               AC[unit + 2] = 10;
        } else {
            AC[unit + 2] = 9;
            if (AC[unit + 3] != 10) {
                AC[unit + 3]--;
                if (AC[unit + 3] == 0)
                   AC[unit + 3] = 10;
            }
        }
   }
    if (chan_dev.dctrl & (0x0100 << chan))
           sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d reccnt- %02o %02o %02o\n",
                 chan, AC[unit + 3], AC[unit + 2], AC[unit + 1]);
   if (AC[unit + 1] == 10 && AC[unit + 2] == 10 && AC[unit + 3] == 10)
        return 1;
   return 0;
}

/* Return true if record count is zero */
int chan_zero_reccnt(int chan) {
    int unit;

    unit = 512 + chan_unit[chan].u3 * 32;
    if (chan_dev.dctrl & (0x0100 << chan))
           sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d reccnt %02o %02o %02o\n",
                 chan, AC[unit + 3], AC[unit + 2], AC[unit + 1]);
   if (AC[unit + 1] == 10 && AC[unit + 2] == 10 && AC[unit + 3] == 10)
        return 1;
   return 0;
}

/* Return next channel data address, advance address by 5 if channel */
uint32  chan_next_addr(int chan) {
    int         unit = 0;
    uint32      addr = 0;
    switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
    case CHAN_754:
    case CHAN_UREC:
        return ++caddr[chan];

    case CHAN_7621:
        unit = 8 + 512 + chan_unit[chan].u3 * 32;
        break;
    case CHAN_7908:
        unit = 8 + 1024 + chan_unit[chan].u3 * 32;
        break;
    }
    addr = load_addr(unit);
    store_addr(addr + 5, unit);
    return addr;
}

/* Execute the next channel instruction. */
void
chan_proc()
{
    int                 chan;
    int                 cmask;
    int                 unit;
    uint32              addr;

    /* Scan channels looking for work */
    for (chan = 0; chan < NUM_CHAN; chan++) {
        /* Skip if channel is disabled */
        if (chan_unit[chan].flags & UNIT_DIS)
            continue;

       /* If channel is disconnecting, do nothing */
        if (chan_flags[chan] & DEV_DISCO)
             continue;
        cmask = 0x0100 << chan;

        /* Check if RWW pending */
        if (chan_flags[chan] & STA_PEND) {
             /* If pending issue read command */
             chan_flags[chan] &= ~STA_PEND;
             if (selreg2 & 0x8000) {
                 int    chan2;
                 /* Find device on given channel and give it the command */
                 chan2 = chan_mapdev(selreg2 & 0x7fff);
                 if (chan2 < 0 || chan2 >= NUM_CHAN)
                     continue;
                 /* If no channel device, quick exit */
                 if (chan_unit[chan2].flags & UNIT_DIS ||
                      CHAN_G_TYPE(chan_unit[chan2].flags) != CHAN_754) {
                        flags |= 0x440; /* Set I/O Check */
                        selreg2 = 0;
                        continue;
                 }

                 /* Channel is busy doing something, wait */
                 if (chan_flags[chan2] & (DEV_SEL| DEV_DISCO|STA_TWAIT|STA_WAIT|
                                STA_ACTIVE)) {
                       chan_flags[chan] |= STA_PEND;
                       /* Try again */
                       continue;
                 }

                 /* Issue another command */
                 switch(chan_issue_cmd(chan2, IO_RDS, selreg2 & 0x7fff)) {
                 case SCPE_BUSY:        /* Try again */
                        chan_flags[chan] |= STA_PEND;
                        break;
                 case SCPE_NODEV:
                 case SCPE_IOERR:
                        /* Something wrong, stop */
                        flags |= 0x440; /* Set I/O Check */
                        selreg2 = 0;
                        break;
                 case SCPE_OK:
                        chan_flags[chan2] |= STA_ACTIVE;
                        selreg2 &= 0x7fff;
                        chwait = chan2+1;       /* Change wait channel */
                        break;
                  }
             } else {
                /* No pending, just store last address in MAC2 */
                  MAC2 = caddr[chan];
                  selreg2 = 0;
             }
             continue;
        }

        /* If channel not active, don't process anything */
        if ((chan_flags[chan] & STA_ACTIVE) == 0)
             continue;

        if ((chan_flags[chan] & (CTL_READ|CTL_WRITE)) &&
                (chan_flags[chan] & (CTL_END|SNS_UEND))) {
            if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= DEV_WEOR|DEV_DISCO;
            chan_flags[chan] &= ~(SNS_UEND|CTL_END|CTL_READ|CTL_WRITE);
        }

        /* If device requested attention, abort current command */
        if (chan_flags[chan] & CHS_ATTN) {
             if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_EXP, &chan_dev, "chan %d Attn %d\n",
                              chan, irqdev[chan]);
             switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
             case CHAN_UREC:
             case CHAN_754:
                  if (selreg2 != 0)
                        chan_flags[chan] |= STA_PEND;
                  if (chan_flags[chan] & CHS_ERR)
                      flags |= 0x40; /* Set I/O Check */
                  break;
             case CHAN_7621:
             case CHAN_7908:
                  irqflags |= 1 << chan;
                  if (chan_dev.dctrl & cmask)
                      sim_debug(DEBUG_EXP, &chan_dev, "chan %d IRQ %x\n",
                              chan, irqdev[chan]);
                  break;
             }
             chan_flags[chan] &= ~(CHS_ATTN|STA_ACTIVE|STA_WAIT|DEV_WRITE);
             cmd[chan] &= ~CHAN_RECCNT;
             unit = irqdev[chan];
             if (chan_flags[chan] & CHS_EOF)
                 ioflags[unit/8] |= (1 << (unit & 07));
             flags |= 0x400; /* Set Any flag */
             /* Disconnect if selected */
             if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= (DEV_DISCO);
             continue;
        }

        /* If channel action all done, finish operation */
        if (((chan_flags[chan] & (DEV_SEL|STA_ACTIVE|STA_WAIT)) == STA_ACTIVE)
                 && (chan_flags[chan] & (CTL_CNTL|CTL_PREAD|CTL_PWRITE|CTL_READ|
                        CTL_WRITE|CTL_SNS)) == 0) {
            switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
            case CHAN_UREC:
            case CHAN_754:
                  if (selreg2 != 0)
                        chan_flags[chan] |= STA_PEND;
                  break;
            case CHAN_7621:
            case CHAN_7908:
                  irqflags |= 1 << chan;
                  if (chan_dev.dctrl & cmask)
                      sim_debug(DEBUG_EXP, &chan_dev, "chan %d IRQ %x\n",
                              chan, irqdev[chan]);
                  break;
            }
            chan_flags[chan] &= ~(STA_ACTIVE|DEV_WRITE);
            if (chan_flags[chan] & CHS_EOF) {
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_EXP, &chan_dev, "chan %d EOF %x\n",
                              chan, irqdev[chan]);
                unit = irqdev[chan];
                ioflags[unit/8] |= (1 << (unit & 07));
                chan_flags[chan] &= ~CHS_EOF;
                chan_flags[chan] |= CHS_ERR;
                flags |= 0x400; /* Set Any flag */
            }
            continue;
        }

        switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_UREC:
        case CHAN_754:
             /* If device put up EOR, terminate transfer. */
             if (chan_flags[chan] & (DEV_REOR|DEV_WEOR)) {
                  if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_EXP, &chan_dev, "chan %d EOR\n",
                                   chan);
                  if (selreg2 != 0)
                        chan_flags[chan] |= STA_PEND;
                  chan_flags[chan] &= ~(STA_ACTIVE|STA_WAIT|DEV_WRITE|DEV_REOR);
                  /* Disconnect if selected */
                  if (chan_flags[chan] & DEV_SEL)
                     chan_flags[chan] |= (DEV_DISCO);
                  continue;
             }
             break;

        case CHAN_7621:
             /* Waiting on unit ready, or command */
             if (chan_flags[chan] & STA_WAIT) {
                if ((chan_flags[chan] & STA_TWAIT) == 0) {
                     /* Device ready, see if command under record count */
                     if (cmd[chan] & CHAN_CMD) {
                         /* Done if EOF set */
                         if (chan_flags[chan] & CHS_EOF) {
                              cmd[chan] &= ~(CHAN_RECCNT|CHAN_CMD);
                              chan_flags[chan] &= ~(STA_WAIT);
                              continue; /* On to next channel */
                         }

                         /* Issue another command */
                         switch(chan_issue_cmd(chan, 0xff & (cmd[chan] >> 9),
                                irqdev[chan])) {
                         case SCPE_BUSY:
                                continue;
                         case SCPE_OK:
                                /* If started, drop record count */
                                if (chan_decr_reccnt(chan)){
                                     cmd[chan] &= ~(CHAN_RECCNT|CHAN_CMD);
                                     chan_flags[chan] &= ~(STA_WAIT);
                                     continue;  /* On to next channel */
                                }
                                continue;
                         case SCPE_NODEV:
                         case SCPE_IOERR:
                             /* Something wrong, stop */
                                cmd[chan] &= ~(CHAN_RECCNT|CHAN_CMD);
                                break;
                         }
                         continue; /* On to next channel */
                     }
                     chan_flags[chan] &= ~(STA_WAIT);
                 }
                 continue;      /* On to next channel */
             }

             /* If device put up EOR, terminate transfer. */
             if (chan_flags[chan] & DEV_REOR) {
                  int ch;
                  if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_EXP, &chan_dev, "chan %d EOR\n",
                                   chan);
                 /* If reading, check if partial word read */
                  if ((chan_flags[chan] & DEV_WRITE) == 0) {
                      unit = 512 + chan_unit[chan].u3 * 32;
                      unit += (cmd[chan] & CHAN_BFLAG)? 16: 24;
                      ch = AC[unit + 5];
                      if (ch != 10) {
                        /* Yes, fill with group marks and mark as full */
                          while(ch < 5)
                              AC[unit+ch++] = CHR_GM;
                          cmd[chan] |= (cmd[chan] & CHAN_BFLAG)?
                                     CHAN_BFULL: CHAN_AFULL;
                      }
                  }
                  if (cmd[chan] & CHAN_RECCNT) {
                      if (!chan_decr_reccnt(chan)) {
                           chan_flags[chan] &= ~DEV_REOR;
                           continue;
                      }
                      cmd[chan] &= ~CHAN_RECCNT;
                  }
                  chan_flags[chan] &= ~(DEV_REOR|DEV_WEOR);
                  /* Disconnect if selected */
                  if (chan_flags[chan] & DEV_SEL)
                     chan_flags[chan] |= (DEV_DISCO);
            }

            /* Channel gave us a Write EOR, terminate it needed. */
            if (chan_flags[chan] & DEV_WEOR) {
                  if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_EXP, &chan_dev, "chan %d WEOR\n",
                                   chan);
                  if (cmd[chan] & CHAN_RECCNT) {
                      if (!chan_decr_reccnt(chan)) {
                           chan_flags[chan] &= ~DEV_WEOR;
                           cmd[chan] &= ~CHAN_END;
                           continue;
                      }
                      cmd[chan] &= ~CHAN_RECCNT;
                  }
                  chan_flags[chan] &= ~(DEV_WEOR|DEV_REOR);
                  /* Disconnect if selected */
                  if (chan_flags[chan] & DEV_SEL)
                     chan_flags[chan] |= (DEV_DISCO);
            }

            if ((chan_flags[chan] & DEV_WRITE) &&
                (cmd[chan] & (CHAN_AFULL|CHAN_BFULL)) !=
                         (CHAN_AFULL|CHAN_BFULL)) {
                char    ch;
                unit = 512 + chan_unit[chan].u3 * 32;
                if (cmd[chan] & CHAN_END)
                    break;
                addr = chan_next_addr(chan);
                if ((cmd[chan] & CHAN_AFULL) == 0) {
                        unit += 24;
                        cmd[chan] |= CHAN_AFULL;
                        ch = 'a';
                } else if ((cmd[chan] & CHAN_BFULL) == 0) {
                        unit += 16;
                        cmd[chan] |= CHAN_BFULL;
                        ch = 'b';
                } else {
                        break;
                }
                AC[unit] = M[addr++];
                AC[unit+1] = M[addr++];
                AC[unit+2] = M[addr++];
                AC[unit+3] = M[addr++];
                AC[unit+4] = M[addr++];
                AC[unit+5] = 10;
                  if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_DATA, &chan_dev,
                                "chan %d (%d) > %c %02o%02o%02o%02o%02o\n",
                                chan, addr-5, ch,
                                AC[unit], AC[unit+1], AC[unit+2], AC[unit+3],
                                AC[unit+4]);

                if (cmd[chan] & CHAN_NOREC && (addr % 20000) == 0)
                   cmd[chan] |= CHAN_END;
                /* Wrap around if over end of memory, set error */
                if (addr > EMEMSIZE) {
                   chan_flags[chan] |= CHS_ERR;
                   /* addr -= EMEMSIZE; */
                   if (chan_dev.dctrl & cmask)
                       sim_debug(DEBUG_EXP, &chan_dev, "write wrap %d\n", chan);
                }
                break;
            }
            while ((chan_flags[chan] & DEV_WRITE) == 0&&
                (cmd[chan] & (CHAN_AFULL|CHAN_BFULL)) != 0) {
                char    ch;
                unit = 512 + chan_unit[chan].u3 * 32;
                addr = chan_next_addr(chan);
                if (cmd[chan] & CHAN_AFULL) {
                        unit += 24;
                        cmd[chan] &= ~CHAN_AFULL;
                        ch = 'a';
                } else if (cmd[chan] & CHAN_BFULL) {
                        unit += 16;
                        cmd[chan] &= ~CHAN_BFULL;
                        ch = 'b';
                } else {
                        break;
                }
                if ((cmd[chan]& CHAN_SKIP) == 0) {
                    M[addr++] = AC[unit];
                    M[addr++] = AC[unit+1];
                    M[addr++] = AC[unit+2];
                    M[addr++] = AC[unit+3];
                    M[addr++] = AC[unit+4];
                    if (addr > EMEMSIZE) {
                        cmd[chan] |= CHAN_SKIP;
                        chan_flags[chan] |= CHS_ERR;
                       if (chan_dev.dctrl & cmask)
                       sim_debug(DEBUG_EXP, &chan_dev, "read wrap %d\n", chan);
                    }
                    if (chan_dev.dctrl & cmask)
                         sim_debug(DEBUG_DATA, &chan_dev,
                                "chan %d (%d) < %c %02o%02o%02o%02o%02o\n",
                                 chan, addr-5, ch,
                                AC[unit], AC[unit+1], AC[unit+2], AC[unit+3],
                                AC[unit+4]);
                }
                AC[unit+5] = 10;
            }
            break;
        case CHAN_7908:
            switch(chan_flags[chan] & (DEV_WRITE|DEV_FULL)) {
            case 0:
            case DEV_WRITE|DEV_FULL:
                continue;
            case DEV_WRITE:
                if (cmd[chan] & CHAN_END)
                    break;
                unit = 1024 + chan_unit[chan].u3 * 32;
                addr = chan_next_addr(chan);
                assembly[chan] = M[addr++] & 077;
                assembly[chan] |= (M[addr++] & 077) << 6;
                assembly[chan] |= (M[addr++] & 077) << 12;
                assembly[chan] |= (M[addr++] & 077) << 18;
                assembly[chan] |= (M[addr++] & 077) << 24;
                if (cmd[chan] & CHAN_NOREC && (addr % 20000) == 19999)
                   cmd[chan] |= CHAN_END;
                bcnt[chan] = 0;
                chan_flags[chan] |= DEV_FULL;
                break;
            case DEV_FULL:
                unit = 1024 + chan_unit[chan].u3 * 32;
                addr = chan_next_addr(chan);
                if ((cmd[chan]& CHAN_SKIP) == 0) {
                    M[addr++] = assembly[chan] & 077;
                    M[addr++] = (assembly[chan] >> 6) & 077;
                    M[addr++] = (assembly[chan] >> 12) & 077;
                    M[addr++] = (assembly[chan] >> 18) & 077;
                    M[addr++] = (assembly[chan] >> 24) & 077;
                }
                if (addr > EMEMSIZE) {
                   cmd[chan] |= CHAN_SKIP;
                   chan_flags[chan] |= CHS_ATTN;
                }
                bcnt[chan] = 0;
                chan_flags[chan] &= ~DEV_FULL;
                break;
            }
            break;
        }
    }
}

void chan_set_attn_inq(int chan) {
/*    inquiry = 1; */
}

void chan_clear_attn_inq(int chan) {
/*    inquiry = 0; */
}


/* Issue a command to a channel */
int
chan_cmd(uint16 dev, uint16 dcmd, uint32 addr)
{
    int                 chan;
    int                 unit;
    t_stat              r;
    int                 op;

    /* Find device on given channel and give it the command */
    chan = chan_mapdev(dev);
    if (chan < 0 || chan >= NUM_CHAN)
        return SCPE_IOERR;
    /* If no channel device, quick exit */
    if (chan_unit[chan].flags & UNIT_DIS)
        return SCPE_IOERR;
    /* Unit is busy doing something, wait */
    if (chan_flags[chan] & (DEV_SEL| DEV_DISCO|STA_TWAIT|STA_WAIT|STA_ACTIVE))
        return SCPE_BUSY;

    /* Ok, try and find the unit */
    caddr[chan] = addr;
    assembly[chan] = 0;
    op = dcmd >> 8;
    cmd[chan] &= CHAN_RECCNT;   /* Clear everything but record count flag */
    cmd[chan] |= dcmd & CHAN_ZERO;
    if (op == IO_RDS && (dcmd & 0xf) != 0) {
        switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_754:
        case CHAN_UREC:
            cmd[chan] |= CHAN_SKIP;
            break;
        case CHAN_7621:
            switch(dcmd & 0xf) {
            case 1:     cmd[chan] |= CHAN_SKIP; break;
            case 2:
                unit = 8+512 + chan_unit[chan].u3 * 32;
                M[addr++] = AC[unit++];
                M[addr++] = AC[unit++];
                M[addr++] = AC[unit++];
                M[addr++] = AC[unit++];
                cmd[chan] &= ~CHAN_RECCNT;
                return SCPE_OK;
            default:
                break;
            }
            break;
        case CHAN_7908:
            switch(dcmd & 0xf) {
            case 1:     cmd[chan] |= CHAN_SKIP;
            case 0:     chan_flags[chan] |= CTL_READ; break;
            case 3:     chan_flags[chan] |= CTL_SNS; break; /* Do Sense */
            case 4:     /* Do control then read */
                chan_flags[chan] |= CTL_CNTL|CTL_PREAD;
                break;
            }
            break;
        }
    }
    if (op == IO_WRS && (dcmd & 0xf) != 0) {
        switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_754:
        case CHAN_UREC:
            cmd[chan] |= CHAN_NOREC;
            break;
        case CHAN_7621:
            dcmd &= ~ CHAN_ZERO;
            switch(dcmd & 0xf) {
            case 1:     cmd[chan] |= CHAN_NOREC; break;
            case 2:
                unit = 512 + chan_unit[chan].u3 * 32;
                addr /= 10;
                AC[unit+1] = addr % 10;
                if (AC[unit+1] == 0)
                   AC[unit+1] = 10;
                addr /= 10;
                AC[unit+2] = addr % 10;
                addr /= 10;
                if (AC[unit+2] == 0)
                   AC[unit+2] = 10;
                AC[unit+3] = addr % 10;
                if (AC[unit+3] == 0)
                   AC[unit+3] = 10;
                if (chan_dev.dctrl & (0x0100 << chan))
                           sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "chan %d set reccnt %02o %02o %02o\n",
                                chan, AC[unit + 3], AC[unit + 2], AC[unit + 1]);
                cmd[chan] |= CHAN_RECCNT;
                return SCPE_OK;
            default:
                break;
            }
            break;
        case CHAN_7908:
            switch(dcmd & 0xf) {
            case 1:     cmd[chan] |= CHAN_NOREC;
            case 0:     chan_flags[chan] |= CTL_WRITE; break;
            case 3:     /* Do control */
                        chan_flags[chan] |= CTL_CNTL; break;
            case 4:     /* Do control then write */
                        chan_flags[chan] |= CTL_CNTL|CTL_PWRITE; break;
            }
            break;
        }
    }
    /* Handle initial record count of zero for special ops */
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7621 &&
                cmd[chan] & CHAN_RECCNT) {
        switch(op) {
        case IO_WEF:
        case IO_ERG:
        case IO_BSR:
                if (chan_zero_reccnt(chan)) {
                    /* Just check if unit ready */
                    r = chan_issue_cmd(chan, OP_TRS, dev);
                    if (r == SCPE_OK)
                       cmd[chan] &= ~CHAN_RECCNT;
                    return r;
                }
                break;
        default:        /* Don't worry about it, not effected by RC */
                break;
        }
    }

    chan_flags[chan] &= ~(CTL_CNTL|CTL_READ|CTL_WRITE|SNS_UEND|CTL_WRITE|
                                CTL_SNS|CHS_ATTN);

    r = chan_issue_cmd(chan, op, dev);
    if (r == SCPE_OK)
        chan_flags[chan] &= ~(CHS_EOF|CHS_ERR|CHS_ATTN);
    /* Activate channel if select raised */
    if (r == SCPE_OK && chan_flags[chan] & DEV_SEL) {
        chan_flags[chan] |= STA_ACTIVE;
        irqdev[chan] = dev;
        irqflags &= ~(1 << chan);
        ioflags[dev/8] &= ~(1 << (dev & 07));
        /* Set starting address. */
        switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
        case CHAN_754:
        case CHAN_UREC:
            if (op == IO_RDS) {
                if (selreg2 & 0x8000)
                    caddr[chan] = MAC2;
                selreg2 &= 0x7fff;
            }
            chwait = chan+1;
            break;
        case CHAN_7621:
            unit = 512 + chan_unit[chan].u3 * 32;
            AC[unit+16+5] = 10; /* Set digit next to 0 */
            AC[unit+24+5] = 10;
            store_addr(caddr[chan], 8 + unit);
            if (cmd[chan] & CHAN_RECCNT && chan_zero_reccnt(chan)) {
                cmd[chan] &= ~CHAN_RECCNT;
            }
            break;
        case CHAN_7908:
            store_addr(caddr[chan], 8+1024 + chan_unit[chan].u3 * 32);
            break;
        }
    }
    if (r == SCPE_OK && CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_7621) {
        switch(op) {
        case IO_WEF:
        case IO_ERG:
        case IO_BSR:
            if (cmd[chan] & CHAN_RECCNT && chan_zero_reccnt(chan)) {
                cmd[chan] &= ~CHAN_RECCNT;
            }
            if (cmd[chan] & CHAN_RECCNT) {
                chan_decr_reccnt(chan);
                cmd[chan] &= CHAN_RECCNT;
                cmd[chan] |= (op << 9) | CHAN_CMD;
            }
            /* Fall through */

        case IO_SKR:
        case IO_BSF:
        case IO_REW:
        case IO_RUN:
            chan_flags[chan] |= STA_ACTIVE|STA_WAIT;
            irqdev[chan] = dev;
            irqflags &= ~(1 << chan);
            ioflags[dev/8] &= ~(1 << (dev & 07));
        default:
            break;
        }
    }
    return r;
}

/*
 * Process the CHR 3 13 command and abort all channel activity
 */
void
chan_chr_13()
{
    int                 chan;

    /* Scan channels looking for work */
    for (chan = 0; chan < NUM_CHAN; chan++) {
        /* Skip if channel is disabled */
        if (chan_unit[chan].flags & UNIT_DIS)
            continue;

       /* If channel is disconnecting, do nothing */
        if (chan_flags[chan] & DEV_DISCO)
             continue;

        /* If channel not active, don't process anything */
        if ((chan_flags[chan] & STA_ACTIVE) == 0)
             continue;

        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_WEOR|DEV_DISCO;

        chan_flags[chan] &= ~(CHS_ATTN|STA_ACTIVE|STA_WAIT);
    }

    irqflags = 0;
}


/*
 * Write a word to the assembly register.
 */
int
chan_write(int chan, t_uint64 * data, int flags)
{
    return TIME_ERROR;
}

/*
 * Read next word from assembly register.
 */
int
chan_read(int chan, t_uint64 * data, int flags)
{
    return TIME_ERROR;
}

/*
 * Write a char to the assembly register.
 */
int
chan_write_char(int chan, uint8 * data, int flags)
{
    uint8       ch = *data;
    int         unit;
    uint16      msk;

    /* Based on channel type get next character */
    switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
    case CHAN_754:
    case CHAN_UREC:
        if (*data == 0)
           *data = 020;
        if (caddr[chan] > EMEMSIZE) {
           cmd[chan] |= CHAN_SKIP;
           chan_flags[chan] |= CHS_ATTN;
        }
        if ((cmd[chan] & CHAN_SKIP) == 0)
            M[caddr[chan]] = *data;
        caddr[chan]++;
        break;
    case CHAN_7621:
        if (*data == 0)
           *data = 020;
        /* If no data, then return timing error */
        if ((cmd[chan] & (CHAN_AFULL|CHAN_BFULL)) == (CHAN_AFULL|CHAN_BFULL))
              return TIME_ERROR;
        unit = 512 + chan_unit[chan].u3 * 32;
        if ((cmd[chan] & CHAN_BFLAG) == 0 && (cmd[chan] & CHAN_AFULL) == 0) {
                unit += 24;
                msk = CHAN_AFULL;
        } else if ((cmd[chan] & CHAN_BFLAG) && (cmd[chan] & CHAN_BFULL) == 0) {
                unit += 16;
                msk = CHAN_BFULL;
        } else { /* We are off sync, or something */
                /* We have data, so switch CHAN_BFLAG and try other buffer */
                cmd[chan] ^= CHAN_BFLAG;
                unit += (cmd[chan] & CHAN_BFLAG)? 16: 24;
                msk = (cmd[chan] & CHAN_BFLAG)? CHAN_BFULL: CHAN_AFULL;
                if (chan_dev.dctrl & (0x0100 << chan))
                           sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "switching buffer %d\n", chan);
        }
        ch = AC[5 + unit];
        if (ch == 10)
            ch = 0;
        AC[unit + ch] = *data & 077;
        if (chan_dev.dctrl & (0x0100 << chan))
                   sim_debug(DEBUG_DATA, &chan_dev,
                                 "%d < %02o (%d)\n", chan, *data, ch);
        AC[5 + unit] = ch + 1;
        if (ch == 4) {
            cmd[chan] |= msk;
            cmd[chan] ^= CHAN_BFLAG;
        }
        break;
    case CHAN_7908:
        if (bcnt[chan] > 4)
           return TIME_ERROR;
        if (chan_flags[chan] & CTL_SNS) {
           *data &= 027;
           *data |= 040;
        } else {
           if (*data == 0)
                *data = 020;
        }
        assembly[chan] |= *data << (6 * bcnt[chan]);
        bcnt[chan]++;
        if (bcnt[chan] == 5)
           chan_flags[chan] |= DEV_FULL;
    }


    /* If device gave us an end, terminate transfer */
    if (flags & DEV_REOR) {
        chan_flags[chan] |= DEV_REOR|DEV_FULL;
        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_DISCO;
        return END_RECORD;
        /* If over size of memory, terminate */
    } else if (!MEM_ADDR_OK(caddr[chan])) {
        chan_flags[chan] |= DEV_REOR|DEV_FULL;
        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_DISCO;
        return END_RECORD;
    }

    return DATA_OK;
}

/*
 * Read next char from assembly register.
 */
int
chan_read_char(int chan, uint8 * data, int flags)
{
    uint8       ch;
    int         unit;
    uint16      msk;

    /* Check if he write out last data */
    if ((chan_flags[chan] & STA_ACTIVE) == 0)
        return TIME_ERROR;

    /* Based on channel type get next character */
    switch(CHAN_G_TYPE(chan_unit[chan].flags)) {
    case CHAN_754:
    case CHAN_UREC:
        *data = M[caddr[chan]];
        if (*data == CHR_BLANK)
            *data = CHR_ABLANK;
        if (cmd[chan] & CHAN_ZERO && *data != CHR_GM)
            M[caddr[chan]] = CHR_BLANK;
        if (chan_dev.dctrl & (0x0100 << chan))
                   sim_debug(DEBUG_DATA, &chan_dev,
                                 "%d > %02o (%d)\n", chan, *data, *data);
        caddr[chan]++;
        if ((cmd[chan] & CHAN_NOREC && (caddr[chan] % 19999) == 0)) {
            chan_flags[chan] |= DEV_WEOR;
            return END_RECORD;
        }
        break;
    case CHAN_7621:
        /* If no data, then return timing error */
        if ((cmd[chan] & (CHAN_AFULL|CHAN_BFULL)) == 0) {
              if (cmd[chan] & CHAN_END) {
                  chan_flags[chan] |= DEV_WEOR;
                  return END_RECORD;
              }
              return TIME_ERROR;
        }
        unit = 512 + chan_unit[chan].u3 * 32;
        if ((cmd[chan] & CHAN_BFLAG) == 0 && (cmd[chan] & CHAN_AFULL)) {
                unit += 24;
                msk = ~CHAN_AFULL;
        } else if ((cmd[chan] & CHAN_BFLAG) && (cmd[chan] & CHAN_BFULL)) {
                unit += 16;
                msk = ~CHAN_BFULL;
        } else { /* We are off sync, or something */
                /* We have data, so switch CHAN_BFLAG and try other buffer */
                cmd[chan] ^= CHAN_BFLAG;
                unit += (cmd[chan] & CHAN_BFLAG)? 16: 24;
                msk = (cmd[chan] & CHAN_BFLAG)? ~CHAN_BFULL: ~CHAN_AFULL;
                if (chan_dev.dctrl & (0x0100 << chan))
                           sim_debug(DEBUG_DETAIL, &chan_dev,
                                 "switching buffer %d\n", chan);
        }
        ch = AC[5 + unit];
        if (ch == 10)
            ch = 0;
        *data = AC[unit + ch] & 077;
        if (*data == CHR_BLANK)
            *data = CHR_ABLANK;
        if (chan_dev.dctrl & (0x0100 << chan))
                   sim_debug(DEBUG_DATA, &chan_dev,
                                 "%d > %02o (%d)\n", chan, *data, ch);
        AC[5 + unit] = ch + 1;
        if (ch == 4) {
            cmd[chan] &= msk;
            cmd[chan] ^= CHAN_BFLAG;
        }
        break;
    case CHAN_7908:
        if (bcnt[chan] > 4)
           return TIME_ERROR;
        *data = assembly[chan] >> (6 * bcnt[chan]);
        if (*data == CHR_BLANK)
            *data = CHR_ABLANK;
        if (chan_flags[chan] & CTL_CNTL && *data == CHR_GM) {
           chan_flags[chan] |= (chan_flags[chan] & (CTL_PREAD|CTL_PWRITE)) << 2;
           chan_flags[chan] &= ~(CTL_CNTL|CTL_PREAD|CTL_PWRITE);
           if (chan_flags[chan] & CTL_READ) {
                chan_next_addr(chan);
                chan_next_addr(chan);
           }
           bcnt[chan] = 0;
           return END_RECORD;
        }

        bcnt[chan]++;
        if (bcnt[chan] == 5)
           chan_flags[chan] &= ~DEV_FULL;
    }

    /* Check if we hit group mark */
    if ((cmd[chan] & CHAN_NOREC) == 0 && *data == CHR_GM) {
        chan_flags[chan] |= DEV_WEOR;
        return END_RECORD;
    }

    /* If end of record, don't transfer any data */
    if (flags & DEV_REOR) {
        chan_flags[chan] &= ~(DEV_WRITE/*|STA_ACTIVE*/);
        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_DISCO;
        chan_flags[chan] |= DEV_REOR;
        return TIME_ERROR;
    } else
        chan_flags[chan] |= DEV_WRITE;
    return DATA_OK;
}


void
chan9_set_error(int chan, uint32 mask)
{
    if (chan_flags[chan] & mask)
        return;
    chan_flags[chan] |= mask;
}

t_stat
chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "%s\n", chan_description(dptr));
    fprintf(st, "The 7080 supports up to 10 channels. Channel 0 is for unit\n");
    fprintf(st, "record devices.  Channels 1 through 4 are for tape drives.\n\n");
    fprintf (st, "        7261            tapes on Data Synchronizer\n");
    fprintf (st, "        754             Standard 705 tape drives\n\n");
    fprintf (st, "Channels are fixed on the 7080.\n\n");
    fprintf (st, "Channel * is a puesdo channel for unit record devices.\n");

    fprintf(st, "\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
chan_description(DEVICE *dptr)
{
    return "IBM 7080 channel controller";
}


