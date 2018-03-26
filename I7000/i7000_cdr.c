/* i7000_cdr.c: IBM 7000 Card reader.

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

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_CDR

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_026

/* Flags for punch and reader. */
#define ATTENA          (1 << (UNIT_V_UF+7))
#define ATTENB          (1 << (UNIT_V_UF+14))


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

uint32              cdr_cmd(UNIT *, uint16, uint16);
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
extern t_stat       chan_boot(int32, DEVICE *);
#ifdef I7070
t_stat              cdr_setload(UNIT *, int32, CONST char *, void *);
t_stat              cdr_getload(FILE *, UNIT *, int32, CONST void *);
#endif
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *dptr);

UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDR, 0), 300},       /* A */
#if NUM_DEVS_CDR > 1
   {UDATA(cdr_srv, UNIT_S_CHAN(CHAN_CHUREC+1) | UNIT_CDR, 0), 300},     /* B */
#endif
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL, "Set card format"},
#ifdef I7070
    {ATTENA|ATTENB, 0, NULL, "NOATTEN", NULL, NULL, NULL, "No attention signal"},
    {ATTENA|ATTENB, ATTENA, "ATTENA", "ATTENA", NULL, NULL, NULL, "Signal Attention A"},
    {ATTENA|ATTENB, ATTENB, "ATTENB", "ATTENB", NULL, NULL, NULL, "Signal Attention B"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "LCOL", "LCOL", &cdr_setload,
        &cdr_getload, NULL, "Load card column indicator"},
#endif
#ifdef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL, "Set device channel"},
#endif
    {0}
};

DEVICE              cdr_dev = {
    "CDR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &cdr_boot, &cdr_attach, &sim_card_detach,
    &cdr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
};


/*
 * Device entry points for card reader.
 */
uint32 cdr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdr_unit);
    int                 stk = dev & 017;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_READ)
        return SCPE_BUSY;

    /* Test ready */
    if (cmd == IO_TRS && uptr->flags & UNIT_ATT) {
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Test Rdy\n", u);
        return SCPE_OK;
    }

    if (stk == 10)
        stk = 0;

#ifdef STACK_DEV
    uptr->u5 &= ~0xF0000;
    uptr->u5 |= stk << 16;
#endif
    if (uptr->u5 & (URCSTA_EOF|URCSTA_ERR))
        return SCPE_IOERR;

    /* Process commands */
    switch(cmd) {
    case IO_RDS:
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Cmd RDS %02o\n", u, dev & 077);
#ifdef I7010
        if (stk!= 9)
#endif
        uptr->u5 &= ~(URCSTA_CARD|URCSTA_ERR);
        break;
    case IO_CTL:
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Cmd CTL %02o\n", u, dev & 077);
#ifdef I7010
        uptr->u5 |= URCSTA_NOXFER;
#endif
        break;
    default:
        chan_set_attn(chan);
        return SCPE_IOERR;
    }

    /* If at eof, just return EOF */
    if (uptr->u5 & URCSTA_EOF) {
        chan_set_eof(chan);
        chan_set_attn(chan);
        return SCPE_OK;
    }

    uptr->u5 |= URCSTA_READ;
    uptr->u4 = 0;

    if ((uptr->u5 & URCSTA_NOXFER) == 0)
        chan_set_sel(chan, 0);
    /* Wake it up if not busy */
    if ((uptr->u5 & URCSTA_BUSY) == 0)
        sim_activate(uptr, 50);
    return SCPE_OK;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdr_unit);
    struct _card_data   *data;

    data = (struct _card_data *)uptr->up7;

    /* Waiting for disconnect */
    if (uptr->u5 & URCSTA_WDISCO) {
        if (chan_stat(chan, DEV_DISCO)) {
            chan_clear(chan, DEV_SEL|DEV_WEOR);
            uptr->u5 &= ~ URCSTA_WDISCO;
        } else {
            /* No disco yet, try again in a bit */
            sim_activate(uptr, 50);
            return SCPE_OK;
        }
        /* If still busy, schedule another wait */
        if (uptr->u5 & URCSTA_BUSY)
             sim_activate(uptr, uptr->wait);
    }

    if (uptr->u5 & URCSTA_BUSY) {
        uptr->u5 &= ~URCSTA_BUSY;
#ifdef I7070
        switch(uptr->flags & (ATTENA|ATTENB)) {
        case ATTENA: chan_set_attn_a(chan); break;
        case ATTENB: chan_set_attn_b(chan); break;
        }
#endif
    }

    /* Check if new card requested. */
    if (uptr->u4 == 0 && uptr->u5 & URCSTA_READ &&
                (uptr->u5 & URCSTA_CARD) == 0) {
        switch(sim_read_card(uptr)) {
        case SCPE_EOF:
             sim_debug(DEBUG_DETAIL, &cdr_dev, "%d: EOF\n", u);
             /* Fall through */

        case SCPE_UNATT:
             chan_set_eof(chan);
             chan_set_attn(chan);
             chan_clear(chan, DEV_SEL);
             uptr->u5 |= URCSTA_EOF;
             uptr->u5 &= ~(URCSTA_BUSY|URCSTA_READ);
             return SCPE_OK;
        case SCPE_IOERR:
             sim_debug(DEBUG_DETAIL, &cdr_dev, "%d: ERF\n", u);
             uptr->u5 |= URCSTA_ERR;
             uptr->u5 &= ~(URCSTA_BUSY|URCSTA_READ);
             chan_set_attn(chan);
             chan_clear(chan, DEV_SEL);
             return SCPE_OK;
        case SCPE_OK:
             uptr->u5 |= URCSTA_CARD;
#ifdef I7010
             chan_set_attn_urec(chan, cdr_dib.addr);
#endif
             break;
        }
#ifdef I7070
        /* Check if load card. */
        if (uptr->capac && (data->image[uptr->capac-1] & 0x800)) {
             uptr->u5 |= URCSTA_LOAD;
             chan_set_load_mode(chan);
        } else {
             uptr->u5 &= ~URCSTA_LOAD;
        }
#endif
    }

    if (uptr->u5 & URCSTA_NOXFER) {
        uptr->u5 &= ~(URCSTA_NOXFER|URCSTA_READ);
        return SCPE_OK;
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_READ && uptr->u4 < 80) {
        uint8                ch = 0;

#ifdef I7080
        /* Detect RSU */
        if (data->image[uptr->u4] == 0x924) {
             uptr->u5 &= ~URCSTA_READ;
             uptr->u5 |= URCSTA_WDISCO;
             chan_set(chan, DEV_REOR);
             sim_activate(uptr, 10);
             return SCPE_OK;
        }
#endif

        ch = sim_hol_to_bcd(data->image[uptr->u4]);

        /* Handle invalid punch */
        if (ch == 0x7f) {
#ifdef I7080
             uptr->u5 &= ~(URCSTA_READ|URCSTA_BUSY);
             sim_debug(DEBUG_DETAIL, &cdr_dev, "%d: bad punch %d\n", u,
                       uptr->u4);
             chan_set_attn(chan);
             chan_set_error(chan);
             chan_clear(chan, DEV_SEL);
#else
             uptr->u5 |= URCSTA_ERR;
             ch = 017;
#endif
        }

#ifdef I7070
        /* During load, only sign on every 10 columns */
        if (uptr->u5 & URCSTA_LOAD && (uptr->u4 % 10) != 9)
            ch &= 0xf;
#endif

        switch(chan_write_char(chan, &ch, (uptr->u4 == 79)? DEV_REOR: 0)) {
        case TIME_ERROR:
        case END_RECORD:
            uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY;
            uptr->u5 &= ~URCSTA_READ;
            break;
        case DATA_OK:
            uptr->u4++;
            break;
        }
        sim_debug(DEBUG_DATA, &cdr_dev, "%d: Char > %02o\n", u, ch);
        sim_activate(uptr, 10);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_stat              r;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    /* Read in one record */
    r = cdr_cmd(uptr, IO_RDS, cdr_dib.addr);
    if (r != SCPE_OK)
        return r;
    r = chan_boot(unit_num, dptr);
    return r;
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 &= URCSTA_BUSY|URCSTA_WDISCO;
    uptr->u4 = 0;
    uptr->u6 = 0;
#ifdef I7010
    chan_set_attn_urec(UNIT_G_CHAN(uptr->flags), cdr_dib.addr);
#endif
    return SCPE_OK;
}
#ifdef I7070
t_stat
cdr_setload(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int i;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = 0;
    while(*cptr != '\0') {
        if (*cptr < '0' || *cptr > '9')
           return SCPE_ARG;
        i = (i * 10) + (*cptr++) - '0';
    }
    if (i > 80)
        return SCPE_ARG;
    uptr->capac = i;
    return SCPE_OK;
}

t_stat
cdr_getload(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "loadcolumn=%d", uptr->capac);
    return SCPE_OK;
}
#endif

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", cdr_description(dptr));
#if NUM_DEVS_CDR > 1
   fprintf (st, "The system supports up to two card readers.\n");
#else
   fprintf (st, "The system supports one card reader.\n");
#endif
#ifdef I7070
   fprintf (st, "Unit record devices can be configured to interrupt the CPU on\n");
   fprintf (st, "one of two priority channels A or B, to set this\n\n");
   fprintf (st, "   sim> SET %s ATTENA     To set device to raise Atten A\n\n", dptr->name);
   fprintf (st, "The 7500 Card reader supported a load mode, this was\n");
   fprintf (st, "selected by use of a 12 punch in a given column. When this\n");
   fprintf (st, "was seen the card was read into 8 words. Normal read is\n");
   fprintf (st, "text only\n\n");
   fprintf (st, "   sim> SET %s LCOL=72    Sets column to select load mode\n\n", dptr->name);
#endif
#if NUM_DEVS_CDR > 1
#ifdef I7010
   help_set_chan_type(st, dptr, "Card reader");
#endif
#endif
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
#ifdef I7010
   return "1402 Card Reader";
#endif
#ifdef I7070
   return "7500 Card Reader";
#endif
#ifdef I7080
   return "711 Card Reader";
#endif
}

#endif

