/* i7090_cdr.c: IBM 7090 Card Read.

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

   This is the standard card reader.

*/

#include "i7090_defs.h"
#include "sim_card.h"

#ifdef NUM_DEVS_CDR

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_026


/* std devices. data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

/* Device status information stored in u5 */
#define CDRSTA_EOR      002000   /* Hit end of record */
#define CDRPOSMASK      0770000  /* Bit Mask to retrive drum position */
#define CDRPOSSHIFT     12


t_stat              cdr_srv(UNIT *);
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
t_stat              cdr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *cdr_description (DEVICE *dptr);

UNIT                cdr_unit[] = {
#if NUM_DEVS_CDR > 1
    {UDATA(&cdr_srv, UNIT_S_CHAN(CHAN_A) | UNIT_CDR, 0), 3000}, /* A */
#endif
#if NUM_DEVS_CDR > 2
    {UDATA(&cdr_srv, UNIT_S_CHAN(CHAN_C) | UNIT_CDR, 0), 3000}, /* B */
#endif
#if NUM_DEVS_CDR > 3
    {UDATA(&cdr_srv, UNIT_S_CHAN(CHAN_E) | UNIT_CDR | UNIT_DIS, 0), 3000},      /* C */
#endif
    {UDATA(&cdr_srv, UNIT_S_CHAN(CHAN_CHPIO) | UNIT_CDR, 0), 3000},     /* D */
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
#if NUM_CHAN != 1
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL},
#endif
    {0}
};

DEVICE              cdr_dev = {
    "CDR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 36,
    NULL, NULL, &cdr_reset, &cdr_boot, &cdr_attach, &cdr_detach,
    &cdr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
};


uint32 cdr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);

    if ((uptr->flags & UNIT_ATT) != 0 && cmd == IO_RDS) {
        int                 u = (uptr - cdr_unit);

        /* Start device */
        if ((uptr->u5 & URCSTA_CMD) == 0) {
            if ((uptr->u5 & (URCSTA_ON | URCSTA_IDLE)) ==
                 (URCSTA_ON | URCSTA_IDLE) && (uptr->wait <= 60)) {
                uptr->wait += 100;      /* Wait for next latch point */
            } else
                uptr->wait = 75;        /* Startup delay */
            uptr->u5 |= URCSTA_READ | URCSTA_CMD | CDRPOSMASK;
            chan_set_sel(chan, 0);
            chan_clear_status(chan);
            sim_activate(uptr, us_to_ticks(1000));      /* activate */
            sim_debug(DEBUG_CMD, &cdr_dev, "RDS unit=%d\n", u);
            return SCPE_OK;
        }
        return SCPE_BUSY;
    }
    chan_set_attn(chan);
    return SCPE_NODEV;
}

t_stat cdr_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdr_unit);
    int                 pos, col, b;
    uint16              bit;
    t_uint64            mask, wd;
    struct _card_data   *data;

    /* Channel has disconnected, abort current read. */
    if (uptr->u5 & URCSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 &= ~(URCSTA_READ | URCSTA_CMD);
        uptr->u5 |= CDRPOSMASK;
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &cdr_dev, "unit=%d disconnecting\n", u);
    }

    /* Check to see if we have timed out */
    if (uptr->wait != 0) {
        /* If at end of record and channel is still active, do another read */
        if (uptr->wait == 30
            && ((uptr->u5 & (URCSTA_CMD|URCSTA_IDLE|URCSTA_READ|URCSTA_ON))
                 == (URCSTA_CMD | URCSTA_IDLE | URCSTA_ON))
            && chan_test(chan, STA_ACTIVE)) {
            uptr->u5 |= URCSTA_READ;
            sim_debug(DEBUG_CHAN, &cdr_dev, "unit=%d restarting\n", u);
        }
        uptr->wait--;
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    }

    /* If no read request, go to idle mode */
    if ((uptr->u5 & URCSTA_READ) == 0) {
        if ((uptr->u5 & URCSTA_EOF) || (uptr->u5 & URCSTA_IDLE)) {
            uptr->u5 &= ~(URCSTA_ON | URCSTA_IDLE);     /* Turn motor off */
        } else {
            uptr->wait = 85;    /* Delay 85ms */
            uptr->u5 |= URCSTA_IDLE;    /* Go idle */
            sim_activate(uptr, us_to_ticks(1000));
        }
        return SCPE_OK;
    }

    /* Motor is up to speed now */
    uptr->u5 |= URCSTA_ON;
    uptr->u5 &= ~URCSTA_IDLE;

    pos = (uptr->u5 & CDRPOSMASK) >> CDRPOSSHIFT;
    if (pos == (CDRPOSMASK >> CDRPOSSHIFT)) {
        switch (sim_read_card(uptr)) {
        case SCPE_UNATT:
        case SCPE_IOERR:
            sim_debug(DEBUG_EXP, &cdr_dev, "unit=%d Setting ATTN\n", u);
            chan_set_error(chan);
            chan_set_attn(chan);
            uptr->u5 &= ~URCSTA_READ;
            sim_activate(uptr, us_to_ticks(1000));
            return SCPE_OK;
        case SCPE_EOF:
            sim_debug(DEBUG_EXP, &cdr_dev, "unit=%d EOF\n", u);
            chan_set_eof(chan);
            chan_set_attn(chan);
            uptr->u5 &= ~URCSTA_READ;
            sim_activate(uptr, us_to_ticks(1000));
            return SCPE_OK;
        case SCPE_OK:
            break;
        }
        pos = 0;
    }

    /* Check if everything read in, if so return EOR now */
    if (pos == 24) {
        sim_debug(DEBUG_CHAN, &cdr_dev, "unit=%d set EOR\n", u);
        chan_set(chan, DEV_REOR);
        uptr->u5 &= ~URCSTA_READ;
        uptr->u5 |= CDRSTA_EOR | CDRPOSMASK;
        uptr->wait = 86;
        sim_activate(uptr, us_to_ticks(1000));
        return SCPE_OK;
    }

    data = (struct _card_data *)uptr->up7;
    /* Bit flip into read buffer */
    bit = 1 << (pos / 2);
    mask = 1;
    wd = 0;
    b = (pos & 1)?36:0;

    for (col = 35; col >= 0; mask <<= 1) {
        if (data->image[col-- + b] & bit)
             wd |= mask;
    }

    switch (chan_write (chan, &wd,/* (pos == 23) ? DEV_REOR :*/ 0)) {
    case DATA_OK:
        sim_debug(DEBUG_DATA, &cdr_dev, "unit=%d read row %d %012llo\n", u,
                  pos, wd);
        pos++;
        uptr->u5 &= ~CDRPOSMASK;
        uptr->u5 |= pos << CDRPOSSHIFT;
        uptr->wait = 0;
        sim_activate(uptr, (pos & 1) ? us_to_ticks(300) : us_to_ticks(8000));
        return SCPE_OK;

    case END_RECORD:
        sim_debug(DEBUG_CHAN, &cdr_dev, "unit=%d got EOR\n", u);
        uptr->u5 &= ~CDRPOSMASK;
        uptr->u5 |= 24 << CDRPOSSHIFT;
        uptr->wait = 8 * (12 - (pos / 2)) /*+ 86*/;
        break;

    case TIME_ERROR:
        sim_debug(DEBUG_EXP, &cdr_dev, "unit=%d no data\n", u);
        uptr->u5 &= ~CDRPOSMASK;
        uptr->u5 |= 24 << CDRPOSSHIFT;
        uptr->wait = 8 * (12 - (pos / 2)) /*+ 85*/;
        break;
    }

    sim_activate(uptr, us_to_ticks(1000));
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags);
    t_stat              r;
    int                 pos;
    struct _card_data   *data;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    uptr->u5 = 0;
/* Init for a read */
    if (cdr_cmd(uptr, IO_RDS, cdr_dib.addr) != SCPE_OK)
        return STOP_IONRDY;
    r = sim_read_card(uptr);
    if (r != SCPE_OK)
        return r;

/* Copy first three records. */
    data = (struct _card_data *)uptr->up7;
    uptr->u5 &= ~CDRPOSMASK;
    for(pos = 0; pos <3; pos++) {
        uint16          bit = 1 << (pos / 2);
        t_uint64        mask = 1;
        int             b = (pos & 1)?36:0;
        int             col;

        if (pos == 2 && chan == 0)
            break;
        M[pos] = 0;
        for (col = 35; col >= 0; mask <<= 1) {
            if (data->image[col-- + b] & bit)
                 M[pos] |= mask;
        }
        sim_debug(DEBUG_DATA, &cdr_dev, "boot read row %d %012llo\n",
                  pos, M[pos]);
    }
    uptr->u5 |= pos << CDRPOSSHIFT;
/* Make sure channel is set to start reading rest. */
    return chan_boot(unit_num, dptr);
}

t_stat
cdr_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    uptr->u4 = 0;
    return SCPE_OK;
}

t_stat
cdr_detach(UNIT * uptr)
{
    return sim_card_detach(uptr);
}

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   const char *cpu = cpu_description(&cpu_dev);

   fprintf (st, "%s\n\n", cdr_description(dptr));
#if NUM_DEVS_CDR > 3
   fprintf (st, "The %s supports up to four card readers\n\n", cpu);
#elif NUM_DEVS_CDR > 2
   fprintf (st, "The %s supports up to three card readers\n\n", cpu);
#elif NUM_DEVS_CDR > 1
   fprintf (st, "The %s supports up to two card readers\n\n", cpu);
#elif NUM_DEVS_CDR > 0
   fprintf (st, "The %s supports one card reader\n\n", cpu);
#endif
   help_set_chan_type(st, dptr, "Card readers");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   fprintf (st, "\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "711 Card Reader";
}

#endif
