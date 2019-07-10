/* ka10_cr.c: PDP10 Card reader.

   Copyright (c) 2016-2017, Richard Cornwell

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

#include "kx10_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#if (NUM_DEVS_CR > 0)

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_029

#define CR_DEVNUM        0150


/* std devices. data structures

   cr_dev      Card Reader device descriptor
   cr_unit     Card Reader unit descriptor
   cr_reg      Card Reader register list
   cr_mod      Card Reader modifiers list
*/

/* CONO Bits */
#define PIA             0000007
#define CLR_DRDY        0000010    /* Clear data ready */
#define CLR_END_CARD    0000020    /* Clear end of card */
#define CLR_EOF         0000040    /* Clear end of File Flag */
#define EN_READY        0000100    /* Enable ready irq */
#define CLR_DATA_MISS   0000200    /* Clear data miss */
#define EN_TROUBLE      0000400    /* Enable trouble IRQ */
#define READ_CARD       0001000    /* Read card */
#define OFFSET_CARD     0004000
#define CLR_READER      0010000    /* Clear reader */
/* CONI Bits */
#define DATA_RDY        00000010    /* Data ready */
#define END_CARD        00000020    /* End of card */
#define END_FILE        00000040    /* End of file */
#define RDY_READ        00000100    /* Ready to read */
#define DATA_MISS       00000200    /* Data missed */
#define TROUBLE         00000400    /* Trouble */
#define READING         00001000    /* Reading card */
#define HOPPER_EMPTY    00002000
#define CARD_IN_READ    00004000    /* Card in reader */
#define STOP            00010000
#define MOTION_ERROR    00020000
#define CELL_ERROR      00040000
#define PICK_ERROR      00100000
#define RDY_READ_EN     00200000
#define TROUBLE_EN      00400000

#define STATUS      u3
#define COL         u4
#define DATA        u5

#define CARD_RDY(u)       (sim_card_input_hopper_count(u) > 0 || \
                           sim_card_eof(u) == 1)

t_stat              cr_devio(uint32 dev, uint64 *data);
t_stat              cr_srv(UNIT *);
t_stat              cr_reset(DEVICE *);
t_stat              cr_attach(UNIT *, CONST char *);
t_stat              cr_detach(UNIT *);
t_stat              cr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cr_description(DEVICE *dptr);
uint16              cr_buffer[80];

DIB cr_dib = { CR_DEVNUM, 1, cr_devio, NULL};

UNIT                cr_unit = {
   UDATA(cr_srv, UNIT_CDR, 0), 300,
};

MTAB                cr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {0}
};

REG                 cr_reg[] = {
    {BRDATA(BUFF, cr_buffer, 16, 16, sizeof(cr_buffer)), REG_HRO},
    {0}
};


DEVICE              cr_dev = {
    "CR", &cr_unit, cr_reg, cr_mod,
    NUM_DEVS_CR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cr_attach, &cr_detach,
    &cr_dib, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug,
    NULL, NULL, &cr_help, NULL, NULL, &cr_description
};


/*
 * Device entry points for card reader.
 */
t_stat cr_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &cr_unit;
    switch(dev & 3) {
    case CONI:
         if (uptr->flags & UNIT_ATT &&
             (uptr->STATUS & (TROUBLE|READING|CARD_IN_READ|END_CARD)) == 0 &&
             CARD_RDY(uptr)) {
                uptr->STATUS |= RDY_READ;
         }
         *data = uptr->STATUS;
         sim_debug(DEBUG_CONI, &cr_dev, "CR: CONI %012llo\n", *data);
         break;

    case CONO:
         clr_interrupt(dev);
         sim_debug(DEBUG_CONO, &cr_dev, "CR: CONO %012llo\n", *data);
         if (*data & CLR_READER) {
             uptr->STATUS = 0;
             if (!CARD_RDY(uptr))
                 uptr->STATUS |= END_FILE;
            sim_cancel(uptr);
            break;
         }
         uptr->STATUS &= ~(PIA);
         uptr->STATUS |= *data & PIA;
         uptr->STATUS &= ~(*data & (CLR_DRDY|CLR_END_CARD|CLR_EOF|CLR_DATA_MISS));
         if (*data & EN_TROUBLE)
             uptr->STATUS |= TROUBLE_EN;
         if (*data & EN_READY)
             uptr->STATUS |= RDY_READ_EN;
         if (uptr->flags & UNIT_ATT && *data & READ_CARD) {
             uptr->STATUS |= READING;
             uptr->STATUS &= ~(CARD_IN_READ|RDY_READ|DATA_RDY);
             uptr->COL = 0;
             sim_activate(uptr, uptr->wait);
             break;
         }
         if (CARD_RDY(uptr))
             uptr->STATUS |= RDY_READ;
         else
             uptr->STATUS |= STOP;
         if (uptr->STATUS & RDY_READ_EN && uptr->STATUS & RDY_READ)
             set_interrupt(dev, uptr->STATUS);
         if (uptr->STATUS & TROUBLE_EN && uptr->STATUS & TROUBLE)
             set_interrupt(dev, uptr->STATUS);
         break;

    case DATAI:
         clr_interrupt(dev);
         if (uptr->STATUS & DATA_RDY) {
             *data = uptr->DATA;
             sim_debug(DEBUG_DATAIO, &cr_dev, "CR: DATAI %012llo\n", *data);
             uptr->STATUS &= ~DATA_RDY;
         } else
             *data = 0;
         break;
    case DATAO:
         break;
    }
    return SCPE_OK;
}

/* Handle transfer of data for card reader */
t_stat
cr_srv(UNIT *uptr) {

    /* Read in card, ready to read next one set IRQ */
    if (uptr->flags & UNIT_ATT /*&& uptr->STATUS & END_CARD*/) {
        if ((uptr->STATUS & (READING|CARD_IN_READ|RDY_READ)) == 0 &&
            (CARD_RDY(uptr))) {
            sim_debug(DEBUG_EXP, &cr_dev, "CR: card ready %d\n",
                   sim_card_input_hopper_count(uptr));
            uptr->STATUS |= RDY_READ;    /* INdicate ready to start reading */
            if (uptr->STATUS & RDY_READ_EN)
                set_interrupt(CR_DEVNUM, uptr->STATUS);
            return SCPE_OK;
        }
    }

    /* Check if new card requested. */
    if ((uptr->STATUS & (READING|CARD_IN_READ)) == READING) {
        uptr->STATUS &= ~(END_CARD|RDY_READ);
        switch(sim_read_card(uptr, cr_buffer)) {
        case CDSE_EOF:
             sim_debug(DEBUG_EXP, &cr_dev, "CR: card eof\n");
             uptr->STATUS &= ~(CARD_IN_READ|READING);
             uptr->STATUS |= END_FILE;
             if (sim_card_input_hopper_count(uptr) != 0)
                 sim_activate(uptr, uptr->wait);
             set_interrupt(CR_DEVNUM, uptr->STATUS);
             return SCPE_OK;
        case CDSE_EMPTY:
         sim_debug(DEBUG_EXP, &cr_dev, "CR: card empty\n");
             uptr->STATUS &= ~(CARD_IN_READ|READING);
             uptr->STATUS |= HOPPER_EMPTY|TROUBLE|STOP;
             if (uptr->STATUS & TROUBLE_EN)
                 set_interrupt(CR_DEVNUM, uptr->STATUS);
             return SCPE_OK;
        case CDSE_ERROR:
             sim_debug(DEBUG_EXP, &cr_dev, "CR: card error\n");
             uptr->STATUS &= ~(CARD_IN_READ|READING);
             uptr->STATUS |= TROUBLE|PICK_ERROR|STOP;
             if (uptr->STATUS & TROUBLE_EN)
                 set_interrupt(CR_DEVNUM, uptr->STATUS);
             return SCPE_OK;
        case CDSE_OK:
         sim_debug(DEBUG_EXP, &cr_dev, "CR: card ok\n");
             uptr->STATUS |= CARD_IN_READ;
             break;
        }
        uptr->COL = 0;
        sim_activate(uptr, uptr->wait);
        return SCPE_OK;
    }

    /* Copy next column over */
    if (uptr->STATUS & CARD_IN_READ) {
        if (uptr->COL >= 80) {
             uptr->STATUS &= ~(CARD_IN_READ|READING);
             uptr->STATUS |= END_CARD;
             set_interrupt(CR_DEVNUM, uptr->STATUS);
             sim_activate(uptr, uptr->wait);
             return SCPE_OK;
        }
        uptr->DATA = cr_buffer[uptr->COL++];
        if (uptr->STATUS & DATA_RDY) {
            uptr->STATUS |= DATA_MISS;
        }
        uptr->STATUS |= DATA_RDY;
        sim_debug(DEBUG_DATA, &cr_dev, "CR Char > %d %03x\n", uptr->COL, uptr->DATA);
        set_interrupt(CR_DEVNUM, uptr->STATUS);
        sim_activate(uptr, uptr->wait);
    }
    return SCPE_OK;
}

t_stat
cr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    if ((uptr->STATUS & (READING|CARD_IN_READ)) == 0) {
        uptr->STATUS |= RDY_READ;
        uptr->STATUS &= ~(HOPPER_EMPTY|STOP|TROUBLE|CELL_ERROR|PICK_ERROR);
        if (uptr->STATUS & RDY_READ_EN)
            set_interrupt(CR_DEVNUM, uptr->STATUS);
    }
    return SCPE_OK;
}

t_stat
cr_detach(UNIT * uptr)
{
    if (uptr->flags & UNIT_ATT) {
        uptr->STATUS |= TROUBLE|HOPPER_EMPTY;
        if (uptr->STATUS & TROUBLE_EN)
            set_interrupt(CR_DEVNUM, uptr->STATUS);
    }
    uptr->STATUS &= ~ RDY_READ;
    return sim_card_detach(uptr);
}

t_stat
cr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Reader\n\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cr_description(DEVICE *dptr)
{
   return "Card Reader";
}

#endif

