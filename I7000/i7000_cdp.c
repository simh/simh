/* i7000_cdp.c: IBM 7000 Card Punch

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

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_CDP

#define UNIT_CDP        UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ | MODE_026


/* Flags for punch and reader. */
#define ATTENA          (1 << (UNIT_V_UF+7))
#define ATTENB          (1 << (UNIT_V_UF+14))
#define INPUT_V         (UNIT_V_UF+7)
#define INPUT_FULL      (1 << INPUT_V)
#define INPUT_EMPTY     (2 << INPUT_V)
#define INPUT_DECK      (3 << INPUT_V)
#define INPUT_BLANK     (4 << INPUT_V)
#define INPUT_MASK      (7 << INPUT_V)


/* std devices. data structures

   cdp_dev      Card Punch device descriptor
   cdp_unit     Card Punch unit descriptor
   cdp_reg      Card Punch register list
   cdp_mod      Card Punch modifiers list
*/

uint32              cdp_cmd(UNIT *, uint16, uint16);
void                cdp_ini(UNIT *, t_bool);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_get_input(FILE *, UNIT *, int32, CONST void *);
t_stat              cdp_set_input(UNIT *, int32, CONST char *, void *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *dptr);
t_stat              stk_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *stk_description(DEVICE *dptr);

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDP, 0), 600},      /* A */
#if NUM_DEVS_CDP > 1
    {UDATA(cdp_srv, UNIT_S_CHAN(CHAN_CHUREC+1) | UNIT_CDP, 0), 600},    /* B */
#endif
};

UNIT                cdp_input_unit[] = {
    {UDATA(NULL, UNIT_ATTABLE | INPUT_FULL | UNIT_RO, 0), 600},      /* A */
#if NUM_DEVS_CDP > 1
    {UDATA(NULL, UNIT_ATTABLE | INPUT_FULL | UNIT_RO, 0), 600},    /* B */
#endif
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL, "Set card format"},
#ifdef I7070
    {ATTENA|ATTENB,0, NULL, "NOATTEN", NULL, NULL, NULL, "No attention signal"},
    {ATTENA|ATTENB,ATTENA, "ATTENA", "ATTENA", NULL, NULL, NULL, "Signal Attention A"},
    {ATTENA|ATTENB,ATTENB, "ATTENB", "ATTENB", NULL, NULL, NULL, "Signal Attention B"},
#endif
#ifdef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL, "Set device channel"},
#endif
    {MTAB_XTD | MTAB_VUN | MTAB_VALR | MTAB_NC, 0, "INPUT", "INPUT", &cdp_set_input, &cdp_get_input,
              NULL, "Set input to card punch"},
    {0}
};

DEVICE              cdp_dev = {
    "CDP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, &cdp_reset, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};

DEVICE              cdp_input_dev = {
    "INPUT", cdp_input_unit, NULL, NULL,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

#ifdef STACK_DEV
UNIT stack_unit[] = {
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    };

DEVICE stack_dev = {
    "STKR", stack_unit, NULL, NULL,
    NUM_DEVS_CDP * 10, 10, 31, 1, 8, 7,
    NULL, NULL, NULL, NULL, &sim_card_attach, &sim_card_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &stk_help, NULL, NULL, &stk_description
    };
#endif



/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/


uint32 cdp_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    int                 stk = dev & 017;
    UNIT               *iuptr = &cdp_input_unit[u];
    uint16             *image = (uint16 *)(uptr->up7);
    int                 i;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_WRITE) {
        sim_debug(DEBUG_DETAIL, &cdp_dev, "%d: Busy\n", u);
        return SCPE_BUSY;
    }

    if (stk == 10)
        stk = 0;
    if ((uptr->flags & UNIT_ATT) == 0) {
#ifdef STACK_DEV
        if ((stack_unit[stk * u].flags & UNIT_ATT) == 0)
#endif
        return SCPE_IOERR;
    }

    switch(cmd) {
    /* Test ready */
    case IO_TRS:
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd TRS\n", u);
        return SCPE_OK;

    /* Suppress punch */
    case IO_RUN:
        uptr->u5 &= ~URCSTA_FULL;
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd RUN\n", u);
        return SCPE_OK;

    /* Retieve data from CPU */
    case IO_WRS:
#ifdef STACK_DEV
        uptr->u5 &= ~0xF0000;
        uptr->u5 |= stk << 16;
#endif
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd WRS\n", u);

        switch((iuptr->flags & INPUT_MASK) >> INPUT_V) {
        case INPUT_EMPTY >> INPUT_V:
        case INPUT_BLANK >> INPUT_V:
            if (iuptr->u3 == 0) {
                sim_debug(DEBUG_DETAIL, &cdp_dev, "%d: Empty\n", u);
                return SCPE_IOERR;
            }
            iuptr->u3--;
            /* Fall through */

        case INPUT_FULL >> INPUT_V:
            for (i = 0; i < 80; image[i++] = 0);
            break;
        case INPUT_DECK >> INPUT_V:
            switch(sim_read_card(iuptr, image)) {
            case CDSE_ERROR:
                 uptr->u5 |= URCSTA_ERR;
                 /* Fall through */

            case CDSE_EOF:
            case CDSE_EMPTY:
                 sim_debug(DEBUG_DETAIL, &cdp_dev, "%d: Empty deck\n", u);
                 return SCPE_IOERR;
            case CDSE_OK:
                 sim_debug(DEBUG_DETAIL, &cdp_dev, "%d: left %d\n", u,
                         sim_card_input_hopper_count(iuptr));
                 break;
            }
            break;
        }
        chan_set_sel(chan, 1);
        uptr->u5 |= URCSTA_WRITE;
        uptr->u4 = 0;
        if ((uptr->u5 & URCSTA_BUSY) == 0)
            sim_activate(uptr, 50);
        return SCPE_OK;
    }
    return SCPE_IOERR;
}

/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    uint16              *image = (uint16 *)(uptr->up7);

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
        /* Done waiting, punch card */
        if (uptr->u5 & URCSTA_FULL) {
#ifdef STACK_DEV
              UNIT   *sptr = &stack_unit[(u * 10) + ((uptr->u5 >> 16) & 0xf)];
              if ((uptr->flags & UNIT_ATT) != 0 || (sptr->flags & UNIT_ATT) == 0)
                  sptr = uptr;
              switch(sim_punch_card(sptr, image)) {
#else
              switch(sim_punch_card(uptr, image)) {
#endif
              case CDSE_EOF:
              case CDSE_EMPTY:
                  chan_set_eof(chan);
                  break;
                 /* If we get here, something is wrong */
              case CDSE_ERROR:
                  chan_set_error(chan);
                  break;
              case CDSE_OK:
                  break;
              }
              uptr->u5 &= ~URCSTA_FULL;
        }
        uptr->u5 &= ~URCSTA_BUSY;
#ifdef I7070
        switch(uptr->flags & (ATTENA|ATTENB)) {
        case ATTENA: chan_set_attn_a(chan); break;
        case ATTENB: chan_set_attn_b(chan); break;
        }
#endif
#ifdef I7010
        chan_set_attn_urec(chan, cdp_dib.addr);
#endif
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_WRITE && uptr->u4 < 80) {
        uint8               ch = 0;

        switch(chan_read_char(chan, &ch, 0)) {
        case TIME_ERROR:
        case END_RECORD:
             uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY|URCSTA_FULL;
             uptr->u5 &= ~URCSTA_WRITE;
             break;
        case DATA_OK:
            if (ch == 0)
               ch = 020;
            else if (ch == 020)
               ch = 0;
            image[uptr->u4] |= sim_bcd_to_hol(ch);
            if (sim_hol_to_bcd(image[uptr->u4]) == 0x7f) {
                chan_set_eof(chan);
            }
            sim_debug(DEBUG_DATA, &cdp_dev, "%d: Char < %02o %04o\n", u, ch, image[uptr->u4]);
            if (++uptr->u4 == 80) {
                chan_set(chan, DEV_REOR);
                uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY|URCSTA_FULL;
                uptr->u5 &= ~URCSTA_WRITE;
            }
            break;
        }
        sim_activate(uptr, 10);
    }
    return SCPE_OK;
}

t_stat
cdp_reset(DEVICE *dptr) {
    return sim_register_internal_device (&cdp_input_dev);
}

void
cdp_ini(UNIT *uptr, t_bool f) {
}

t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat        r;

    sim_switches |= SWMASK ('A');   /* Position to EOF */
    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    if (uptr->up7 == 0) {
        uptr->up7 = calloc(80, sizeof(uint16));
        uptr->u5 = 0;
    }
    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    uint16        *image = (uint16 *)(uptr->up7);

    if (uptr->u5 & URCSTA_FULL) {
#ifdef STACK_DEV
        UNIT   *sptr = &stack_unit[((uptr - cdp_unit) * 10) + ((uptr->u5 >> 16) & 0xf)];
        if ((uptr->flags & UNIT_ATT) != 0 || (sptr->flags & UNIT_ATT) == 0)
            sptr = uptr;
        sim_punch_card(sptr, image);
#else
        sim_punch_card(uptr, image);
#endif
    }
    if (uptr->up7 == 0)
        free(uptr->up7);
    uptr->up7 = 0;
    return sim_card_detach(uptr);
}

t_stat
cdp_set_input(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int       nflag = 1;
    int       num = 0;
    char      gbuf[30];
    char     *p;
    int       u = (uptr - cdp_unit);
    UNIT     *iuptr = &cdp_input_unit[u];
    t_stat    r = SCPE_ARG;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;

    /* Clear existing input */
    sim_card_detach(iuptr);
    iuptr->u3 = 0;
    iuptr->flags &= ~INPUT_MASK;
    iuptr->flags |= INPUT_EMPTY;

    /* Get first argument */
    cptr = get_glyph(cptr, gbuf, ';');

    /* Check if it is a number */
    for (p = gbuf; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            nflag = 0;
            break;
        }
        num = (num * 10) + (*p) - '0';
    }

    /* If valid number, set to number of blank cards */
    if (nflag) {
       iuptr->u3 = num;
       iuptr->flags &= ~INPUT_MASK;
       iuptr->flags |= INPUT_BLANK;
       return SCPE_OK;
    }

    /* Check for given format */
    if (sim_strcasecmp(gbuf, "EMPTY") == 0) {
       iuptr->u3 = 0;
       iuptr->flags &= ~INPUT_MASK;
       iuptr->flags |= INPUT_EMPTY;
       return SCPE_OK;
    }

    if (sim_strcasecmp(gbuf, "FULL") == 0) {
       iuptr->u3 = 0;
       iuptr->flags &= ~INPUT_MASK;
       iuptr->flags |= INPUT_FULL;
       return SCPE_OK;
    }

    /* If deck attach it to input */
    if (sim_strcasecmp(gbuf, "DECK") == 0) {
       int32     saved_switches = sim_switches;

       sim_switches = SWMASK('E') | SWMASK('R');
       if ((saved_switches & SWMASK('F')) != 0) {
           cptr = get_glyph(cptr, gbuf, ';');
           sim_card_set_fmt(iuptr, 0, gbuf, NULL);
       }
       r = sim_card_attach(iuptr, cptr);
       if (r == SCPE_OK) {
           iuptr->flags &= ~INPUT_MASK;
           iuptr->flags |= INPUT_DECK;
       }
       sim_switches = saved_switches;
    }
    /* Error, set to empty */
    return r;
}

t_stat
cdp_get_input(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    int u = (uptr - cdp_unit);
    UNIT *iuptr = &cdp_input_unit[u];
    int i;

    if (uptr == NULL)
        return SCPE_IERR;

    i = (iuptr->flags & INPUT_MASK) >> INPUT_V;
    switch((iuptr->flags & INPUT_MASK) >> INPUT_V) {
    case INPUT_BLANK >> INPUT_V:
        fprintf(st, "%d blanks", iuptr->u3);
        break;
    case INPUT_FULL >> INPUT_V:
        fprintf(st, "full");
        break;
    case INPUT_EMPTY >> INPUT_V:
        fprintf(st, "empty");
        break;
    case INPUT_DECK >> INPUT_V:
        fprintf(st, "deck %s", iuptr->filename);
        break;
    }
    return SCPE_OK;
}



#ifdef STACK_DEV
t_stat
stk_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", stk_description(dptr));
   fprintf (st, "Allows stack control functions to direct cards to specific ");
   fprintf (st, "bins based on stacker\nselection. Attach cards here if you ");
   fprintf (st, "wish this specific stacker select to recieve\nthis group of");
   fprintf (st, " cards. If nothing is attached cards will be punched on the");
   fprintf (st, " default\npunch\n\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
stk_description(DEVICE *dptr)
{
   return "Card stacking device";
}
#endif

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", cdp_description(dptr));
#ifdef STACK_DEV
   fprintf (st, "If the punch device is not attached and instead the %s ", stack_dev.name);
   fprintf (st, "device is attached,\nthe cards will be sent out to the ");
   fprintf (st, "given stacker based on the flag set by\nthe processor.\n\n");
#endif
#ifdef I7070
   fprintf (st, "Unit record devices can be configured to interrupt the CPU on\n");
   fprintf (st, "one of two priority channels A or B, to set this\n\n");
   fprintf (st, "   sim> SET %s ATTENA     to set device to raise Atten A\n\n", dptr->name);
#endif
#ifdef I7010
   help_set_chan_type(st, dptr, "Card punches");
#endif
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
#ifdef I7010
   return "1402 Card Punch";
#endif
#ifdef I7070
   return "7550 Card Punch";
#endif
#ifdef I7080
   return "721 Card Punch";
#endif
}

#endif

