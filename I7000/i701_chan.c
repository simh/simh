/* i701_chan.c: IBM 701 Channel simulator

   Copyright (c) 2005, Richard Cornwell

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

   There is no channel on the 701, this module just provides basic support
   for polled mode devices.

   Simulated register for the channel is:
   STATUS<0:16>         Simulated register for basic channel status.
*/

#include "i7090_defs.h"

extern uint8        iocheck;
extern UNIT         cpu_unit;
extern uint16       IC;
extern t_uint64     MQ;

t_stat              chan_reset(DEVICE * dptr);
void                chan_fetch(int chan);
t_stat              chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *chan_description (DEVICE *dptr);
uint32              dly_cmd(UNIT *, uint16, uint16);

/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

t_uint64            assembly[NUM_CHAN];       /* Assembly register */
uint32              chan_flags[NUM_CHAN];     /* Unit status */
uint8               bcnt[NUM_CHAN];           /* Character count */

const char     *chan_type_name[] = {
    "Polled", "", "", "", ""};


/* Delay device for IOD instruction */
DIB                 dly_dib =
    { CH_TYP_PIO, 1, 2052, 07777, &dly_cmd, NULL };


UNIT                chan_unit[] = {
    /* Puesdo channel for 701 devices */
    {UDATA(NULL, UNIT_DISABLE | CHAN_SET |
                        CHAN_S_TYPE(CHAN_PIO)|UNIT_S_CHAN(0), 0)},
};

REG                 chan_reg[] = {
    {BRDATAD(ASM, assembly, 8, 36, NUM_CHAN, "Channel Assembly Register"),
           REG_RO|REG_FIT},
    {BRDATAD(FLAGS, chan_flags, 2, 32, NUM_CHAN, "Channel flags"),
           REG_RO|REG_FIT},
    {NULL}
};

MTAB                chan_mod[] = {
    {0}
};

DEVICE              chan_dev = {
    "CH", chan_unit, chan_reg, chan_mod,
    NUM_CHAN, 8, 15, 1, 8, 36,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    &dly_dib, DEV_DEBUG, 0, NULL,
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

    IC = 0;
    chan_flags[chan] |= STA_ACTIVE;
    chan_flags[chan] &= ~STA_PEND;
    return SCPE_OK;
}


/* Execute the next channel instruction. */
void
chan_proc()
{
    if (chan_flags[0] & CHS_ATTN) {
        chan_flags[0] &= ~(CHS_ATTN | STA_START | STA_ACTIVE | STA_WAIT);
        if (chan_flags[0] & DEV_SEL)
            chan_flags[0] |= (DEV_DISCO);
    }
}



/* Issue a command to a channel */
int
chan_cmd(uint16 dev, uint16 dcmd)
{
    UNIT               *uptr;
    int32               chan;
    DEVICE            **dptr;
    DIB                *dibp;
    int                 j;

    /* Find device on given channel and give it the command */
    chan = (dev >> 9) & 017;
    if (chan >= NUM_CHAN)
        return SCPE_IOERR;
    /* If no channel device, quick exit */
    if (chan_unit[chan].flags & UNIT_DIS)
        return SCPE_IOERR;
    /* On 704 device new command aborts current operation */
    if (CHAN_G_TYPE(chan_unit[chan].flags) == CHAN_PIO &&
        (chan_flags[chan] & (DEV_SEL | DEV_FULL | DEV_DISCO)) == DEV_SEL) {
        chan_flags[chan] |= DEV_DISCO | DEV_WEOR;
        return SCPE_BUSY;
    }
    /* Unit is busy doing something, wait */
    if (chan_flags[chan] & (DEV_SEL | DEV_DISCO | STA_TWAIT | STA_WAIT))
        return SCPE_BUSY;
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
                    return r;
                }
            }
        }
    }
    return SCPE_NODEV;
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
            chan_flags[chan] &= ~(DEV_WEOR|STA_WAIT);
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;       /* We had error */
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }
        chan_flags[chan] |= DEV_DISCO;
        return TIME_ERROR;
    } else {
        if (chan == 0)
            MQ = *data;
        assembly[chan] = *data;
        bcnt[chan] = 6;
        chan_flags[chan] |= DEV_FULL;
        chan_flags[chan] &= ~DEV_WRITE;
        if (flags & DEV_REOR) {
            chan_flags[chan] |= DEV_REOR;
        }
    }

    /* If Writing end of record, abort */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_FULL | DEV_WEOR);
        return END_RECORD;
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
        chan_flags[chan] &= ~(DEV_WEOR);
        return END_RECORD;
    }

    /* Check if he write out last data */
    if ((chan_flags[chan] & DEV_FULL) == 0) {
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_WRITE;
            chan_flags[chan] &= ~(DEV_WEOR | STA_WAIT);
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }
        chan_flags[chan] |= DEV_DISCO;
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

    /* Check if last data still not taken */
    if (chan_flags[chan] & DEV_FULL) {
        /* Nope, see if we are waiting for end of record. */
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_REOR;
            chan_flags[chan] &= ~(DEV_WEOR|STA_WAIT);
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;       /* We had error */
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }
        chan_flags[chan] |= DEV_DISCO;
        return TIME_ERROR;
    } else {
        int     cnt = --bcnt[chan];
        t_uint64        wd = (chan == 0)? MQ:assembly[chan];
        wd &= 0007777777777LL;
        wd <<= 6;
        wd |= (*data) & 077;
        if (chan == 0)
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

    /* If Writing end of record, abort */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_FULL | DEV_WEOR);
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

    /* Return END_RECORD if requested */
    if (flags & DEV_WEOR) {
        chan_flags[chan] &= ~(DEV_WEOR);
        return END_RECORD;
    }

    /* Check if he write out last data */
    if ((chan_flags[chan] & DEV_FULL) == 0) {
        if (chan_flags[chan] & DEV_WEOR) {
            chan_flags[chan] |= DEV_WRITE;
            chan_flags[chan] &= ~(DEV_WEOR | STA_WAIT);
            return END_RECORD;
        }
        if (chan_flags[chan] & STA_ACTIVE) {
            chan_flags[chan] |= CHS_ATTN;
            if ((flags & DEV_DISCO) == 0)
                iocheck = 1;
        }
        chan_flags[chan] |= DEV_DISCO;
        return TIME_ERROR;
    } else {
        int     cnt = --bcnt[chan];
        t_uint64        wd = assembly[chan];
        *data = 077 & (wd >> 30);
        wd <<= 6;
        wd |= 077 & (wd >> 36);
        wd &= 0777777777777LL;
        if (chan == 0)
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
chan9_set_error(int chan, uint32 mask)
{
}

t_stat
chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
   fprintf(st, "IBM 701 Channel\n\n");
   fprintf(st, "Psuedo device to display IBM 701 I/O. The IBM 701 used polled");
   fprintf(st, " I/O,\nThe assembly register and the flags can be displayed\n");
   fprintf(st, "There are no options for the this device\n");
   return SCPE_OK;
}

const char          *chan_description (DEVICE *dptr) {
   return "IBM 701 Psuedo Channel";
}

