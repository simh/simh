/* i7090_drum.c: IBM 7320A High Speed Drum

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

   High speed drum for CTSS

*/

#include "i7090_defs.h"

#ifdef NUM_DEVS_HD
#define UNIT_DRM        UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX | \
                        UNIT_BUFABLE | UNIT_MUSTBUF

/* Device status information stored in u5 */
#define DRMSTA_READ     000001  /* Unit is in read */
#define DRMSTA_WRITE    000002  /* Unit is in write */
#define DRMSTA_START    000004  /* Which half of drum accessing */
#define DRMSTA_CMD      000010  /* Unit has recieved a cmd */
#define DRMSTA_UNIT     000700  /* Unitmask */
#define DRMSTA_SHFT     6

uint32              hsdrm_cmd(UNIT *, uint16, uint16);
t_stat              hsdrm_srv(UNIT *);
void                hsdrm_ini(UNIT *, t_bool);
t_stat              hsdrm_reset(DEVICE *);
t_uint64            hsdrm_addr; /* Read/write drum address */
t_stat              set_hunits(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              get_hunits(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat              hsdrm_attach(UNIT * uptr, CONST char *file);
t_stat              hsdrm_detach(UNIT * uptr);
t_stat              hsdrm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *hsdrm_description (DEVICE *dptr);


UNIT                hsdrm_unit[] = {
    {UDATA (&hsdrm_srv, UNIT_S_CHAN(7) | UNIT_DRM,
                NUM_UNITS_HD * 8 * 32767), 0, NUM_UNITS_HD},
};

MTAB                hsdrm_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "UNITS", "UNITS",
                     &set_hunits, &get_hunits, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN",
                     &set_chan, &get_chan, NULL},
    {0}
};

DEVICE              hsdrm_dev = {
    "HD", hsdrm_unit, NULL /* Registers */ , hsdrm_mod,
    NUM_DEVS_HD, 8, 15, 1, 8, 36,
    NULL, NULL, &hsdrm_reset, NULL, &hsdrm_attach, &hsdrm_detach,
    &hsdrm_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &hsdrm_help, NULL, NULL, &hsdrm_description
};

uint32 hsdrm_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);

    if ((uptr->flags & UNIT_ATT) != 0) {
        /* Delay if transfer still in progress. */
        if (chan_active(chan))
            return SCPE_BUSY;
        /* Wait for device to time out */
        if (uptr->u5 & DRMSTA_CMD)
            return SCPE_BUSY;
        switch (cmd) {
        case IO_RDS:
            /* Start device */
            uptr->u5 = DRMSTA_READ | DRMSTA_CMD;
            chan_set_sel(chan, 0);
            sim_debug(DEBUG_CMD, &hsdrm_dev, "RDS dev %o\n", dev);
            break;
        case IO_WRS:
            /* Start device */
            uptr->hwmark = uptr->capac; /* Mark as changed */
            uptr->u5 = DRMSTA_WRITE | DRMSTA_CMD;
            chan_set_sel(chan, 1);
            sim_debug(DEBUG_CMD, &hsdrm_dev, "WRS dev %o\n", dev);
            break;
        default:
            return SCPE_IOERR;
        }
        hsdrm_addr = 0;         /* Set drum address */
        if (!sim_is_active(uptr))
            sim_activate(uptr, us_to_ticks(100));
        return SCPE_OK;
    }
    return SCPE_IOERR;
}

t_stat hsdrm_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    t_uint64           *buf = (t_uint64 *)uptr->filebuf;
    t_stat              r;

    /* Channel has disconnected, abort current read. */
    if (uptr->u5 & DRMSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 = 0;
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &hsdrm_dev, "disconnecting\n");
        sim_activate(uptr, us_to_ticks(50));
    }

    uptr->u6++;                 /* Adjust rotation */
    uptr->u6 &= 007777;

    /* Check if we have a address match */
    if ((chan_flags[chan] & (STA_ACTIVE | DEV_SEL)) == (STA_ACTIVE | DEV_SEL)
        && uptr->u5 & (DRMSTA_READ | DRMSTA_WRITE)
        && (uint32)uptr->u6 == (hsdrm_addr & 007777)) {
            int                 addr =
                ((hsdrm_addr >> 12) & 07000000) |
                  ((hsdrm_addr >> 3) & 0700000) |
                          (hsdrm_addr & 077777);
           sim_debug(DEBUG_DETAIL, &hsdrm_dev, "drum addr %o\n\r", addr);
            if (((addr >> 18) & 07) > uptr->u3) {
                chan_set(chan, DEV_REOR | CHS_ATTN | CHS_ERR);
                goto next;
            }
            /* Flag to disconnect without setting iocheck */
            if (uptr->u5 & DRMSTA_READ)
                r = chan_write(chan, &buf[addr], DEV_DISCO);
            else
                r = chan_read(chan, &buf[addr], DEV_DISCO);
            switch (r) {
            case DATA_OK:
                sim_debug(DEBUG_DATA, &hsdrm_dev,
                          "transfer %s %o: %012llo\n\r",
                          (uptr->u5 & DRMSTA_READ) ? "read" : "write",
                          addr, buf[addr]);
                hsdrm_addr++;
                hsdrm_addr &= 070007077777LL;
                if ((hsdrm_addr & (2048 - 1)) == 0)
                    chan_set(chan, DEV_REOR);
                break;

            case END_RECORD:
            case TIME_ERROR:
                uptr->u5 = DRMSTA_CMD;
                break;
            }
        }
  next:
    sim_activate(uptr, us_to_ticks(20));
    return SCPE_OK;
}

void
hsdrm_ini(UNIT * uptr, t_bool f)
{
    uptr->u5 = 0;
}

t_stat
hsdrm_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

/* Sets the number of drum units */
t_stat
set_hunits(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
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
    if (i < 0 || i > NUM_UNITS_HD)
        return SCPE_ARG;
    uptr->capac = i * 32767 * 8;
    uptr->u3 = i;
    return SCPE_OK;
}

t_stat
get_hunits(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "Units=%d", uptr->u3);
    return SCPE_OK;
}

t_stat
hsdrm_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    sim_activate(uptr, us_to_ticks(100));
    return SCPE_OK;
}

t_stat
hsdrm_detach(UNIT * uptr)
{
    sim_cancel(uptr);
    return detach_unit(uptr);
}

t_stat
hsdrm_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", hsdrm_description(dptr));
   fprintf (st, "The High speed drum supports up to %d units of storage\n", NUM_UNITS_HD);
   fprintf (st, "Each unit held 265k words of data\n");
   help_set_chan_type(st, dptr, "High speed drum");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}


const char *
hsdrm_description (DEVICE *dptr)
{
    return "IBM 7320A Drum for CTSS";
}

#endif

