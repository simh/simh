/* i7090_cdp.c: IBM 7090 Card punch.

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

   This is the standard card punch.

*/

#include "i7090_defs.h"
#include "sim_card.h"
#ifdef NUM_DEVS_CDP
#define UNIT_CDP        UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ


/* std devices. data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

/* Device status information stored in u5 */
#define CDPSTA_PUNCH    0004000  /* Punch strobe during run */
#define CDPSTA_POSMASK  0770000
#define CDPSTA_POSSHIFT 12

t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *cdp_description (DEVICE *dptr);

UNIT                cdp_unit[] = {
#if NUM_DEVS_CDP > 1
    {UDATA(&cdp_srv, UNIT_S_CHAN(CHAN_A) | UNIT_CDP, 0), 6000}, /* A */
#endif
#if NUM_DEVS_CDP > 2
    {UDATA(&cdp_srv, UNIT_S_CHAN(CHAN_C) | UNIT_CDP, 0), 6000}, /* B */
#endif
#if NUM_DEVS_CDP > 3
    {UDATA(&cdp_srv, UNIT_S_CHAN(CHAN_E) | UNIT_CDP | UNIT_DIS, 0), 6000},      /* C */
#endif
    {UDATA(&cdp_srv, UNIT_S_CHAN(CHAN_CHPIO) | UNIT_CDP, 0), 6000},     /* D */
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
#if NUM_CHAN != 1
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
          &get_chan, NULL},
#endif
    {0}
};

DEVICE              cdp_dev = {
    "CDP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 36,
    NULL, NULL, &cdp_reset, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};

/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/


uint32 cdp_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    extern uint16   IC;

    if ((uptr->flags & UNIT_ATT) != 0 && cmd == IO_WRS) {
        /* Start device */
        if (!(uptr->u5 & URCSTA_CMD)) {
            dev_pulse[chan] &= ~PUNCH_M;
            uptr->u5 &= ~CDPSTA_PUNCH;
            if ((uptr->u5 & URCSTA_ON) == 0) {
                uptr->wait = 330;       /* Startup delay */
            } else if (uptr->u5 & URCSTA_IDLE && uptr->wait <= 30) {
                uptr->wait += 85;       /* Wait for next latch point */
            }
            uptr->u5 |= (URCSTA_WRITE | URCSTA_CMD);
            uptr->u5 &= ~CDPSTA_POSMASK;
            chan_set_sel(chan, 1);
            chan_clear_status(chan);
            sim_activate(uptr, us_to_ticks(1000));      /* activate */
            sim_debug(DEBUG_CMD, &cdp_dev, "%05o WRS unit=%d\n", IC, u);
            return SCPE_OK;
        }
    }
    chan_set_attn(chan);
    return SCPE_IOERR;
}

t_stat cdp_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    uint16             *image = (uint16 *)(uptr->up7);
    int                 pos;
    t_uint64            wd;
    int                 bit;
    t_uint64            mask;
    int                 b;
    int                 col;

    /* Channel has disconnected, abort current card. */
    if (uptr->u5 & URCSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        if ((uptr->u5 & CDPSTA_POSMASK) != 0) {
            sim_debug(DEBUG_DETAIL, &cdp_dev, "punch card\n");
            sim_punch_card(uptr, image);
            uptr->u5 &= ~CDPSTA_PUNCH;
        }
        uptr->u5 &= ~(URCSTA_WRITE | URCSTA_CMD | CDPSTA_POSMASK);
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &cdp_dev, "unit=%d disconnect\n", u);
    }

    /* Check to see if we have timed out */
    if (uptr->wait != 0) {
        uptr->wait--;
        /* If at end of record and channel is still active, do another read */
        if (
            ((uptr->u5 & (URCSTA_CMD | URCSTA_IDLE | URCSTA_WRITE | URCSTA_ON))
                  == (URCSTA_CMD | URCSTA_IDLE | URCSTA_ON)) && uptr->wait > 30
                  && chan_test(chan, STA_ACTIVE)) {
            uptr->u5 |= URCSTA_WRITE;
            uptr->u5 &= ~URCSTA_IDLE;
            chan_set(chan, DEV_WRITE);
            chan_clear(chan, DEV_WEOR);
            sim_debug(DEBUG_CHAN, &cdp_dev, "unit=%d restarting\n", u);
        }
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    }

    /* If no write request, go to idle mode */
    if ((uptr->u5 & URCSTA_WRITE) == 0) {
        if ((uptr->u5 & (URCSTA_IDLE | URCSTA_ON)) ==
            (URCSTA_IDLE | URCSTA_ON)) {
            uptr->wait = 85;    /* Delay 85ms */
            uptr->u5 &= ~URCSTA_IDLE;   /* Not running */
            sim_activate(uptr, us_to_ticks(1000));
        } else {
            uptr->u5 &= ~URCSTA_ON;     /* Turn motor off */
        }
        return SCPE_OK;
    }

    /* Motor is up to speed now */
    uptr->u5 |= URCSTA_ON;
    uptr->u5 &= ~URCSTA_IDLE;   /* Not running */

    if (dev_pulse[chan] & PUNCH_M)
        uptr->u5 |= CDPSTA_PUNCH;

    pos = (uptr->u5 & CDPSTA_POSMASK) >> CDPSTA_POSSHIFT;
    if (pos == 24) {
        if (chan_test(chan, STA_ACTIVE)) {
            sim_debug(DEBUG_CHAN, &cdp_dev, "unit=%d set EOR\n", u);
            chan_set(chan, DEV_REOR);
        } else {
            chan_clear(chan, DEV_WEOR | DEV_SEL);
            sim_debug(DEBUG_CHAN, &cdp_dev, "unit=%d disconnect\n", u);
        }
        sim_debug(DEBUG_DETAIL, &cdp_dev, "punch card full\n");
        sim_punch_card(uptr, image);
        uptr->u5 |= URCSTA_IDLE;
        uptr->u5 &= ~(URCSTA_WRITE | CDPSTA_POSMASK | CDPSTA_PUNCH);
        uptr->wait = 85;
        sim_activate(uptr, us_to_ticks(1000));
        return SCPE_OK;
    }



    sim_debug(DEBUG_DATA, &cdp_dev, "unit=%d write column %d ", u, pos);
    wd = 0;
    switch (chan_read(chan, &wd, 0)) {
    case DATA_OK:
        sim_debug(DEBUG_DATA, &cdp_dev, " %012llo\n", wd);
        /* Bit flip into temp buffer */
        bit = 1 << (pos / 2);
        mask = 1;
        b = (pos & 1)?36:0;

        for (col = 35; col >= 0; mask <<= 1, col--) {
            if (wd & mask)
                image[col + b] |= bit;
        }
        pos++;
        uptr->wait = 0;
        uptr->u5 &= ~CDPSTA_POSMASK;
        uptr->u5 |= (pos << CDPSTA_POSSHIFT) & CDPSTA_POSMASK;
        sim_activate(uptr, (pos & 1) ? us_to_ticks(300) : us_to_ticks(8000));
        return SCPE_OK;
    case END_RECORD:
        sim_debug(DEBUG_DATA, &cdp_dev, "eor\n");
        uptr->wait = 8 * (12 - (pos / 2)) /*+ 85*/;
        uptr->u5 &= ~(CDPSTA_POSMASK);
        uptr->u5 |= (24 << CDPSTA_POSSHIFT) & CDPSTA_POSMASK;
        break;
    case TIME_ERROR:
        sim_debug(DEBUG_DATA, &cdp_dev, "no data\n");
        chan_set_attn(chan);
        uptr->wait = 8 * (12 - (pos / 2)) /*+ 85*/;
        uptr->u5 &= ~(CDPSTA_POSMASK);
        uptr->u5 |= (24 << CDPSTA_POSSHIFT) & CDPSTA_POSMASK;
        break;
    }

    sim_activate(uptr, us_to_ticks(1000));
    return SCPE_OK;
}

void
cdp_ini(UNIT * uptr, t_bool f)
{
    uptr->u5 = 0;
}

t_stat
cdp_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    sim_switches |= SWMASK ('A');   /* Position to EOF */
    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    if (uptr->up7 == 0) {
        uptr->up7 = calloc(80, sizeof(uint16));
        uptr->u5 = CDPSTA_POSMASK;
    }
    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    if (uptr->up7 != 0)
        free(uptr->up7);
    uptr->up7 = 0;
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   const char *cpu = cpu_description(&cpu_dev);

   fprintf (st, "%s\n\n", cdp_description(dptr));
#if NUM_DEVS_CDP > 3
   fprintf (st, "The %s supports up to four card punches\n", cpu);
#elif NUM_DEVS_CDP > 2
   fprintf (st, "The %s supports up to three card punches\n", cpu);
#elif NUM_DEVS_CDP > 1
   fprintf (st, "The %s supports up to two card punches\n", cpu);
#elif NUM_DEVS_CDP > 0
   fprintf (st, "The %s supports one card punch\n", cpu);
#endif
   help_set_chan_type(st, dptr, "Card punches");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   fprintf (st, "\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
   return "721 Card Punch";
}


#endif

