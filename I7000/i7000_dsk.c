/* i7090_disk.c: IBM 7090 Disk

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
   RICHARD CORNRWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Support for 1301/1302/2302 disks and 7238 drums

   Disks are represented in files as follows:

   Since these drives supported variable format for each cylinder the
   format is represented as one track per cylinder as follows:

     0          data
     1          header
     2          Home Address
     3          end of track

   These codes are packed 4 per byte and used to control read/write of
   data.

   After one format track per cylinder there is one record of bytes per
   track data for each track. First bytes are home address 2, followed
   by record address, and record data to cover number in format. All data
   is stored with the top 2 bits as zero.

   Limitiation of this are that the address field for each record can not
   be more then 16 bytes.

*/

#include "i7000_defs.h"

#ifdef NUM_DEVS_DSK
#define UNIT_DSK        UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX
#define FORMAT_OK       (1 << (UNIT_V_LOCAL+0))
#define HA2_OK          (1 << (UNIT_V_LOCAL+1))
#define CTSS_BOOT       (1 << (UNIT_V_MODE))

/* Device status information stored in u5 */
#define DSKSTA_CMD      0x0000100       /* Unit has recieved a cmd */
#define DSKSTA_DATA     0x0000200       /* Unit has finished cmd */
#define DSKSTA_WRITE    0x0000400       /* Last command was a write */
#define DSKSTA_CHECK    0x0000800       /* Doing a write check */
#define DSKSTA_CMSK     0x00000ff       /* Command mask */
#define DSKSTA_ARGMSK   0x0fff000       /* Command argument */
#define DSKSTA_ARGSHFT  12
#define DSKSTA_SCAN     0x1000000       /* Scanning for header */
#define DSKSTA_SKIP     0x2000000       /* Skiping record */
#define DSKSTA_XFER     0x4000000       /* Tranfser current record */
#define DSKSTA_DIRTY    0x8000000       /* Buffer needs to be written */

#define FMT_DATA        0               /* Data */
#define FMT_HDR         1               /* Header */
#define FMT_HA2         2               /* Home address 2 */
#define FMT_END         3               /* End of track */

/* Disk commands */
#define DNOP            0x00            /* Nop */
#define DREL            0x04            /* Release */
#define DEBM            0x08            /* Eight Bit mode */
#define DSBM            0x09            /* Six bit mode */
#define DSEK            0x80            /* Seek */
#define DVSR            0x82            /* Prepare to Verify single record */
#define DWRF            0x83            /* Prepare to Format */
#define DVTN            0x84            /* Prepare to Verify track no addr */
#define DVCY            0x85            /* Prepare to Verify Cyl */
#define DWRC            0x86            /* Prepare to Write Check */
#define DSAI            0x87            /* Set Access Inoperative */
#define DVTA            0x88            /* Prepare to Verify track addr */
#define DVHA            0x89            /* Prepare to Verify home addr */

/* Disk sense codes */  /*01234 */
#define STAT_SIXBIT     0x00004         /* Disk in 6bit more. */
#define EXPT_FILECHK    0x10010         /* File control check error */
#define EXPT_DSKCHK     0x10020         /* Disk storage error */
#define STAT_NOTRDY     0x10040         /* Disk no ready */
#define STAT_OFFLINE    0x10080         /* Disk offline */
#define DATA_PARITY     0x20100         /* Data parity error */
#define DATA_CHECK      0x20200         /* Compare error */
#define DATA_RESPONSE   0x20400         /* Response check */
#define PROG_INVADDR    0x40800         /* Invalid seek address */
#define PROG_NOREC      0x41000         /* No record found */
#define PROG_FMTCHK     0x42000         /* Format check */
#define PROG_INVCODE    0x44000         /* Invalid code */
#define PROG_INVSEQ     0x48000         /* Invalid sequence */

#define MAXTRACK        6020    /* Max size per track */

uint32              dsk_cmd(UNIT *, uint16, uint16);
t_stat              dsk_srv(UNIT *);
t_stat              dsk_boot(int32, DEVICE *);
void                dsk_ini(UNIT *, t_bool);
t_stat              dsk_reset(DEVICE *);
t_stat              dsk_set_module(UNIT * uptr, int32 val, CONST char *cptr,
                                   void *desc);
t_stat              dsk_get_module(FILE * st, UNIT * uptr, int32 v,
                                   CONST void *desc);
t_stat              dsk_set_type(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              dsk_get_type(FILE * st, UNIT * uptr, int32 v,
                                 CONST void *desc);
t_stat              dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *dsk_description (DEVICE *dptr);

int                 disk_rblock(UNIT * uptr, int track);
int                 disk_wblock(UNIT * uptr);
void                disk_posterr(UNIT * uptr, uint32 error);
void                disk_cmderr(UNIT * uptr, uint32 error);
int                 disk_cmd(UNIT * uptr);
int                 disk_write(UNIT * uptr, uint8 data, int chan,
                               int eor);
int                 disk_read(UNIT * uptr, uint8 * data, int chan);
int                 disk_format(UNIT * uptr, FILE * f, int cyl,
                                UNIT * base);
int                 bcd_to_track(uint32 addr);

/* Data buffer for track */
uint8               dbuffer[NUM_DEVS_DSK * 4][MAXTRACK];

/* Format buffer for cylinder */
uint8               fbuffer[NUM_DEVS_DSK * 4][MAXTRACK / 4];

/* Currently loaded format record */
uint16              fmt_cyl[NUM_DEVS_DSK * 4];

/* Currently read in track in buffer */
uint16              dtrack[NUM_DEVS_DSK * 4];

/* Arm position */
uint16              arm_cyl[NUM_DEVS_DSK * 4];
uint32              sense[NUM_CHAN * 2];
uint32              sense_unit[NUM_CHAN * 2];
uint8               cmd_buffer[NUM_CHAN];       /* Command buffer per channel */
uint8               cmd_mod[NUM_CHAN];          /* Command module per channel */
uint32              cmd_option[NUM_CHAN];       /* Command option per channel */
uint16              cmd_count[NUM_CHAN];        /* Number of chars recieved */

#ifdef I7010
extern uint8        chan_seek_done[NUM_CHAN];   /* Seek finished flag */
extern uint8        chan_io_status[NUM_CHAN];   /* Channel status flags */
#endif

/* Macro to help build the disk size table */
#define DISK_DEF(name, cyl, cylpertrk, acc, charpertrk, overhd, mod, dr) \
        { name, cyl, cylpertrk, ((charpertrk/128) + 1) * 128, \
         acc, (((charpertrk/128) + 1) * 128)/4, \
          acc * cyl * ((((charpertrk/128) + 1) * 128)/4), \
        overhd, mod, dr }

struct disk_t
{
    const char         *name;   /* Type Name */
    int                 cyl;    /* Number of cylinders */
    int                 track;  /* Number of tracks/cylinder */
    unsigned int        bpt;    /* Max bytes per track */
    int                 arms;   /* Number of access arms */
    int                 fbpt;   /* Number of format bytes per track */
    int                 fmtsz;  /* Format size */
    int                 overhd; /* Number of characters overhead on HA/RA */
    int                 mods;   /* Number of modules */
    int                 datarate;       /* us per chars */
}
disk_type[] =
{
        DISK_DEF("1301", 254, 40, 1, 2880, 4, 1, 15),
        DISK_DEF("1301-2", 254, 40, 1, 2880, 4, 2, 15),
        DISK_DEF("1302", 254, 40, 2, 5940, 7, 1, 10),
        DISK_DEF("1302-2", 254, 40, 2, 5940, 7, 2, 10),
        DISK_DEF("2302", 254, 40, 2, 5940, 7, 2, 10),
        DISK_DEF("7238", 1, 404, 1, 3270, 4, 1, 10),
        DISK_DEF("7238-2", 1, 404, 1, 3270, 4, 2, 10), {
        NULL, 0}
};

int                 unit_bit[] = {
  /*0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
   19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 30, 30, 30, 30, 30, 30,
    9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 30, 30, 30, 30, 30, 30
};

#define DSKSTA_BSY      0x02    /* Controller busy. */
#define DSKSTA_EIGHT    0x04    /* Controller in 8 bit mode */

#ifdef I7090
#define CH1 4
#define CH2 6
#endif
#ifdef I7070
#define CH1 5
#define CH2 6
#endif
#ifdef I7080
#define CH1 5
#define CH2 6
#endif
#ifndef CH1
#define CH1 1
#endif
#ifndef CH2
#define CH2 2
#endif

UNIT                dsk_unit[] = {
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH1) | UNIT_DSK, 0), 0, 0x000, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH1) | UNIT_DSK, 0), 0, 0x102, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH1) | UNIT_DSK, 0), 0, 0x204, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH1) | UNIT_DSK, 0), 0, 0x306, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH1) | UNIT_DSK, 0), 0, 0x408, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH2) | UNIT_DSK, 0), 0, 0x500, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH2) | UNIT_DSK, 0), 0, 0x602, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH2) | UNIT_DSK, 0), 0, 0x704, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH2) | UNIT_DSK, 0), 0, 0x806, 0},
    {UDATA(&dsk_srv, UNIT_S_CHAN(CH2) | UNIT_DSK, 0), 0, 0x908, 0},
/* Second set for arm two of each unit */
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
/* Third set for arm one of second module */
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
/* Fourth set for arm two of second module */
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
    {UDATA(&dsk_srv, UNIT_DIS, 0), 0, 0xff, 0},
};

MTAB                dsk_mod[] = {
    {FORMAT_OK, 0, 0, "NOFORMAT", NULL, NULL, NULL, "Format not allowed"},
    {FORMAT_OK, FORMAT_OK, "FORMAT", "FORMAT", NULL, NULL, NULL,
             "Format allowed"},
    {HA2_OK, 0, 0, "NOHA2", NULL, NULL, NULL, "No writing of Home Address"},
    {HA2_OK, HA2_OK, "HA2", "HA2", NULL, NULL, NULL,
            "Allow writing of Home Address"},
#ifdef I7090
    {CTSS_BOOT, 0, 0, "IBSYS", NULL, NULL, NULL, "IBSYS Boot Card"},
    {CTSS_BOOT, CTSS_BOOT, "CTSS", "CTSS", NULL, NULL, NULL, "CTSS Boot Card"},
#endif
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
     &dsk_set_type, &dsk_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "MODULE", "MODULE",
     &dsk_set_module, &dsk_get_module, NULL, "Module number"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN",
     &set_chan, &get_chan, NULL, "Channel number"},
#ifndef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "SELECT", "SELECT",
     &chan9_set_select, &chan9_get_select, NULL, "Unit select"},
#endif
    {0}
};

DEVICE              dsk_dev = {
    "DK", dsk_unit, NULL /* Registers */ , dsk_mod,
    NUM_DEVS_DSK, 8, 15, 1, 8, 8,
    NULL, NULL, &dsk_reset, &dsk_boot, NULL, NULL,
    &dsk_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dsk_help, NULL, NULL, &dsk_description
};

uint32 dsk_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 u = (uptr->u3 >> 8) & 0xf;
    int                chan = UNIT_G_CHAN(dsk_unit[u].flags);
#ifdef I7010
    int                 sel;

    sel = (dsk_unit[u].flags & UNIT_SELECT) ? 1 : 0;
    if (cmd & 0x100)
        sense[(chan * 2) + sel] |= STAT_SIXBIT;
    else
        sense[(chan * 2) + sel] &= ~STAT_SIXBIT;
    cmd_buffer[chan] = cmd & 0xff;
    cmd_count[chan] = 2;
    sim_debug(DEBUG_CHAN, &dsk_dev, "unit %d = cmd=%02x\n\r",
                dev, cmd & 0xff);
#else
    cmd_buffer[chan] = 0;
    cmd_count[chan] = 0;
    sim_debug(DEBUG_CHAN, &dsk_dev, "unit=%d cmd\n\r", dev);
#endif
    cmd_option[chan] = 0;
    cmd_mod[chan] = 0;
    chan_clear(chan, DEV_SEL);

    /* If device is not active start it working */
    if (!sim_is_active(uptr))
        sim_activate(uptr, us_to_ticks(50));
    return SCPE_OK;
}

t_stat dsk_srv(UNIT * uptr)
{
    int                 chan;
    int                 sel;
    int                 schan;
    int                 dev = uptr->u3 & 0xff;
    int                 u = (uptr->u3 >> 8) & 0xf;
    struct disk_t      *dsk = &disk_type[uptr->u4];
    UNIT               *base = &dsk_unit[u];
    uint8               ch = 0;
    int                 eor = 0;

    chan = UNIT_G_CHAN(base->flags);
    sel = (base->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;
    /* Make sure channel is talking to us */
    if (sel != chan_test(chan, CTL_SEL))
        goto seeking;
    /* Channel has disconnected, abort current operation. */
    if (chan_test(chan, DEV_DISCO)) {
        int                 i;

        /* Scan all units and stop them if they are on this channel */
        for (i = 0; i < NUM_DEVS_DSK; i++) {
            int                 xchan = UNIT_G_CHAN(dsk_unit[i].flags);
            int                 j;

            if (xchan != chan)
                continue;
            for (j = 3 * NUM_DEVS_DSK + i; j >= 0; j -= NUM_DEVS_DSK) {
                disk_wblock(&dsk_unit[j]);      /* Flush block */
                if (dsk_unit[j].u5 & DSKSTA_CMD) {
                    dsk_unit[j].u5 &=
                        ~(DSKSTA_CMD | DSKSTA_XFER | DSKSTA_SCAN | DSKSTA_DATA);
                    sim_cancel(&dsk_unit[j]);
                }
            }
        }
        chan_clear(chan, (DEV_DISCO | DEV_WEOR | DEV_SEL));
        sim_debug(DEBUG_CHAN, &dsk_dev, "unit=%d disconnecting\n\r", dev);
        if (uptr->wait > 0)
            sim_activate(uptr, us_to_ticks(100));
        return SCPE_OK;
    }

    /* Call channel proccess to make sure all date is available. */
    chan_proc();
    /* Handle sending sense data */
    if (chan_test(chan, CTL_SNS)) {
        chan9_clear_error(chan, sel);
        switch(cmd_count[chan]) {
        case 0:
                sim_debug(DEBUG_SNS, &dsk_dev, "unit=%d chan sense=%05x\n", dev,
                         sense[schan]);
                /* fall through */
        case 1: case 2: case 3: case 4:
                ch = (sense[schan] >> (4 * (4 - cmd_count[chan]))) & 0xF;
                break;
        case 9:
                sim_debug(DEBUG_SNS, &dsk_dev, "unit=%d unit sense=%08x\n", dev,
                         sense_unit[schan]);
                eor = DEV_REOR;
                /* fall through */
        case 5: case 6: case 7: case 8:
                ch = (sense_unit[schan] >> (4 * (9 - cmd_count[chan]))) & 0xF;
                break;
        }
        if (ch & 010)
            ch ^= 030;
        sim_debug(DEBUG_SNS, &dsk_dev, "unit=%d sense %d %02o\n\r", dev,
                  cmd_count[chan], ch);
        cmd_count[chan]++;
        switch(chan_write_char(chan, &ch, eor)) {
        case DATA_OK:
                if (eor == 0)
                   break;
        case TIME_ERROR:
        case END_RECORD:
                sense[chan] &= STAT_SIXBIT;
                sense_unit[chan] = 0;
                chan_set(chan, CTL_END);
                chan_clear(chan, DEV_SEL);
                sim_debug(DEBUG_CHAN, &dsk_dev, "unit=%d sense eor\n\r", dev);
                break;
        }
        sim_activate(uptr, us_to_ticks(20));
        return SCPE_OK;
    }

    if (chan_test(chan, CTL_CNTL) && disk_cmd(uptr)) {
        sim_activate(uptr, us_to_ticks(50));
        return SCPE_OK;
    }

    /* Handle writing of data */
    if (chan_test(chan, CTL_WRITE) && uptr->u5 & DSKSTA_CMD) {
        /* Channel asking for end of record? */
        if (chan_stat(chan, DEV_WEOR)) {
            sim_debug(DEBUG_CHAN, &dsk_dev, "Disk chan %d -> weor\n\r", chan);
            while (disk_write(uptr, 0x40, chan, 1) == 0) ;
            disk_wblock(uptr);  /* Flush block */
            uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
            uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
            chan_set(chan, CTL_END|DEV_REOR);
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        }
        uptr->u5 |= DSKSTA_WRITE;       /* Flag as write */
        switch(chan_read_char(chan, &ch, 0)) {
        case TIME_ERROR:
            disk_posterr(uptr, DATA_RESPONSE);
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        case END_RECORD:
            /* If last word, fill with zeros until we get eor */
            sim_debug(DEBUG_CHAN, &dsk_dev, "Disk chan %d eor\n\r",chan);
            uptr->u5 |= DSKSTA_DATA;
            while (disk_write(uptr, 0x40, chan, 1) == 0) ;
            disk_wblock(uptr);  /* Flush block */
            uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
            chan_set(chan, CTL_END);
            sim_activate(uptr, us_to_ticks(100));
            break;
        case DATA_OK:
            eor = disk_write(uptr, ch, chan, 0);
            uptr->u5 |= DSKSTA_DATA;
            if (eor == 1) {
                /* Handle end of record */
                sim_debug(DEBUG_CHAN, &dsk_dev,
                        "Disk chan %d end of track\n\r", chan);
                if ((uptr->u5 & DSKSTA_CMSK) == DVSR &&
                        (uptr->u5 & DSKSTA_XFER) == 0)
                        disk_posterr(uptr, PROG_NOREC);
                uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
                chan_set(chan, DEV_REOR|CTL_END);
            }
            sim_activate(uptr, us_to_ticks(dsk->datarate));
            return SCPE_OK;
        }
    }

    /* Handle reading of data */
    if (chan_test(chan, CTL_READ) && uptr->u5 & DSKSTA_CMD) {
        /* Channel asking for end of record? */
        if (chan_stat(chan, DEV_WEOR)) {
            sim_debug(DEBUG_CHAN, &dsk_dev, "Disk chan %d -> weor\n\r", chan);
            uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
            chan_set(chan, CTL_END|DEV_REOR);
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        }
        eor = disk_read(uptr, &ch, chan);
        /* Check if we got error during read */
        if (eor == -1) {
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        }
        switch(chan_write_char(chan, &ch, (eor)?DEV_REOR:0)) {
        case TIME_ERROR:
             /* Flag as timming error */
             disk_posterr(uptr, DATA_RESPONSE);
             break;
        case END_RECORD:
             /* Fill the buffer until end of record */
             sim_debug(DEBUG_CHAN, &dsk_dev, "Disk chan %d eor\n\r", chan);
             if ((uptr->u5 & DSKSTA_CMSK) == DVSR &&
                    (uptr->u5 & DSKSTA_XFER) == 0)
                    disk_posterr(uptr, PROG_NOREC);
             uptr->u5 &= ~(DSKSTA_SCAN | DSKSTA_XFER);
             chan_set(chan, CTL_END);
             uptr->u5 |= DSKSTA_DATA;
             break;
        case DATA_OK:
             uptr->u5 |= DSKSTA_DATA;
             break;
        }
        sim_activate(uptr, us_to_ticks(dsk->datarate));
        return SCPE_OK;
    }

    /* Handle read/write without a command */
    if (chan_test(chan, CTL_WRITE|CTL_READ) &&
           (uptr->u3 & 0xff) == cmd_mod[chan] &&
           (uptr->u5 & (DSKSTA_DATA|DSKSTA_CMD)) == 0)
        disk_posterr(uptr, PROG_INVSEQ);

  seeking:
    if (uptr->wait || uptr->u5 & DSKSTA_CMD)
        sim_activate(uptr, us_to_ticks(100));

    /* Handle seeking */
    if (uptr->wait > 0) {
        uptr->wait--;
        if (uptr->wait == 0) {
            sim_debug(DEBUG_EXP, &dsk_dev, "Seek done dev=%d\n", dev);
            sense_unit[schan] |= 1 << unit_bit[dev];
#ifdef I7010
            chan_seek_done[chan] = 1;
#else
            chan9_set_attn(chan, sel);
#endif
        }
    }

    return SCPE_OK;
}

/* Post a error on a given unit. */
void
disk_posterr(UNIT * uptr, uint32 error)
{
    int                 chan;
    int                 schan;
    int                 sel;
    int                 u = (uptr->u3 >> 8) & 0xf;

    uptr = &dsk_unit[u];
    uptr->u5 &= ~(DSKSTA_CMD | DSKSTA_XFER | DSKSTA_SCAN);
    chan = UNIT_G_CHAN(uptr->flags);
    sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;
    sense[schan] |= error;
    if (error != 0)
        chan9_set_error(chan, SNS_UEND);
    chan_set(chan, DEV_REOR|CTL_END);
    sim_debug(DEBUG_DETAIL, &dsk_dev, "post err dev=%d err=%08x\n", u,error);
#ifdef I7010
    if ((error & STAT_OFFLINE) == STAT_OFFLINE)
        chan_io_status[chan] |= 01;

    if ((error & STAT_NOTRDY) == STAT_NOTRDY)
        chan_io_status[chan] |= 02;

    if (error & (EXPT_FILECHK|EXPT_DSKCHK|DATA_PARITY|DATA_CHECK|
                 DATA_RESPONSE|PROG_INVADDR) & 0xFFFF)
        chan_io_status[chan] |= 04;

    if (error & (PROG_NOREC|PROG_FMTCHK|PROG_INVCODE|PROG_INVSEQ) & 0xffff)
        chan_io_status[chan] |= 010;
#endif
}

/* Post error for command that could not be completed */
void
disk_cmderr(UNIT * uptr, uint32 error)
{
    int                 chan;
    int                 schan;
    int                 sel;
    int                 u = (uptr->u3 >> 8) & 0xf;

    uptr = &dsk_unit[u];
    uptr->u5 &= ~(DSKSTA_CMSK | DSKSTA_CMD | DSKSTA_CHECK | DSKSTA_WRITE);
    chan = UNIT_G_CHAN(uptr->flags);
    sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;
    sense[schan] |= error;
    if (error != 0)
        chan9_set_error(chan, SNS_UEND);
    chan_set(chan, DEV_REOR|CTL_END);
    sim_debug(DEBUG_DETAIL, &dsk_dev, "cmd err dev=%d err=%08x\n", u,error);
#ifdef I7010
    if ((error & STAT_OFFLINE) == STAT_OFFLINE)
        chan_io_status[chan] |= 01;

    if ((error & STAT_NOTRDY) == STAT_NOTRDY)
        chan_io_status[chan] |= 02;

    if (error & (EXPT_FILECHK|EXPT_DSKCHK|DATA_PARITY|DATA_CHECK|
                 DATA_RESPONSE|PROG_INVADDR) & 0xFFFF)
        chan_io_status[chan] |= 04;

    if (error & (PROG_NOREC|PROG_FMTCHK|PROG_INVCODE|PROG_INVSEQ) & 0xffff)
        chan_io_status[chan] |= 010;
#endif
}

/* Process command */
int
disk_cmd(UNIT * uptr)
{
    uint8               ch;
    UNIT               *base;
    UNIT               *up;
    int                 chan;
    int                 schan;
    int                 sel;
    int                 i;
    int                 u;
    int                 t;
    int                 trk;
    int                 cyl;

    /* Get information on unit */
    u = uptr - dsk_unit;
    base = &dsk_unit[(uptr->u3 >> 8) & 0xf];
    chan = UNIT_G_CHAN(base->flags);
    sel = (base->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;

    /* Check if we have a command yet */
    switch(chan_read_char(chan, &ch, 0)) {
    case TIME_ERROR:
        disk_cmderr(uptr, DATA_RESPONSE);
    case END_RECORD:
        return 0;
    case DATA_OK:
        sim_debug(DEBUG_DATA, &dsk_dev, "unit=%d data=%02o\n", u, ch);
        break;
    }

    /* Place in command buffer */
    switch(cmd_count[chan]) {
    case 0:
    case 1:
        if (ch != 012)
            cmd_buffer[chan] |= (ch & 0xf) << (4 * (1 - cmd_count[chan]));
        break;
    case 2:
    case 3:
        if (ch != 012)
            cmd_mod[chan] |= (ch & 0xf) << (4 * (3 - cmd_count[chan]));
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        if (ch != 012)
           cmd_option[chan] |= (ch & 0xf) << (16 + (4 * (7 - cmd_count[chan])));
        break;
    case 8:
    case 9:
        cmd_option[chan] |= (ch & 0x3f) << (6 * (9 - cmd_count[chan]));
        break;
    }

    /* Need at least two chars to determine command */
    if (++cmd_count[chan] == 1)
        return 1;

    /* Check if we have enough digits */
    switch(cmd_buffer[chan]) {
    case DNOP:          /* Nop */
    case DREL:          /* Release */
    case DEBM:          /* Eight Bit mode */
    case DSBM:          /* Six bit mode */
        break;          /* Yes */

    case DSAI:          /* Set Access Inoperative */
        if (cmd_count[chan] <= 3)
           return 1;    /* Need more */
        break;
    case DSEK:          /* Seek */
    case DWRF:          /* Prepare to Format */
    case DVHA:          /* Prepare to Verify home addr */
        if (cmd_count[chan] <= 7)
           return 1;    /* Need more */
        break;
    case DVTA:          /* Prepare to Verify track addr */
    case DVTN:          /* Prepare to Verify track no addr */
    case DVCY:          /* Prepare to Verify Cyl */
    case DVSR:          /* Prepare to Verify single record */
    case DWRC:          /* Prepare to Write Check */
        if (cmd_count[chan] < 10)
           return 1;
        break;
    }

    /* Flag last item recieved */
    chan_set(chan, DEV_REOR);
    chan9_clear_error(chan, sel);

    sim_debug(DEBUG_CMD, &dsk_dev, "unit=%d cmd=%02x %02x %04x %04o ",u,
         cmd_buffer[chan], cmd_mod[chan], cmd_option[chan] >> 16,
                 cmd_option[chan] & 07777);
    /* 40  36  32   2824  2016     6     0 */
    /* 01   2  3     4 5   6 7    8     9  */
    /* op | ac mod | t t | t t | HA2 | HA2 */
    up = NULL;

    sense[schan] &= STAT_SIXBIT;
    switch (cmd_buffer[chan]) {
    case DNOP:          /* Nop */
    case DREL:          /* Release */
         /* Should not happen, but if read or write, cpy will be
          * expected so turn on command flag to catch disconnect */
         sim_debug(DEBUG_CMD, &dsk_dev, "nop\n");
         goto clear_drive;

    case DEBM:          /* Eight Bit mode */
         /* Should not happen, but if read or write, cpy will be
          * expected so turn on command flag to catch disconnect */
         sim_debug(DEBUG_CMD, &dsk_dev, "eight bit mode\n");
         sense[schan] &= ~STAT_SIXBIT;
         goto clear_drive;

     case DSBM:         /* Six bit mode */
         /* Should not happen, but if read or write, cpy will be
          * expected so turn on command flag to catch disconnect */
         sim_debug(DEBUG_CMD, &dsk_dev, "six bit mode\n");
         sense[schan] |= STAT_SIXBIT;
clear_drive:
        /* Scan all units and clear command */
        for (i = 0; i < NUM_DEVS_DSK; i++) {
            int                 xchan = UNIT_G_CHAN(dsk_unit[i].flags);
            int                 j;

            if (xchan != chan)
                continue;
            for (j = 3 * NUM_DEVS_DSK + i; j >= 0; j -= NUM_DEVS_DSK)
                dsk_unit[j].u5 &= ~ DSKSTA_CMSK;
        }
        sim_activate(uptr, us_to_ticks(100));
        return 1;

     case DWRC:         /* Prepare to Write Check */
         sim_debug(DEBUG_CMD, &dsk_dev, "write check\n");
     case DVSR:         /* Prepare to Verify single record */
     case DWRF:         /* Prepare to Format */
     case DVTN:         /* Prepare to Verify track no addr */
     case DVCY:         /* Prepare to Verify Cyl */
     case DVTA:         /* Prepare to Verify track addr */
     case DVHA:         /* Prepare to Verify home addr */
     case DSAI:         /* Set Access Inoperative */
     case DSEK:         /* Seek */
            break;      /* Go find unit to operate on */
     default:
            sim_debug(DEBUG_CMD, &dsk_dev, " Unknown Command\n\r");
            /* Flag as invalid command */
            disk_cmderr(uptr, PROG_INVCODE);
            return 1;
     }

     /* Find actual owner of this command */
     for (i = 0; i < NUM_DEVS_DSK; i++) {

        if ((dsk_unit[i].flags & (UNIT_SELECT | UNIT_CHAN)) !=
            (base->flags & (UNIT_SELECT | UNIT_CHAN)))
            continue;

        /* Adjust for unit */
        if ((0xff & dsk_unit[i].u3) == cmd_mod[chan])
            up = &dsk_unit[i];
        else if ((0xff & dsk_unit[i + NUM_DEVS_DSK].u3) == cmd_mod[chan])
            up = &dsk_unit[i + NUM_DEVS_DSK];
        else if ((0xff & dsk_unit[i + (NUM_DEVS_DSK * 2)].u3) == cmd_mod[chan])
            up = &dsk_unit[i + (NUM_DEVS_DSK * 2)];
        else if ((0xff & dsk_unit[i + (NUM_DEVS_DSK * 3)].u3) == cmd_mod[chan])
            up = &dsk_unit[i + (NUM_DEVS_DSK * 3)];
        else
            continue;

        /* Check if unit is busy */
        if (cmd_buffer[chan] != DWRC && (up->u5 & DSKSTA_CMD || up->wait > 0)) {
            sim_debug(DEBUG_CMD, &dsk_dev, "unit=%d busy\n", u);
            if (up->wait > 5)
                up->wait = 5;
            disk_cmderr(uptr, STAT_NOTRDY);
            return 1;
        } else {
            if (cmd_buffer[chan] == DWRC) {
                if (up->u5 & DSKSTA_CMSK) {
                    up->u5 |= DSKSTA_CHECK;
                    up->u5 &= ~DSKSTA_DATA;
                    cmd_buffer[chan] = up->u5 & DSKSTA_CMSK;
                } else {
                    disk_cmderr(up, PROG_INVSEQ);
                    return 0;
                }
            } else {
                up->u5 &= ~(DSKSTA_CMSK|DSKSTA_CHECK|DSKSTA_WRITE|DSKSTA_DATA);
                up->u5 |= cmd_buffer[chan];
            }
            break;
        }
    }

    if (up == NULL)  {
        sim_debug(DEBUG_CMD, &dsk_dev, "invalid unit\n");
        /* Flag as invalid command */
        disk_cmderr(uptr, STAT_OFFLINE);
        return 1;
    }

    /* Adjust to new unit */
    u = up - dsk_unit;
    base = &dsk_unit[(up->u3 >> 8) & 0xf];
    chan = UNIT_G_CHAN(base->flags);
    sel = (base->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;
    /* Clear unit attention */
    sense_unit[schan] &= ~(1 << unit_bit[up->u3 & 0xff]);

    /* Check if there is a unit here */
    if ((base->flags & UNIT_ATT) == 0) {
         disk_cmderr(uptr, STAT_OFFLINE);
         return 1;
    }

    /* Find out track and cylinder of this operation */
    trk = bcd_to_track(cmd_option[chan]);

    if (chan_test(chan, CTL_PWRITE))
        sim_debug(DEBUG_CMD, &dsk_dev, "write ");
    if (chan_test(chan, CTL_PREAD))
        sim_debug(DEBUG_CMD, &dsk_dev, "read ");
    /* Do command */
    switch (cmd_buffer[chan]) {
    case DSAI:          /* Set Access Inoperative */
        detach_unit(base);
        disk_cmderr(up, 0);
        return 1;

    case DSEK:          /* Seek */
        cyl = trk / disk_type[base->u4].track;
        /* Check how far we have to move */
        t = cyl - arm_cyl[u];
        if (t < 0)
            t = -t;
        sim_debug(DEBUG_CMD, &dsk_dev,
                  "DSEK unit=%d %d cylinders to %d trk=%d\n", u, t, cyl, trk);
        up->u5 &= ~(DSKSTA_CMSK | DSKSTA_CMD | DSKSTA_CHECK | DSKSTA_WRITE);
        arm_cyl[u] = cyl;       /* And cylinder */
        if (cyl > disk_type[base->u4].cyl) {
            disk_cmderr(up, PROG_INVADDR);
            return 1;
        }
        /* Read in track buffer */
        disk_rblock(up, trk);

        /* Set up for seek wait */
        up->wait = 0;
        /* From documentation, it looks like seeks were a fixed time
         * based on movement between cylinder groups */
        if (t == 0)     /* At cylinder, give attention */
            up->wait = 2;       /* Electronic select time */
        else if (t > 50)
            up->wait = (1800);
        else if (t > 10)
            up->wait = (1200);
        else
            up->wait = (300);
        break;

    case DWRF:          /* Prepare to Format */
        /* Verify ok to format */
        if ((base->flags & FORMAT_OK) == 0) {
            disk_cmderr(uptr, PROG_FMTCHK);
            return 1;
        }

        cyl = trk / disk_type[base->u4].track;

        /* Make sure positioned to correct track */
        if (arm_cyl[u] != cyl) {
            disk_cmderr(uptr, PROG_INVSEQ);
            return 1;
        }

        /* Make sure head on valid cylinder */
        if (cyl > disk_type[base->u4].cyl) {
            disk_cmderr(up, PROG_INVADDR);
            return 1;
        }

        fmt_cyl[u] = cyl;
        sim_debug(DEBUG_CMD, &dsk_dev, "FMT unit=%d\n", u);
        up->u5 |= DSKSTA_SCAN | DSKSTA_CMD | DSKSTA_WRITE; /* Flag as write */
        up->u6 = 0;
        chan_set(chan, DEV_SEL);
        break;

    case DVHA:          /* Prepare to Verify home addr */
        /* Verify HA2 ok to write */
        if ((base->flags & HA2_OK) == 0) {
            disk_cmderr(up, PROG_FMTCHK);
            return 1;
        }
        /* fall through */
    case DVTA:          /* Prepare to Verify track addr */
    case DVTN:          /* Prepare to Verify track no addr */
    case DVCY:          /* Prepare to Verify Cyl */
        switch (cmd_buffer[chan]) {
        case DVTA:
            sim_debug(DEBUG_CMD, &dsk_dev, "DVTA unit=%d ", u);
            break;
        case DVTN:
            sim_debug(DEBUG_CMD, &dsk_dev, "DVTN unit=%d ", u);
            break;
        case DVCY:
            sim_debug(DEBUG_CMD, &dsk_dev, "DVCY unit=%d ", u);
            break;
        case DVHA:
            sim_debug(DEBUG_CMD, &dsk_dev, "DVHA unit=%d ", u);
            break;
        }

        sim_debug(DEBUG_CMD, &dsk_dev, "trk=%d\n\r", trk);

        /* Make sure head on valid cylinder */
        if ((trk / disk_type[base->u4].track) > disk_type[base->u4].cyl) {
            disk_cmderr(up, PROG_INVADDR);
            return 1;
        }

        /* Make sure we have correct format for this cylinder in buffer */
        disk_rblock(up, trk);
        /* Start actual operations */
        up->u5 |= DSKSTA_SCAN | DSKSTA_CMD;
        up->u6 = 0;
        chan_set(chan, DEV_SEL);
        break;

    case DVSR:          /* Prepare to Verify single record */
        /* Start actual operations */
        up->u5 |= DSKSTA_SCAN | DSKSTA_CMD;
        up->u6 = 0;
        chan_set(chan, DEV_SEL);
        break;
    }
    sim_activate(up, us_to_ticks(50));
    return 0;
}

/* Print a format pattern */
void
print_format(UNIT * uptr)
{
    struct disk_t      *dsk = &disk_type[uptr->u4];
    int                 i, j;
    int                 u = uptr - dsk_unit;
    int                 flag;
    int                 lflag = -1;

    sim_debug(DEBUG_DETAIL, &dsk_dev, "unit=%d (%s) format: ", u, dsk->name);

    i = 0;
    j = 0;
    do {
        flag = fbuffer[u][i / 4];
        flag >>= (i & 03) * 2;
        flag &= 03;
        if (lflag != flag) {
           if (j != 0) {
                switch(lflag) {
                case FMT_DATA:
                     sim_debug(DEBUG_DETAIL, &dsk_dev, "DA(%d) ", j);
                     break;
                case FMT_HDR:
                     sim_debug(DEBUG_DETAIL, &dsk_dev, "RA(%d) ", j);
                     break;
                case FMT_HA2:
                     sim_debug(DEBUG_DETAIL, &dsk_dev, "HA2(%d) ", j);
                     break;
                }
           }
           j = 1;
           lflag = flag;
        } else {
           j++;
        }
        i++;
    } while (flag != FMT_END);

    sim_debug(DEBUG_DETAIL, &dsk_dev, "total=%d\n", i);
}


int
disk_rblock(UNIT * uptr, int trk)
{
    int                 u = uptr - dsk_unit;
    struct disk_t      *dsk = &disk_type[uptr->u4];
    UNIT               *base = &dsk_unit[(uptr->u3 >> 8) & 0xf];
    FILE               *f = base->fileref;
    int                 offset = 0;
    int                 fbase = 0;

    offset = dsk->cyl * dsk->track * dsk->bpt;
    fbase = dsk->fmtsz;
    offset *= u / (NUM_DEVS_DSK);
    fbase *= u / (NUM_DEVS_DSK);
    offset += dsk->fmtsz * dsk->mods * dsk->arms;

    if (uptr->u5 & DSKSTA_DIRTY) {
        disk_wblock(uptr);
    }

    if (arm_cyl[u] != fmt_cyl[u]) {
        (void)sim_fseek(f, fbase + arm_cyl[u] * dsk->fbpt, SEEK_SET);
        (void)sim_fread(fbuffer[u], 1, dsk->fbpt, f);
        fmt_cyl[u] = arm_cyl[u];
        print_format(uptr);
    }
    /* Read in actualy track data */
    if (dtrack[u] != trk) {
        sim_debug(DEBUG_DETAIL, &dsk_dev, "unit=%d Read track %d\n", u,
                  trk);
        (void)sim_fseek(f, offset + trk * dsk->bpt, SEEK_SET);
        if (sim_fread(dbuffer[u], 1, dsk->bpt, f) != dsk->bpt)
            memset(dbuffer[u], 0, dsk->bpt);
        dtrack[u] = trk;
    }
    return 1;
}

int
disk_wblock(UNIT * uptr)
{
    int                 u = uptr - dsk_unit;
    struct disk_t      *dsk = &disk_type[uptr->u4];
    UNIT               *base = &dsk_unit[(uptr->u3 >> 8) & 0xf];
    FILE               *f = base->fileref;
    int                 offset = 0;

    offset = dsk->cyl * dsk->track * dsk->bpt;
    offset *= u / (NUM_DEVS_DSK);
    offset += dsk->fmtsz * dsk->mods * dsk->arms;

    /* Check if new format data */
    if ((uptr->u5 & DSKSTA_CMSK) == DWRF) {
        if (uptr->u5 & (DSKSTA_CHECK|DSKSTA_DIRTY)) {
            switch (disk_format(uptr, f, arm_cyl[u], base)) {
            case 2:
                if (uptr->u5 & DSKSTA_CHECK) {
                    disk_posterr(uptr, PROG_FMTCHK|EXPT_DSKCHK);
                    break;
                }
            case 1:
                disk_posterr(uptr, PROG_FMTCHK);
                break;
            case 0:
                break;
            }
            uptr->u5 &= ~(DSKSTA_CHECK);
        }
        return 1;
    }

    if (uptr->u5 & DSKSTA_CHECK) {
        uptr->u5 &= ~(DSKSTA_CHECK);
        if ((uptr->u5 & DSKSTA_DIRTY) == 0)
            return 1;
    } else if ((uptr->u5 & DSKSTA_DIRTY) == 0)
        return 1;

    sim_debug(DEBUG_DETAIL, &dsk_dev, "unit=%d Write track %d\n",
              u, dtrack[u]);
    /* Write in actualy track data */
    (void)sim_fseek(f, offset + dtrack[u] * dsk->bpt, SEEK_SET);
    (void)sim_fwrite(dbuffer[u], 1, dsk->bpt, f);
    uptr->u5 &= ~DSKSTA_DIRTY;
    return 1;
}

/* Convert a format pattern into a format track */
int
disk_format(UNIT * uptr, FILE * f, int cyl, UNIT * base)
{
    uint8               tbuffer[MAXTRACK];
    struct disk_t      *dsk = &disk_type[uptr->u4];
    int                 i, j;
    int                 out = 0;
    int                 u = uptr - dsk_unit;
    uint8               ch;
    int                 offset;

    f = base->fileref;

    offset = dsk->fmtsz;
    offset *= u / (NUM_DEVS_DSK);

    uptr->u5 &= ~(DSKSTA_DIRTY);
    sim_debug(DEBUG_DETAIL, &dsk_dev, "unit=%d (%s) format: ", u, dsk->name);
    /* Scan over new format */
    /* Convert format specification in place in dbuffer, then
     * pack it into fbuffer and write to file */

    /* Skip initial gap */
    for (i = 0; i < MAXTRACK && dbuffer[u][i] == 04; i++) ;
    if (i == MAXTRACK)
        return 2;               /* Failed if we hit end */
    /* HA1 Gap */
    for (j = i; i < MAXTRACK && dbuffer[u][i] == 03; i++) ;
    if ((i - j) > 12)
        return 1;               /* HA1 too big */

    if (dbuffer[u][i++] != 04)
        return 2;               /* Not gap */
    for (j = i; i < MAXTRACK && dbuffer[u][i] == 03; i++) ;
    if (i == MAXTRACK)
        return 2;               /* Failed if we hit end */

    if (dbuffer[u][i++] != 04)
        return 2;               /* Not gap */

    /* Size up HA2 gap */
    for (j = i;
         i < MAXTRACK && (dbuffer[u][i] == 03 || dbuffer[u][i] == 01);
         i++) ;
    j = i - j;
    if (j < 6)
        return 2;

    j -= dsk->overhd;           /* Remove overhead */
    sim_debug(DEBUG_DETAIL, &dsk_dev, "HA2(%d) ", j);
    for (; j > 0; j--)
        tbuffer[out++] = FMT_HA2;
    /* Now grab records */
    while (i < MAXTRACK) {
        ch = dbuffer[u][i++];
        if (ch == 0x40)
            break;              /* End of record */
        if (ch != 04 && ch != 02)
            return 2;           /* Not a gap */

        for (j = i; i < MAXTRACK && dbuffer[u][i] == ch; i++) ;
        ch = dbuffer[u][i];     /* Should be RA */
        /* Gap not long enough or eor */
        if (ch == 0x40 || (i - j) < 11)
            break;

        /* Size up RA gap */
        if (ch != 01 && ch != 03)
            return 1;           /* Not header */
        for (j = i; i < MAXTRACK && dbuffer[u][i] == ch; i++) ;
        j = i - j;
        if (j < 10)
            return 2;
        j -= dsk->overhd;       /* Remove overhead */
        sim_debug(DEBUG_DETAIL, &dsk_dev, "RA(%d) ", j);
        for (; j > 0; j--)
            tbuffer[out++] = FMT_HDR;
        ch = dbuffer[u][i++];
        if (ch == 0x40)
            break;              /* End of record */
        if (ch != 04 && ch != 02)
            return 1;           /* End of RA Field */
        ch = dbuffer[u][i];
        if (ch != 01 && ch != 03)
            return 1;           /* Not gap */
        for (j = i; i < MAXTRACK && dbuffer[u][i] == ch; i++) ;
        if ((i - j) < 10)
            return 2;           /* Gap not large enough */
        ch = dbuffer[u][i++];
        if (ch != 04 && ch != 02)
            return 2;           /* End of RA Gap */
        ch = dbuffer[u][i];     /* Should be DA */
        if (ch == 0x40)
            break;              /* End of record */
        if (ch != 01 && ch != 03)
            return 1;           /* Not Record data */
        for (j = i; i < MAXTRACK && dbuffer[u][i] == ch; i++) ;
        j = i - j;
        if (j < 10)
            return 2;
        j -= dsk->overhd;       /* Remove overhead */
        sim_debug(DEBUG_DETAIL, &dsk_dev, "DA(%d) ", j);
        for (; j > 0; j--)
            tbuffer[out++] = FMT_DATA;
    }
    sim_debug(DEBUG_DETAIL, &dsk_dev, "total=%d\n", out);
    /* If checking, don't update */
    if (uptr->u5 & DSKSTA_CHECK)
        return 0;

    /* Put four END chars to end of buffer */
    for (j = 4; j > 0; j--)
        tbuffer[out++] = FMT_END;

    /* Now grab every four characters and place them in next format location */
    for (j = i = 0; j < out; i++) {
        uint8               temp;

        temp = (tbuffer[j++] & 03);
        temp |= (tbuffer[j++] & 03) << 2;
        temp |= (tbuffer[j++] & 03) << 4;
        temp |= (tbuffer[j++] & 03) << 6;
        fbuffer[u][i] = temp;
        if (i > dsk->fbpt)
           break;
    }
    fbuffer[u][dsk->fbpt-1] = (FMT_END<<6)|(FMT_END<<4)|(FMT_END<<2)|FMT_END;

    /* Now write the buffer to the file */
    (void)sim_fseek(f, offset + cyl * dsk->fbpt, SEEK_SET);
    (void)sim_fwrite(fbuffer[u], 1, dsk->fbpt, f);

    /* Make sure we did not pass size of track */
    if (out > (int)dsk->bpt)
        return 1;               /* Too big for track */
    return 0;
}

/* Handle writing of one character to disk */
int
disk_write(UNIT * uptr, uint8 data, int chan, int eor)
{
    int                 u = uptr - dsk_unit;
    UNIT               *base = (u > NUM_DEVS_DSK) ? &uptr[-NUM_DEVS_DSK] : uptr;
    int                 skip = 1;
    int                 flag = -1;
    uint8               cmd;
    int                 schan;

    schan = (chan * 2) + ((base->flags & UNIT_SELECT) ? 1 : 0);
    cmd = (uptr->u5) & DSKSTA_CMSK;
    if (uptr->u6 == 0 && cmd != DWRF) {
        int     cyl = dtrack[u] / disk_type[base->u4].track;

        if (arm_cyl[u] != cyl) {
            sim_debug(DEBUG_CMD, &dsk_dev, "cyl not equal %d %d %d\n\r",
                        u, arm_cyl[u], cyl);
            disk_posterr(uptr, PROG_INVADDR);
            return -1;
        }

        /* Verify that the home address matches */
        if (cmd != DVHA && cmd != DVSR) {
            uint16      t = cmd_option[chan] & 01717;
            uint16      ha;
            ha = (077 & dbuffer[u][0]) << 6;
            ha |= 077 & dbuffer[u][1];
            /* Mask out bits we ignore */
            ha &= 01717;
            /* Convert BCD 0 to Binary 0 */
            if ((ha & 01700) == 01200)
                ha &= 077;
            if ((ha & 017) == 012)
                ha &= 07700;
            if ((t & 01700) == 01200)
                t &= 077;
            if ((t & 017) == 012)
                t &= 07700;

            sim_debug(DEBUG_CMD, &dsk_dev, "HA %04o(c) %04o(d)\n", t, ha);
            if (ha != t) {
                disk_posterr(uptr, PROG_NOREC);
                return -1;
            }
        } else {
            sim_debug(DEBUG_CMD, &dsk_dev, "HA  ignored\n");
        }
    }

    while (skip) {
        flag = fbuffer[u][uptr->u6 / 4];
        flag >>= (uptr->u6 & 03) * 2;
        flag &= 03;
        switch (uptr->u5 & DSKSTA_CMSK) {
        case DWRF:              /* Format */
            if ((uint32)uptr->u6 > disk_type[uptr->u4].bpt) {
                return 1;
            }
            if (uptr->u5 & DSKSTA_CHECK) {
                if ((dbuffer[u][uptr->u6++] & 077) != (data & 077)) {
                    disk_posterr(uptr, DATA_CHECK);
                    return -1;
                }
            } else {
                dbuffer[u][uptr->u6++] = data;
                uptr->u5 |= DSKSTA_DIRTY;
            }
            return 0;
        case DVTN:              /* Verify track no addr */
            if (flag == FMT_END) {
                return 1;
            }
            if (flag == FMT_DATA)
                skip = 0;
            else
                uptr->u6++;
            break;
        case DVTA:              /* Verify track addr */
            if (flag == FMT_END) {
                return 1;
            }
            if (flag != FMT_HA2)
                skip = 0;
            else
                uptr->u6++;
            break;
        case DVHA:              /* Verify home addr */
            if (flag == FMT_END) {
                return 1;
            }
            skip = 0;
            break;
        case DVCY:              /*  Verify Cyl */
            if (flag == FMT_END) {
                /* Move to next track */
                int                 trk = dtrack[u];
                int                 cyl = trk / disk_type[uptr->u4].track;

                uptr->u6 = 0;
                if (eor)
                    return 1;

                if ((++trk) / disk_type[uptr->u4].track != cyl)
                    return 1;
                disk_rblock(uptr, trk);
            } else if (flag != FMT_DATA)
                uptr->u6++;
            else
                skip = 0;
            break;
        case DVSR:              /* Verify single record */
            if (flag == FMT_DATA && uptr->u5 & DSKSTA_XFER) {
                skip = 0;
            } else if (uptr->u5 & DSKSTA_XFER) {
                /* Not in a Data record, and we were transfering, all done */
                uptr->u5 &= ~DSKSTA_XFER;
                uptr->u6 = 0;
                return 1;
            } else if (flag == FMT_END) {
                /* If we hit the end, then no record found */
                disk_posterr(uptr, PROG_NOREC);
                return -1;
            } else if (flag == FMT_HDR) {
                uint8               ch;
                uint32              match = 0;
                int                 i;

                for (i = 0; i < 4 && flag == FMT_HDR; i++) {
                    ch = dbuffer[u][uptr->u6++];
                    match <<= 4;
                    if (ch != 012)
                        match |= ch & 0xf;
                    flag = fbuffer[u][uptr->u6 / 4];
                    flag >>= (uptr->u6 & 03) * 2;
                    flag &= 03;
                }
                /* Short header, skip it, but should not have been made */
                if (flag != FMT_HDR)
                    break;
                match <<= 16;
                /* Grab last two Characters */
                while (flag == FMT_HDR) {
                    ch = dbuffer[u][uptr->u6++];
                    match = (match & 0xFFFF0000) | ((match & 0x3f) << 6) |
                                 (ch & 077);
                    flag = fbuffer[u][uptr->u6 / 4];
                    flag >>= (uptr->u6 & 03) * 2;
                    flag &= 03;
                }
                if (flag != FMT_DATA)
                    break;
                /* Compare values. */
                if (match != cmd_option[chan])
                    break;
                /* Got a match, exit loop */
                uptr->u5 &= ~DSKSTA_SCAN;
                uptr->u5 |= DSKSTA_XFER;
                skip = 0;
            } else {
                uptr->u6++;
            }
            break;
        }
    }
    data &= (sense[schan] & STAT_SIXBIT)?077:0277;
    if (uptr->u5 & DSKSTA_CHECK) {
        if (dbuffer[u][uptr->u6++] != data) {
            sim_printf("Mismatch %d %03o != %03o\n\r",
                   uptr->u6-1, dbuffer[u][uptr->u6-1], data);
            disk_posterr(uptr, DATA_CHECK);
        }
    } else {
        dbuffer[u][uptr->u6++] = data;
        uptr->u5 |= DSKSTA_DIRTY;
    }
    return 0;
}

/* Handle reading of one character to disk */
int
disk_read(UNIT * uptr, uint8 * data, int chan)
{
    int                 u = uptr - dsk_unit;
    UNIT               *base = (u > NUM_DEVS_DSK) ? &uptr[-NUM_DEVS_DSK] : uptr;
    int                 skip = 1;
    int                 flag;
    uint8               cmd;
    int                 schan;

    schan = (chan * 2) + ((base->flags & UNIT_SELECT) ? 1 : 0);
    cmd = (uptr->u5) & DSKSTA_CMSK;
    if (uptr->u6 == 0) {
        int     cyl = dtrack[u] / disk_type[base->u4].track;
        if (arm_cyl[u] != cyl) {
            disk_posterr(uptr, PROG_INVADDR);
            return -1;
        }

        /* Verify that the home address matches */
        if (cmd != DVHA && cmd != DVSR) {
            uint16      t = cmd_option[chan] & 01717;
            uint16              ha;
            ha = (077 & dbuffer[u][0]) << 6;
            ha |= 077 & dbuffer[u][1];
            /* Mask out bits we ignore */
            ha &= 01717;
            /* Convert BCD 0 to Binary 0 */
            if ((ha & 07700) == 01200)
                ha &= 077;
            if ((ha & 077) == 012)
                ha &= 07700;
            if ((t & 07700) == 01200)
                t &= 077;
            if ((t & 077) == 012)
                t &= 07700;

            sim_debug(DEBUG_CMD, &dsk_dev, "HA %04o(c) %04o(d)\n", t, ha);
            if (ha != t) {
                disk_posterr(uptr, PROG_NOREC);
                return -1;
            }
        } else {
            sim_debug(DEBUG_CMD, &dsk_dev, "HA ignored\n");
        }
    }

    while (skip) {
        flag = fbuffer[u][uptr->u6 / 4];
        flag >>= (uptr->u6 & 03) * 2;
        flag &= 03;
        switch (cmd) {
        case DWRF:              /* Format */
            disk_posterr(uptr, PROG_FMTCHK);
            return 1;
        case DVTN:              /* Verify track no addr */
            if (flag == FMT_END) {
                uptr->u6 = 0;
                return 1;
            }
            if (flag == FMT_DATA)
                skip = 0;
            else
                uptr->u6++;
            break;
        case DVTA:              /* Verify track addr */
            if (flag == FMT_END) {
                uptr->u6 = 0;
                return 1;
            }
            if (flag != FMT_HA2)
                skip = 0;
            else
                uptr->u6++;
            break;
        case DVHA:              /* Prepare to Verify home addr */
            if (flag == FMT_END) {
                uptr->u6 = 0;
                return 1;
            }
            skip = 0;
            break;
        case DVCY:              /* Verify Cyl */
            if (flag == FMT_END) {
                /* Move to next track */
                int                 trk = dtrack[u];
                UNIT               *base =
                    (u > NUM_DEVS_DSK) ? &uptr[-NUM_DEVS_DSK] : uptr;
                int                 cyl = trk / disk_type[base->u4].track;

                uptr->u6 = 0;
                if ((++trk) / disk_type[base->u4].track != cyl)
                    return 1;
                disk_rblock(uptr, trk);
            } else if (flag != FMT_DATA)
                uptr->u6++;
            else
                skip = 0;
            break;
        case DVSR:              /* Verify single record */
            if (flag == FMT_DATA && uptr->u5 & DSKSTA_XFER) {
                skip = 0;
            } else if (uptr->u5 & DSKSTA_XFER) {
                /* Not in a Data record, and we were transfering, all done */
                uptr->u5 &= ~DSKSTA_XFER;
                uptr->u6 = 0;
                return 1;
            } else if (flag == FMT_END) {
                /* If we hit the end, then no record found */
                disk_posterr(uptr, PROG_NOREC);
                return -1;
            } else if (flag == FMT_HDR) {
                uint8               ch;
                uint32              match = 0;
                int                 i;

                for (i = 0; i < 4 && flag == FMT_HDR; i++) {
                    ch = dbuffer[u][uptr->u6++];
                    match <<= 4;
                    if (ch != 012)
                        match |= ch & 0xf;
                    flag = fbuffer[u][uptr->u6 / 4];
                    flag >>= (uptr->u6 & 03) * 2;
                    flag &= 03;
                }
                /* Short header, skip it, but should not have been made */
                if (flag != FMT_HDR)
                    break;
                match <<= 16;
                /* Grab last two Characters */
                while (flag == FMT_HDR) {
                    ch = dbuffer[u][uptr->u6++];
                    match = (match & 0xFFFF0000) | ((match & 0x3f) << 6) |
                                 (ch & 077);
                    flag = fbuffer[u][uptr->u6 / 4];
                    flag >>= (uptr->u6 & 03) * 2;
                    flag &= 03;
                }
                if (flag != FMT_DATA)
                    break;
                /* Compare values. */
                if (match != cmd_option[chan])
                    break;
                /* Got a match, exit loop */
                uptr->u5 &= ~DSKSTA_SCAN;
                uptr->u5 |= DSKSTA_XFER;
                skip = 0;
            } else {
                uptr->u6++;
            }
            break;
        }
    }
    *data = dbuffer[u][uptr->u6++] & ((sense[schan] & STAT_SIXBIT)?077:0277);
    /* Check if character is last in record */
    flag = fbuffer[u][uptr->u6 / 4];
    flag >>= (uptr->u6 & 03) * 2;
    flag &= 03;
    switch (cmd) {
    case DVTN:          /* Verify track no addr */
    case DVTA:          /* Verify track addr */
    case DVHA:          /* Prepare to Verify home addr */
         if (flag == FMT_END) {
             sim_debug(DEBUG_DATA, &dsk_dev, "eor\n");
             return 1;
         }
         break;
    case DVCY:          /* Verify Cyl */
         if (flag == FMT_END) {
              /* Move to next track */
             UNIT               *base =
                    (u > NUM_DEVS_DSK) ? &uptr[-NUM_DEVS_DSK] : uptr;

            if (((dtrack[u]+1) / disk_type[base->u4].track) !=
                  (dtrack[u] / disk_type[base->u4].track)) {
                  sim_debug(DEBUG_DATA, &dsk_dev, "eor\n");
                  return 1;
            }
         }
         break;
    case DVSR:          /* Verify single record */
         if (flag != FMT_DATA) {
             sim_debug(DEBUG_DATA, &dsk_dev, "eor\n");
             return 1;
         }
    }
    return 0;
}

/* Convert BCD track address to binary address */
int
bcd_to_track(uint32 addr)
{
    int                 trk = 0;
    int                 i;

    for (i = 28; i >= 16; i -= 4) {
        trk = (trk * 10) + ((addr >> i) & 0xf);
    }
    return trk;
}

/* Boot disk. Build boot card loader in memory and transfer to it */
t_stat
dsk_boot(int unit_num, DEVICE * dptr)
{
#ifdef I7090
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags) - 1;
    int                 sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    int                 dev = uptr->u3 & 0xff;
    int                 msk = (chan / 2) | ((chan & 1) << 11);
    extern uint16       IC;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    if (dev == 0)
        dev = 012;
    if (uptr->flags & CTSS_BOOT) {
         /* Build CTSS boot program in memory */
         /* Read first cylinder into B-Core */
         M[0] = 0377777000100LL;      /*  IORT    BOTTOM,,-1  */
         M[1] = 0006000000001LL;      /*   TCOA    *        */
         M[2] = 0007400400100LL;      /* START  TSX     ENTER,4 */
         M[0100] = 0076000000350LL;  /* ENTER  RICU          */
         M[0100] |= (chan + 1) << 9;
         M[0101] = 0054000000120LL;   /*      RSCU    READ   */
         M[0101] |= ((t_uint64) (msk)) << 24;
         M[0102] = 0006000000102LL;   /*      TCOU    *      */
         M[0102] |= ((t_uint64) (chan)) << 24;
         M[0103] = 0476100000042LL;   /*      SEB            */
         M[0104] = 0450000000000LL;   /*      CAL     0      */
         M[0105] = 0036100477777LL;   /*      ACL     32767,4 */
         M[0106] = 0200001400105LL;   /*      TIX     *-1,4,1 */
         M[0107] = 0476100000041LL;   /*      SEA            */
         M[0110] = 0032200000131LL;   /*      ERA     CHKSUM */
         M[0111] = 0450100000046LL;   /*      ORA     ULOC   */
         M[0112] = 0010000000132LL;   /*      TZE     EXIT   */
         M[0113] = 0000000000002LL;   /*      HTR     START  */
         M[0114] = 0101212001212LL;
         M[0114] |= ((t_uint64) (dev)) << 12;
         M[0115] = 0121212121212LL;
         M[0116] = 0100512001212LL;
         M[0116] |= ((t_uint64) (dev)) << 12;
         M[0117] = 0121267671212LL;
         M[0120] = 0700000000004LL;   /*  READ   SMS     4      */
         M[0120] |= sel;
         M[0121] = 0200000000114LL;   /*         CTL     SEEK   */
         M[0122] = 0500000200122LL;   /*         TCM     *,,0   */
         M[0123] = 0200000200116LL;   /*         CTLR    CYLOP  */
         M[0124] = 0400007000125LL;   /*         CPYP    *+1,,N */
         IC = 02;
    } else {
         /* Build IBSYS Boot program in memory */
         M[0] = 0000025000101LL;        /*      IOCD RSCQ,,21 */
         M[1] = 0006000000001LL;        /*      TCOA * */
         M[2] = 0002000000101LL;        /*      TRA RSCQ */

         M[0101] = 0054000000115LL;     /* RSCQ RSCC SMSQ  Mod */
         M[0101] |= ((t_uint64) (msk)) << 24;
         M[0102] = 0064400000000LL;     /* SCDQ SCDC 0  Mod */
         M[0102] |= ((t_uint64) (msk)) << 24;
         M[0103] = 0044100000000LL;     /*      LDI 0 */
         M[0104] = 0405400007100LL;     /*      LFT 7100 */
         M[0105] = 0002000000110LL;     /*      TRA *+3 */
         M[0106] = 0006000000102LL;     /* TCOQ TCOC SCDQ  Mod */
         M[0106] |= ((t_uint64) (chan)) << 24;
         M[0107] = 0002000000003LL;     /*      TRA 3    Enter IBSYS */
         M[0110] = 0076000000350LL;     /* RICQ RICC **    Mod */
         M[0110] |= (chan + 1) << 9;
         M[0111] = 0500512001212LL;     /*LDVCY DVCY  Mod */
         M[0111] |= ((t_uint64) (dev)) << 12;
         M[0112] = 0121222440000LL;     /*      *    */
         M[0113] = 0501212001212LL;     /*LDSEK DSEEK  Mod */
         M[0113] |= ((t_uint64) (dev)) << 12;
         M[0114] = 0121200000000LL;     /*      *  */
         M[0115] = 0700000000016LL;     /* SMSQ SMS   14 */
         M[0115] |= sel;
         M[0116] = 0200000000113LL;     /*      CTL   LDSEK */
         M[0117] = 0500000200117LL;     /*      TCM   *,,, */
         M[0120] = 0200000200111LL;     /*      CTLR  LDVCY */
         M[0121] = 0400001000122LL;     /*      CPYP  *+1,,1 */
         M[0122] = 0000000000122LL;     /*      WTR * */
         M[0123] = 0100000000121LL;     /*      TCH  *-2 */
         M[0124] = 0500000000000LL;     /*      CPYD  0,,0 */
         M[0125] = 0340000000125LL;     /*      TWT   * */
         IC = 0101;
     }

    return SCPE_OK;
#else
    return SCPE_NOFNC;
#endif
}

void
dsk_ini(UNIT * uptr, t_bool f)
{
    uptr->u5 = 0;
}

t_stat
dsk_reset(DEVICE * dptr)
{
    int                 i;
    int                 t;

    for (i = 0; i < NUM_CHAN; i++) {
        sense[i * 2] = sense[i * 2 + 1] = STAT_SIXBIT;
        sense_unit[i * 2] = sense_unit[i * 2 + 1] = 0;
    }
    for (i = 0; i < NUM_DEVS_DSK; i++) {
        dtrack[i] = 077777;
        fmt_cyl[i] = 077777;
        arm_cyl[i] = 0;
        dtrack[i + NUM_DEVS_DSK] = 077777;
        fmt_cyl[i + NUM_DEVS_DSK] = 077777;
        arm_cyl[i + NUM_DEVS_DSK] = 0;
        dtrack[i + (NUM_DEVS_DSK * 2)] = 077777;
        fmt_cyl[i + (NUM_DEVS_DSK * 2)] = 077777;
        arm_cyl[i + (NUM_DEVS_DSK * 2)] = 0;
        dtrack[i + (NUM_DEVS_DSK * 3)] = 077777;
        fmt_cyl[i + (NUM_DEVS_DSK * 3)] = 077777;
        arm_cyl[i + (NUM_DEVS_DSK * 3)] = 0;
        t = dptr->units[i].u4;
        /* Fill in max capacity */
        dptr->units[i + NUM_DEVS_DSK].u3 = 0xff;
        dptr->units[i + (NUM_DEVS_DSK * 2)].u3 = 0xff;
        dptr->units[i + (NUM_DEVS_DSK * 3)].u3 = 0xff;
        dptr->units[i].capac = disk_type[t].mods * disk_type[t].bpt *
            disk_type[t].arms * disk_type[t].track * disk_type[t].cyl;
        if (disk_type[t].arms > 1)
            dptr->units[i + NUM_DEVS_DSK].u3 =
                0x10 | dptr->units[i].u3 | (i << 8);
        if (disk_type[t].mods > 1) {
            dptr->units[i + (NUM_DEVS_DSK * 2)].u3 = (i<<8) | (dptr->units[i].u3 + 1);
            if (disk_type[t].arms > 1)
                dptr->units[i + (NUM_DEVS_DSK * 3)].u3 = (i<<8) | 0x10 |
                                                        (dptr->units[i].u3 + 1);
        }
    }
    return SCPE_OK;
}

/* Disk option setting commands */

t_stat
dsk_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 i, u;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    for (i = 0; disk_type[i].name != 0; i++) {
        if (strcmp(disk_type[i].name, cptr) == 0) {
            uptr->u4 = i;
            uptr[NUM_DEVS_DSK].u4 = i;
            uptr[NUM_DEVS_DSK].u3 = 0xff;
            uptr[NUM_DEVS_DSK * 2].u4 = i;
            uptr[NUM_DEVS_DSK * 2].u3 = 0xff;
            uptr[NUM_DEVS_DSK * 3].u4 = i;
            uptr[NUM_DEVS_DSK * 3].u3 = 0xff;
            uptr->capac = disk_type[i].mods * disk_type[i].bpt *
                disk_type[i].arms * disk_type[i].track * disk_type[i].cyl;
            /* Adjust other disks in case changed number arms/modules */
            u = uptr->u3 & 0xf0f;
            if (disk_type[uptr->u4].arms > 1)
                uptr[NUM_DEVS_DSK].u3 = u | 0x10;
            if (disk_type[uptr->u4].mods > 1) {
                uptr[NUM_DEVS_DSK * 2].u3 = u + 1;
                if (disk_type[uptr->u4].arms > 1)
                    uptr[NUM_DEVS_DSK * 3].u3 = (u + 1) | 0x10;
            }
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat
dsk_get_type(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs(disk_type[uptr->u4].name, st);
    return SCPE_OK;
}

t_stat
dsk_set_module(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 u;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    if (*cptr == '\0' || cptr[1] != '\0')
        return SCPE_ARG;
    if (*cptr < '0' || *cptr > '8')
        return SCPE_ARG;
    if (*cptr & 1)
        return SCPE_ARG;
    u = uptr->u3 & 0xf00;
    uptr->u3 = u | (*cptr - '0');
    uptr[NUM_DEVS_DSK].u3 = 0xff;
    uptr[NUM_DEVS_DSK * 2].u3 = 0xff;
    uptr[NUM_DEVS_DSK * 3].u3 = 0xff;
    if (disk_type[uptr->u4].arms > 1)
        uptr[NUM_DEVS_DSK].u3 = u | 0x10 | (*cptr - '0');
    if (disk_type[uptr->u4].mods > 1) {
        uptr[NUM_DEVS_DSK * 2].u3 = u | ((*cptr - '0') + 1);
        if (disk_type[uptr->u4].arms > 1)
            uptr[NUM_DEVS_DSK * 3].u3 = u | 0x10 | ((*cptr - '0') + 1);
    }
    return SCPE_OK;
}

t_stat
dsk_get_module(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "Module=%d", uptr->u3 & 0xff);
    return SCPE_OK;
}

t_stat dsk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
      int i;
fprintf (st, "IBM 7631 Disk File Controller\n\n");
fprintf (st, "The IBM 7631 Disk File Controller supports several types of ");
fprintf (st, "disk drives and\ndrums. The drive must be formatted for use ");
fprintf (st, "of the system. This is handled by\nutilities provided by the ");
fprintf (st, "operating system. This will write a special format\ntrack.\n\n");
fprintf (st, "Use:\n\n");
fprintf (st, "    sim> SET DKn TYPE=type\n");
fprintf (st, "Type can be: ");
for (i = 0; disk_type[i].name != 0; i++) {
    fprintf(st, "%s", disk_type[i].name);
    if (disk_type[i+1].name != 0)
        fprintf(st, ", ");
}
fprintf (st, ".\nEach drive has the following storage capacity:\n\n");
for (i = 0; disk_type[i].name != 0; i++) {
    int32 size = disk_type[i].mods * disk_type[i].bpt *
                disk_type[i].arms * disk_type[i].track * disk_type[i].cyl;
    char  sm = 'K';
    size /= 1024;
    if (size > 5000) {
        size /= 1024;
        sm = 'M';
    }
    fprintf(st, "      %-8s %4d%cB %d modules\n", disk_type[i].name, size, sm,
           disk_type[i].mods);
}
fprintf (st, "\nTo enable formating the format switch must be set ");
fprintf (st, "to enable, and the Home\nAddress 2 write must be enabled.\n");
fprintf (st, "To do this:\n\n");
fprintf (st, "     sim> SET DKn FORMAT HA2\n\n");
fprintf (st, "To prevent accidental formating of the drive use:\n\n");
fprintf (st, "     sim> SET DKn NOFORMAT NOHA2\n\n");
help_set_chan_type(st, dptr, "IBM 7631 Disk File");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *dsk_description (DEVICE *dptr)
{
return "IBM 7631 disk file controller";
}

#endif
