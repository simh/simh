/* i7090_drum.c: IBM 7090 Drum

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

   This supports the direct channel and 704 type drums.

*/

#include "i7090_defs.h"

#ifdef NUM_DEVS_DR
#define UNIT_DRM        UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX | \
                        UNIT_BUFABLE | UNIT_MUSTBUF

/* Device status information stored in u5 */
#define DRMSTA_READ     000001  /* Unit is in read */
#define DRMSTA_WRITE    000002  /* Unit is in write */
#define DRMSTA_CMD      000004  /* Unit has recieved a cmd */
#define DRMSTA_UNIT     000170  /* Unit mask */
#define DRMSTA_UNITSHIFT     3
#define DRMSTA_START    000200  /* Drum has started to transfer */
#define DRMWORDTIME     us_to_ticks(96) /* Number of cycles per drum word */
#define DRMSIZE          2048   /* Number words per drum */
#define DRMMASK      (DRMSIZE-1)/* Mask of drum address */

uint32              drm_cmd(UNIT *, uint16, uint16);
t_stat              drm_srv(UNIT *);
t_stat              drm_boot(int32, DEVICE *);
void                drm_ini(UNIT *, t_bool);
t_stat              drm_reset(DEVICE *);
extern t_stat       chan_boot(int32, DEVICE *);
uint32              drum_addr;  /* Read/write drum address */
t_stat              set_units(UNIT * uptr, int32 val, CONST char *cptr,
                              void *desc);
t_stat              drm_attach(UNIT * uptr, CONST char *file);
t_stat              drm_detach(UNIT * uptr);

t_stat              get_units(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat              drm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *drm_description (DEVICE *dptr);


UNIT                drm_unit[] = {
     {UDATA(&drm_srv, UNIT_S_CHAN(0) | UNIT_DRM, NUM_UNITS_DR * DRMSIZE), 0,
          NUM_UNITS_DR},
};

MTAB                drm_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "UNITS", "UNITS", &set_units,
          &get_units, NULL},
#if NUM_CHAN != 1
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan, &get_chan,
          NULL},
#endif
    {0}
};

DEVICE              drm_dev = {
    "DR", drm_unit, NULL /* Registers */ , drm_mod,
    NUM_DEVS_DR, 8, 15, 1, 8, 36,
    NULL, NULL, &drm_reset, &drm_boot, &drm_attach, &drm_detach,
    &drm_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &drm_help, NULL, NULL, &drm_description
};

uint32 drm_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = dev;

    u -= drm_dib.addr;
    if (u > uptr->u3)
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
            sim_debug(DEBUG_CMD, &drm_dev, "WRS %o\n", dev);
            chan_set_sel(chan, 1);
            break;
        default:
            return SCPE_IOERR;
        }
        /* Choose which part to use */
        uptr->u5 |= u << DRMSTA_UNITSHIFT;
        drum_addr = 0;          /* Set drum address */
        chan_clear_status(chan);
        /* Make sure drum is spinning */
        sim_activate(uptr, us_to_ticks(100));
        return SCPE_OK;
    }
    return SCPE_IOERR;
}

t_stat drm_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    t_uint64           *buf = (t_uint64*)uptr->filebuf;
    t_stat              r;

    uptr->u6++;                 /* Adjust rotation */
    uptr->u6 &= DRMMASK;
    /* Channel has disconnected, abort current read. */
    if (uptr->u5 & DRMSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 = 0;
        chan_clear(chan, DEV_WEOR | DEV_SEL | STA_ACTIVE);
        sim_debug(DEBUG_CHAN, &drm_dev, "Disconnect\n");
    }

    /* Check if we have a address match */
    if ((chan_flags[chan] & (STA_ACTIVE | DEV_SEL)) == (STA_ACTIVE | DEV_SEL)
         && (uptr->u5 & (DRMSTA_READ | DRMSTA_WRITE))
         && (uint32)uptr->u6 == (drum_addr & DRMMASK)) {
        uint32            addr =
            (((uptr->u5 & DRMSTA_UNIT) >> DRMSTA_UNITSHIFT) << 11)
            + (drum_addr & DRMMASK);

        /* Try and transfer a word of data */
        if (uptr->u5 & DRMSTA_READ) {
            r = chan_write(chan, &buf[addr], DEV_DISCO);
        } else {
            if (addr >= uptr->hwmark)
                uptr->hwmark = (uint32)addr + 1;
            r = chan_read(chan, &buf[addr], DEV_DISCO);
        }
        switch (r) {
        case DATA_OK:
            sim_debug(DEBUG_DATA, &drm_dev, "loc %6o data %012llo\n", addr,
                                 buf[addr]);
            addr++;
            addr &= DRMMASK;
            drum_addr &= ~DRMMASK;
            drum_addr |= addr;
            break;

        case END_RECORD:
        case TIME_ERROR:
           /* If no data, disconnect */
            sim_debug(DEBUG_DATA, &drm_dev, "loc %6o missed\n", addr);
            chan_clear(chan, STA_ACTIVE | DEV_SEL);
            uptr->u5 = DRMSTA_CMD;
            break;
        }
    }
   /* Increase delay for index time */
    if (uptr->u6 == 0)
        sim_activate(uptr, us_to_ticks(120));
    else
        sim_activate(uptr, DRMWORDTIME);
    return SCPE_OK;
}

/* Boot from given device */
t_stat
drm_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_uint64           *buf = (t_uint64*)uptr->filebuf;
    int                 addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
/* Init for a read */
    if (drm_cmd(uptr, IO_RDS, 0301) != SCPE_OK)
        return STOP_IONRDY;
/* Copy first three records. */
    addr = 0;
    M[0] = buf[addr++];
    M[1] = buf[addr++];
    drum_addr = 2;
    return chan_boot(unit_num, dptr);
}

void
drm_ini(UNIT * uptr, t_bool f)
{
    uptr->u5 = 0;
}

t_stat
drm_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

/* Sets the number of drum units */
t_stat
set_units(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 i;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    i = 0;
    while (*cptr != '\0') {
        if (*cptr < '0' || *cptr > '9')
            return SCPE_ARG;
        i = (i * 10) + (*cptr++) - '0';
    }
    if (i < 0 || i > NUM_UNITS_DR)
        return SCPE_ARG;
    uptr->capac = i * 2048;
    uptr->u3 = i;
    return SCPE_OK;
}

t_stat
get_units(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "Units=%d", uptr->u3);
    return SCPE_OK;
}

t_stat
drm_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    return SCPE_OK;
}

t_stat
drm_detach(UNIT * uptr)
{
    sim_cancel(uptr);
    return detach_unit(uptr);
}

t_stat
drm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", drm_description(dptr));
   fprintf (st, "Up to %d units of drum could be used\n", NUM_UNITS_DR);
   fprintf (st, "    sim> set %s UNITS=n  to set number of units\n", dptr->name);
   help_set_chan_type(st, dptr, "Drums");
   fprintf (st, "Drums could be booted\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);

   return SCPE_OK;
}

const char *
drm_description (DEVICE *dptr)
{
    return "IBM 704/709 Drum";
}

#endif

