/* i7080_drum.c: IBM 7080 Drum

   Copyright (c) 2007-2016, Richard Cornwell

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

   Provides support for 702/705 drums.

   Drums are arranged in tracks of 200 characters each.

   Writing continues until a end of record is recieved. At which point a
   drum mark is written. If more then 200 characters are written the next
   track is automaticaly selected.

   Reading continues until a drum mark is read.

   Drums address is 1000-1999

*/

#include "i7080_defs.h"

#ifdef NUM_DEVS_DR
#define UNIT_DRM        UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX | \
                        UNIT_BUFABLE | UNIT_MUSTBUF

/* Device status information stored in u5 */
#define DRMSTA_READ     000001  /* Unit is in read */
#define DRMSTA_WRITE    000002  /* Unit is in write */
#define DRMSTA_CMD      000004  /* Unit has recieved a cmd */
#define DRMSTA_START    000200  /* Drum has started to transfer */
#define DRMWORDTIME         20  /* Number of cycles per drum word */
#define DRMCHARTRK         200  /* Characters per track */

uint32              drm_cmd(UNIT *, uint16, uint16);
t_stat              drm_srv(UNIT *);
t_stat              drm_boot(int32, DEVICE *);
void                drm_ini(UNIT *, t_bool);
t_stat              drm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *drm_description (DEVICE *dptr);
extern t_stat       chan_boot(int32, DEVICE *);

UNIT                drm_unit[] = {
     {UDATA(&drm_srv, UNIT_S_CHAN(0) | UNIT_DRM, DRMCHARTRK * 1000), 0, 0},
};

DEVICE              drm_dev = {
    "DR", drm_unit, NULL /* Registers */ , NULL,
    1, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &drm_boot, NULL, NULL,
    &drm_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &drm_help, NULL, NULL, &drm_description
};

uint32 drm_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 addr = dev;

    addr -= drm_dib.addr * DRMCHARTRK;
    if (addr > (int32)uptr->capac)
        return SCPE_NODEV;
    if ((uptr->flags & UNIT_ATT) != 0) {
        switch (cmd) {
        case IO_RDS:
            /* Start device */
            uptr->u5 = DRMSTA_READ | DRMSTA_CMD;
            sim_debug(DEBUG_CMD, &drm_dev, "RDS %o\n", dev);
            chan_set_sel(chan, 0);
            break;
        case IO_WRS:
            /* Start device */
            uptr->u5 = DRMSTA_WRITE | DRMSTA_CMD;
            uptr->hwmark = uptr->capac;
            sim_debug(DEBUG_CMD, &drm_dev, "WRS %o\n", dev);
            chan_set_sel(chan, 1);
            break;
        default:
            return SCPE_IOERR;
        }
        /* Choose which part to use */
        uptr->u6 = addr;                /* Set drum address */
        chan_clear(chan, CHS_ATTN);     /* Clear attentions */
        /* Make sure drum is spinning */
        sim_activate(uptr, DRMWORDTIME);
        return SCPE_OK;
    }
    return SCPE_IOERR;
}

t_stat drm_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    uint8               *buf = (uint8 *)uptr->filebuf;
    t_stat              r;

    /* Channel has disconnected, abort current read. */
    if (uptr->u5 & DRMSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 = 0;
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &drm_dev, "Disconnect\n");
        return SCPE_OK;
    }

    /* Check if we have a address match */
    if ((chan_flags[chan] & (STA_ACTIVE | DEV_SEL)) == (STA_ACTIVE | DEV_SEL)
         && (uptr->u5 & (DRMSTA_READ | DRMSTA_WRITE))) {
        if (uptr->u6 > (int32)uptr->capac) {
            uptr->u5 = DRMSTA_CMD;
            chan_set(chan, CHS_ATTN);
            sim_activate(uptr, DRMWORDTIME);
            return SCPE_OK;
        }

        /* Try and transfer a word of data */
        if (uptr->u5 & DRMSTA_READ) {
            uint8       ch = buf[uptr->u6++];
            r = chan_write_char(chan, &ch, (buf[uptr->u6] == 0)? DEV_REOR:0);
        } else {
            r = chan_read_char(chan, &buf[uptr->u6], 0);
            uptr->u6++;
        }
        switch (r) {
        case DATA_OK:
            sim_debug(DEBUG_DATA, &drm_dev, "loc %6d data %02o\n", uptr->u6,
                                 buf[uptr->u6]);
            break;

        case END_RECORD:
        case TIME_ERROR:
           /* If no data, disconnect */
            sim_debug(DEBUG_DATA, &drm_dev, "loc %6d done\n", uptr->u6);
            if (uptr->u5 & DRMSTA_WRITE)
                buf[uptr->u6] = 0;      /* Write mark */
            uptr->u5 = DRMSTA_CMD;
            break;
        }
    }
    sim_activate(uptr, DRMWORDTIME);
    return SCPE_OK;
}

/* Boot from given device */
t_stat
drm_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
/* Init for a read */
    if (drm_cmd(uptr, IO_RDS, drm_dib.addr) != SCPE_OK)
        return STOP_IONRDY;
    return chan_boot(unit_num, dptr);
}

void
drm_ini(UNIT *uptr, t_bool f) {
    uptr->u5 = 0;
}

t_stat
drm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
   fprintf(st, "Drum device for IBM 702 and 705\n\n");
   fprintf(st, "The Drum had 1000 tracks with the capacity of %d digits ",
       DRMCHARTRK);
   fprintf(st, "per track\n");
   fprintf(st, "The drum does not have any settings to change\n");
   return SCPE_OK;
}


const char *
drm_description (DEVICE *dptr) {
   return "Drum";
}

#endif

