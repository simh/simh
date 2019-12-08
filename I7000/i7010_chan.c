/* i7010_chan.c: IBM 7010 Channel simulator

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

   The system state for the IBM 7010 channel is:
   There is only one type of channel on  7010, and it will talk to
   All type devices.

   Common registers to all but PIO channels.
   ADDR<0:16>           Address of next command.
   CMD<0:6>             Channel command.
   ASM<0:32>            Assembled data from devices.

   Simulation registers to handle device handshake.
   STATUS<0:16>         Simulated register for basic channel status.
   SENSE<0:16>          Additional flags for 7907 channels.
*/

#include "i7010_defs.h"

extern UNIT         cpu_unit;
extern uint8        chan_seek_done[NUM_CHAN];   /* Channel seek finished */

#define CHAN_DEF        UNIT_DISABLE|CHAN_SET

t_stat              set_urec(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              get_urec(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat              chan_reset(DEVICE * dptr);
t_stat              chan_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *chan_description (DEVICE *dptr);


/* Channel data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

uint32              caddr[NUM_CHAN];            /* Channel memory address */
uint8               bcnt[NUM_CHAN];             /* Channel character count */
uint8               cmd[NUM_CHAN];              /* Current command */
uint16              irqdev[NUM_CHAN];           /* Device to generate interupts
                                                   for channel */
uint32              chunit[NUM_CHAN];           /* Channel unit */
uint8               assembly[NUM_CHAN];         /* Assembly register */
uint32              chan_flags[NUM_CHAN];       /* Unit status */
extern uint8        chan_io_status[NUM_CHAN];
extern uint8        inquiry;
extern uint8        urec_irq[NUM_CHAN];

#define CHAN_LOAD       0001            /* Channel in load mode */
#define CHAN_NOREC      0002            /* Don't stop at record */
#define CHAN_WM         0004            /* Sent word mark char */
#define CHAN_6BIT       0010            /* Send 6-8 bit command */
#define CHAN_DSK_SEEK   0020            /* Seek Command */
#define CHAN_DSK_DATA   0040            /* Command needs data */
#define CHAN_DSK_RD     0100            /* Command is read command */
#define CHAN_OVLP       0200            /* Channel ran overlaped */

const char     *chan_type_name[] = {
    "Polled", "Unit Record", "7010", "7010", "7010"};


/* Map commands to channel commands */
/* Commands are reversed to be way they are sent out */
uint8 disk_cmdmap[16] = { 0xff, 0x82, 0x84, 0x86, 0x00, 0x89, 0x88, 0x83,
                          0x87, 0x04, 0x80, 0xff, 0x85, 0xff, 0xff, 0xff};

UNIT                chan_unit[] = {
    {UDATA(NULL, CHAN_SET|UNIT_DIS, 0)},        /* Place holder channel */
    {UDATA(NULL, CHAN_SET|CHAN_S_TYPE(CHAN_7010)|UNIT_S_CHAN(1),0)},
    {UDATA(NULL, CHAN_SET|CHAN_S_TYPE(CHAN_7010)|UNIT_S_CHAN(2),0)},
    {UDATA(NULL, CHAN_SET|CHAN_S_TYPE(CHAN_7010)|UNIT_S_CHAN(3),0)},
    {UDATA(NULL, CHAN_SET|CHAN_S_TYPE(CHAN_7010)|UNIT_S_CHAN(4),0)},
};

REG                 chan_reg[] = {
    {BRDATA(ADDR, caddr, 10, 18, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(CMD, cmd, 8, 6, NUM_CHAN), REG_RO|REG_FIT},
    {BRDATA(FLAGS, chan_flags, 2, 32, NUM_CHAN), REG_RO|REG_FIT},
    {NULL}
};

MTAB                chan_mod[] = {
    {CHAN_MODEL, CHAN_S_TYPE(CHAN_7010), "7010", NULL, NULL,NULL,NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "UREC", "UREC", &set_urec, &get_urec,
     NULL},
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
    {"CH1", 0x0100 << 1},
    {"CH2", 0x0100 << 2},
    {"CH3", 0x0100 << 3},
    {"CH4", 0x0100 << 4},
    {0, 0}
};

DEVICE              chan_dev = {
    "CH", chan_unit, chan_reg, chan_mod,
    NUM_CHAN, 10, 18, 1, 8, 8,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, chn_debug,
    NULL, NULL, &chan_help, NULL, NULL, &chan_description
};

struct urec_t {
    uint16      addr;
    const char  *name;
} urec_devs[] = {
        {0100,  "CR"},
        {0200,  "LP"},
        {0400,  "CP"},
        {0000,  "NONE"},
        {0000,  NULL}
};


/* Sets the device that will interrupt on the channel. */
t_stat
set_urec(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 chan;
    int                 i;

    if (cptr == NULL)
        return SCPE_IERR;
    if (uptr == NULL)
        return SCPE_IERR;

    chan = UNIT_G_CHAN(uptr->flags);
    for(i = 0; urec_devs[i].name != NULL; i++)
        if (strcmp(cptr, urec_devs[i].name) == 0)
            break;
    if (urec_devs[i].name == NULL)
        return SCPE_ARG;

    irqdev[chan] = urec_devs[i].addr;
    return SCPE_OK;
}

t_stat
get_urec(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    int                 chan;
    int                 i;

    if (uptr == NULL)
        return SCPE_IERR;
    chan = UNIT_G_CHAN(uptr->flags);
    if (irqdev[chan] == 0) {
        fprintf(st, "UREC=NONE");
        return SCPE_OK;
    }
    for(i = 0; urec_devs[i].name != NULL; i++) {
        if (urec_devs[i].addr == irqdev[chan]) {
            fprintf(st, "UREC=%s", urec_devs[i].name);
            return SCPE_OK;
        }
    }
    fprintf(st, "UREC=%o", irqdev[chan]);
    return SCPE_OK;
}

t_stat
chan_reset(DEVICE * dptr)
{
    int                 i;

    /* Clear channel assignment */
    for (i = 0; i < NUM_CHAN; i++) {
        chan_flags[i] = 0;
        chunit[i] = 0;
        caddr[i] = 0;
        cmd[i] = 0;
        bcnt[i] = 0;
    }
    return chan_set_devs(dptr);
}

/* Channel selector characters */
uint8 chan_char[NUM_CHAN] = {0, CHR_RPARN, CHR_LPARN, CHR_QUEST, CHR_EXPL};

/* Boot from given device */
t_stat
chan_boot(int32 unit_num, DEVICE * dptr)
{
    /* Set IAR = 1 (done by reset), channel to read one
        record to location 1 */
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags);
    extern int          chwait;

    chwait = chan;      /* Force wait for channel */
    /* Set up channel to load into location 1 */
    caddr[chan] = 1;
    assembly[chan] = 0;
    cmd[chan] = CHAN_NOREC|CHAN_LOAD;
    chunit[chan] = unit_num;
    chan_flags[chan] |= STA_ACTIVE;
    return SCPE_OK;
}

t_stat
chan_issue_cmd(uint16 chan, uint16 dcmd, uint16 dev) {
    DEVICE            **dptr;
    DIB                *dibp;
    uint32              j;
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

        cmask = 0x0100 << chan;
       /* If channel is disconnecting, do nothing */
        if (chan_flags[chan] & DEV_DISCO)
             continue;

        if (chan_flags[chan] & CHS_EOF) {
             chan_io_status[chan] |= IO_CHS_COND;
             chan_flags[chan] &= ~CHS_EOF;
        }

        if (chan_flags[chan] & CHS_ERR) {
             chan_io_status[chan] |= IO_CHS_CHECK;
             chan_flags[chan] &= ~CHS_ERR;
        }

        if (cmd[chan] & CHAN_DSK_DATA) {
            if (chan_flags[chan] & DEV_REOR) {
                /* Find end of command */
                while(MEM_ADDR_OK(caddr[chan]) && M[caddr[chan]] != (WM|077)) {

                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_CHAN, &chan_dev, "%02o,", M[caddr[chan]]);
                        caddr[chan]++;
                }
                caddr[chan]++;
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_CHAN, &chan_dev, "chan %d fin\n", chan);
                /* Configure channel for data transfer */
                cmd[chan] &= ~CHAN_DSK_DATA;
                chan_flags[chan] |= (chan_flags[chan]&
                                                (CTL_PREAD|CTL_PWRITE))>>2;
                chan_flags[chan] &= ~(DEV_REOR|CTL_PREAD|CTL_PWRITE|CTL_CNTL);
                /* If no select, all done */
                if ((chan_flags[chan] & DEV_SEL) == 0)
                    chan_flags[chan] &= ~(CTL_READ|CTL_WRITE);
                /* Set direction if reading */
                if (chan_flags[chan] & CTL_READ)
                    chan_flags[chan] |= DEV_WRITE;
                /* Check if we should finish now */
                if ((chan_flags[chan] & (CTL_READ|CTL_WRITE)) == 0
                    || chan_flags[chan] & (SNS_UEND|CTL_END)) {
                    if (chan_flags[chan] & DEV_SEL)
                        chan_flags[chan] |= DEV_WEOR|DEV_DISCO;
                    if (cmd[chan] & CHAN_DSK_SEEK)
                        chan_flags[chan] &= ~(CTL_END);
                    else
                        chan_flags[chan] &= ~(STA_ACTIVE|SNS_UEND|CTL_END);
                    chan_io_status[chan] |= IO_CHS_DONE;
                }
                continue;
            }
        }

        if (cmd[chan] & CHAN_DSK_SEEK) {
            if (chan_seek_done[chan] || chan_flags[chan] & SNS_UEND) {
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_CHAN, &chan_dev, "chan %d seek done\n", chan);
                chan_flags[chan] &= ~(STA_ACTIVE|SNS_UEND);
                cmd[chan] &= ~CHAN_DSK_SEEK;
            }
            continue;
        }

        if ((chan_flags[chan] & (CTL_READ|CTL_WRITE)) &&
                (chan_flags[chan] & (CTL_END|SNS_UEND))) {
                if (chan_flags[chan] & DEV_SEL)
                    chan_flags[chan] |= DEV_WEOR|DEV_DISCO;
                chan_flags[chan] &= ~(STA_ACTIVE|SNS_UEND|CTL_END|CTL_READ
                               |CTL_WRITE);
                if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_CHAN, &chan_dev, "chan %d end\n", chan);
                cmd[chan] &= ~CHAN_DSK_SEEK;
                chan_io_status[chan] |= IO_CHS_DONE;
        }

        /* If device put up EOR, terminate transfer. */
        if (chan_flags[chan] & DEV_REOR) {
             if (chan_flags[chan] & DEV_WRITE) {
                 if ((cmd[chan] & (CHAN_LOAD|CHAN_WM)) == (CHAN_WM|CHAN_LOAD))
                    M[caddr[chan]++] = 035;
                 caddr[chan]++;
             } else {
                 if ((cmd[chan] & CHAN_NOREC) == 0 &&
                     (chan_flags[chan] & STA_WAIT) == 0) {
                     if (MEM_ADDR_OK(caddr[chan])) {
                         if (M[caddr[chan]++] != (WM|077)) {
                             if (MEM_ADDR_OK(caddr[chan])) {
                                 chan_io_status[chan] |= IO_CHS_WRL;
                                 if (!MEM_ADDR_OK(caddr[chan]+1)) {
                                     caddr[chan]++;
                                 }
                             }
                        }
                    } else {
                         chan_io_status[chan] |= IO_CHS_WRL;
                    }
                }
                if ((cmd[chan] & CHAN_NOREC) && MEM_ADDR_OK(caddr[chan])) {
                     chan_io_status[chan] |= IO_CHS_WRL;
                     if (!MEM_ADDR_OK(caddr[chan]+1)) {
                          chan_io_status[chan] &= ~IO_CHS_WRL;
                     }
                     caddr[chan]++;
                }
             }
             chan_flags[chan] &= ~(STA_ACTIVE|STA_WAIT|DEV_WRITE|DEV_REOR);
             chan_io_status[chan] |= IO_CHS_DONE;
             /* Disconnect if selected */
             if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= (DEV_DISCO);
             if (chan_dev.dctrl & cmask)
                sim_debug(DEBUG_EXP, &chan_dev, "chan %d EOR %d %o\n", chan,
                       caddr[chan], chan_io_status[chan]);
             continue;
        }

        if (((chan_flags[chan] & (DEV_SEL|STA_ACTIVE)) == STA_ACTIVE) &&
             (chan_flags[chan] & (CTL_CNTL|CTL_PREAD|CTL_PWRITE|CTL_READ|
                        CTL_WRITE|CTL_SNS)) == 0) {
            chan_flags[chan] &= ~STA_ACTIVE;
        }

        /* If device requested attention, abort current command */
        if (chan_flags[chan] & CHS_ATTN) {
             chan_flags[chan] &= ~(CHS_ATTN|STA_ACTIVE|STA_WAIT);
             chan_io_status[chan] |= IO_CHS_DONE|IO_CHS_COND;
             /* Disconnect if selected */
             if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= (DEV_DISCO);
             if (chan_dev.dctrl & cmask)
                    sim_debug(DEBUG_EXP, &chan_dev, "chan %d Attn %o\n",
                              chan, chan_io_status[chan]);
             continue;
        }
    }
}

void chan_set_attn_urec(int chan, uint16 addr) {
    if (irqdev[chan] == addr)
        urec_irq[chan] = 1;
}

void chan_set_attn_inq(int chan) {
    inquiry = 1;
}

void chan_clear_attn_inq(int chan) {
    inquiry = 0;
}


/* Issue a command to a channel */
int
chan_cmd(uint16 dev, uint16 dcmd, uint32 addr)
{
    uint32              chan;
    t_stat              r;

    /* Find device on given channel and give it the command */
    chan = (dev >> 12) & 0x7;
    /* If no channel device, quick exit */
    if (chan_unit[chan].flags & UNIT_DIS)
        return SCPE_IOERR;
    /* Unit is busy doing something, wait */
    if (chan_flags[chan] & (DEV_SEL|DEV_DISCO|STA_TWAIT|STA_WAIT|STA_ACTIVE))
        return SCPE_BUSY;
    /* Ok, try and find the unit */
    caddr[chan] = addr;
    assembly[chan] = 0;
    cmd[chan] = 0;
    if (dcmd & 0100)            /* Mod $ or X */
        cmd[chan] |= CHAN_NOREC;
    if (dcmd & 0200)            /* Opcode L */
        cmd[chan] |= CHAN_LOAD;
    else
        cmd[chan] |= CHAN_WM;   /* Force first char to have word mark set */
    dcmd = (dcmd >> 8) & 0x7f;
    chunit[chan] = dev;
    chan_flags[chan] &= ~(CTL_CNTL|CTL_READ|CTL_WRITE|SNS_UEND|CTL_WRITE
                         |CTL_SNS|STA_PEND);
    /* Handle disk device special */
    if ((dsk_dib.mask & dev) == (dsk_dib.addr & dsk_dib.mask)) {
        uint16  dsk_cmd = 0;
        dsk_cmd = disk_cmdmap[dev&017];
        /* Set up channel if command ok */
        if (dsk_cmd == 0xFF || dev & 060) {
           /* Set io error and abort */
           return SCPE_IOERR;
        }
        if (cmd[chan] & CHAN_LOAD) {
           cmd[chan] &= ~CHAN_LOAD;
           dsk_cmd = 0x100;
        } else {
           cmd[chan] |= CHAN_6BIT;
        }
        /* Try to start drive */
        r = chan_issue_cmd(chan, dsk_cmd, dev);
        if (r != SCPE_OK)
            return r;
        chan_flags[chan] |= CTL_CNTL;
        if (dcmd == IO_RDS)
           chan_flags[chan] |= CTL_PREAD;
        if (dcmd == IO_WRS)
           chan_flags[chan] |= CTL_PWRITE;
        if (dcmd == IO_TRS)
           chan_flags[chan] |= CTL_SNS;
        cmd[chan] |= CHAN_DSK_DATA;
        if ((dsk_cmd & 0xff) == 0x80 && cmd[chan] & CHAN_OVLP) {
           cmd[chan] |= CHAN_DSK_SEEK;
           chan_seek_done[chan] = 0;
        }
        chan_flags[chan] &= ~DEV_REOR;  /* Clear in case still set */
        chan_flags[chan] |= STA_ACTIVE;
        return r;
    }
    if ((com_dib.mask & dev) == (com_dib.addr & com_dib.mask)) {
        switch(dcmd) {
        case IO_RDS: chan_flags[chan] |= CTL_READ; break;
        case IO_WRS: chan_flags[chan] |= CTL_WRITE; break;
        case IO_TRS: chan_flags[chan] |= CTL_SNS; break;
        case IO_CTL: chan_flags[chan] |= CTL_CNTL; break;
        }
        if ((dev & 077) != 1)
           cmd[chan] |= CHAN_6BIT;
        r = chan_issue_cmd(chan, dcmd, dev);
        if (r == SCPE_OK)
            chan_flags[chan] |= STA_ACTIVE;
        return r;
    }

    r = chan_issue_cmd(chan, dcmd, dev);
    /* Activate channel if select raised */
    if (chan_flags[chan] & DEV_SEL) {
        chan_flags[chan] |= STA_ACTIVE;
    }
    return r;
}

/*
 * Write a word to the assembly register.
 */
int
chan_write(int chan, t_uint64 * data, int flags)
{
    /* Not implimented on this machine */
    return TIME_ERROR;
}

/*
 * Read next word from assembly register.
 */
int
chan_read(int chan, t_uint64 * data, int flags)
{
    /* Not implimented on this machine */
    return TIME_ERROR;
}

/*
 * Write a char to the assembly register.
 */
int
chan_write_char(int chan, uint8 * data, int flags)
{
    uint8       ch = *data;

    sim_debug(DEBUG_DATA, &chan_dev, "chan %d char %o %d %o %o\n", chan,
               *data, caddr[chan], chan_io_status[chan], flags);

    if (chan_flags[chan] & STA_WAIT) {
        sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d setWR %d %o\n", chan,
               caddr[chan], chan_io_status[chan]);
        chan_io_status[chan] |= IO_CHS_WRL;
        return END_RECORD;
    }

    /* Check if end of data */
    if ((chan_flags[chan] & STA_WAIT) == 0 && (cmd[chan] & CHAN_NOREC) == 0 &&
             M[caddr[chan]] == (WM|077)) {
        chan_flags[chan] |= STA_WAIT;   /* Saw group mark */
        chan_io_status[chan] |= IO_CHS_WRL;
        caddr[chan]++;
        sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d GEor %d %o\n", chan,
               caddr[chan], chan_io_status[chan]);
        return END_RECORD;
    }

    /* If over size of memory, terminate */
    if (!MEM_ADDR_OK(caddr[chan])) {
        chan_flags[chan] |= DEV_REOR;
        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_DISCO;
        chan_io_status[chan] |= IO_CHS_DONE;
        caddr[chan]++;
        sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d past mem %d %o\n", chan,
                caddr[chan], chan_io_status[chan]);
        chan_flags[chan] &= ~(DEV_WRITE|STA_ACTIVE);
        return DATA_OK;
    }

    /* If we are in load mode and see word mark, save it */
    if ((cmd[chan] & (CHAN_LOAD|CHAN_WM)) == CHAN_LOAD && ch == 035)
        cmd[chan] |= CHAN_WM;
    else {
        if (cmd[chan] & CHAN_6BIT)
            ch &= 077;
        if (cmd[chan] & CHAN_WM && ch != 035)
            ch |= WM;
        cmd[chan] &= ~CHAN_WM;
        if ((cmd[chan] & CHAN_LOAD) == 0)
            ch |= M[caddr[chan]] & WM;
        if ((chan_flags[chan] & DEV_REOR) == 0)
            M[caddr[chan]] = ch;
        caddr[chan]++;
    }


    /* If device gave us an end, terminate transfer */
    if (flags & DEV_REOR) {
        chan_flags[chan] |= DEV_REOR;
        sim_debug(DEBUG_DETAIL, &chan_dev, "chan %d Eor %d %o %x\n", chan,
                 caddr[chan], chan_io_status[chan], chan_flags[chan]);
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
        chan_io_status[chan] |= IO_CHS_DONE;
        return END_RECORD;
    }

    /* Check if he write out last data */
    if ((chan_flags[chan] & STA_ACTIVE) == 0)
        return TIME_ERROR;

    /* Send rest of command */
    if (cmd[chan] & CHAN_DSK_DATA) {
        *data = M[caddr[chan]];
        if (*data == (WM|077))
           return END_RECORD;
        *data &= 077;
        caddr[chan]++;
        return DATA_OK;
    }

    /* If we had a previous word mark send it */
    if ((cmd[chan] & (CHAN_LOAD|CHAN_WM)) == (CHAN_LOAD|CHAN_WM)) {
        *data = assembly[chan];
        cmd[chan] &= ~CHAN_WM;
    } else {
        if (!MEM_ADDR_OK(caddr[chan]+1)) {
            chan_flags[chan] &= ~STA_ACTIVE;
            if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= DEV_DISCO;
            caddr[chan]++;
            return END_RECORD;
        }
        assembly[chan] = M[caddr[chan]++];
        /* Handle end of record */
        if ((cmd[chan] & CHAN_NOREC) == 0 && assembly[chan] == (WM|077)) {
            chan_flags[chan] &= ~STA_ACTIVE;
            if (chan_flags[chan] & DEV_SEL)
                chan_flags[chan] |= DEV_DISCO;
            chan_io_status[chan] |= IO_CHS_DONE;
            return END_RECORD;
        }
        if (cmd[chan] & CHAN_LOAD &&
           (assembly[chan] & WM || assembly[chan] == 035)) {
            cmd[chan] |= CHAN_WM;
            assembly[chan] &= 077;
            *data = 035;
            return DATA_OK;
        }
        if (cmd[chan] & CHAN_6BIT)
            *data &= 077;
        *data = assembly[chan];
    }

    /* If end of record, don't transfer any data */
    if (flags & DEV_REOR) {
        chan_flags[chan] &= ~(DEV_WRITE|STA_ACTIVE);
        if (chan_flags[chan] & DEV_SEL)
            chan_flags[chan] |= DEV_DISCO;
        chan_io_status[chan] |= IO_CHS_DONE;
        chan_flags[chan] |= DEV_REOR;
        return END_RECORD;
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
    fprintf (st, "%s\n\n", chan_description(dptr));
    fprintf (st, "The 7010 supports up to 4 channels.  Channel models include\n\n");
    fprintf (st, "   Channel * is for unit record devices.\n");
    fprintf (st, "   Channels 1-4 are 7010  multiplexor channel\n\n");
    fprintf (st, "Channels are fixed on the 7010.\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
chan_description(DEVICE *dptr)
{
    return "IBM 7010 channel controller";
}

