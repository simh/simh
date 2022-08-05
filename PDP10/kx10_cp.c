/* kx10_cp.c: PDP10 Card Punch

   Copyright (c) 2016-2020, Richard Cornwell

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

#include "kx10_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#if (NUM_DEVS_CP > 0)

#define UNIT_CDP        UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ | MODE_DEC29 

#define CP_DEVNUM        0110

#if KL
#define CP_DIS DEV_DIS
#endif

#ifndef CP_DIS
#define CP_DIS  0
#endif


/* std devices. data structures

   cp_dev      Card Punch device descriptor
   cp_unit     Card Punch unit descriptor
   cp_reg      Card Punch register list
   cp_mod      Card Punch modifiers list
*/

/* CONO Bits */
#define SET_DATA_REQ    0000010
#define CLR_DATA_REQ    0000020
#define SET_PUNCH_ON    0000040
#define CLR_END_CARD    0000100
#define EN_END_CARD     0000200
#define DIS_END_CARD    0000400
#define CLR_ERROR       0001000
#define EN_TROUBLE      0002000
#define DIS_TROUBLE     0004000
#define EJECT           0010000     /* Finish punch and eject */
#define OFFSET_CARD     0040000     /* Offset card stack */
#define CLR_PUNCH       0100000     /* Clear Trouble, Error, End */

/* CONI Bits */
#define PIA             0000007
#define DATA_REQ        0000010
#define PUNCH_ON        0000040
#define END_CARD        0000100    /* Eject or column 80 */
#define END_CARD_EN     0000200
#define CARD_IN_PUNCH   0000400    /* Card ready to punch */
#define ERROR           0001000    /* Punch error */
#define TROUBLE_EN      0002000
#define TROUBLE         0004000    /* Bit 18,22,23, or 21 */
#define EJECT_FAIL      0010000    /* Could not eject card 23 */
#define PICK_FAIL       0020000    /* Could not pick up card 22 */
#define NEED_OPR        0040000    /* Hopper empty, chip full 21 */
#define HOPPER_LOW      0100000    /* less 200 cards 20 */
#define TEST            0400000    /* In test mode 18 */

#define STATUS      u3
#define COL         u4

t_stat              cp_devio(uint32 dev, uint64 *data);
t_stat              cp_srv(UNIT *);
t_stat              cp_reset(DEVICE *);
t_stat              cp_attach(UNIT *, CONST char *);
t_stat              cp_detach(UNIT *);
t_stat              cp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cp_description(DEVICE *dptr);
uint16              cp_buffer[80];


DIB cp_dib = { CP_DEVNUM, 1, cp_devio, NULL};

UNIT                cp_unit = {UDATA(cp_srv, UNIT_CDP, 0), 2000 };

MTAB                cp_mod[] = {
    { MODE_CHAR, MODE_026, "IBM026", "IBM026", NULL, NULL, NULL,
              "IBM 026 punch encoding"},
    { MODE_CHAR, MODE_029, "IBM029", "IBM029", NULL, NULL, NULL,
              "IBM 029 punch encoding"},
    { MODE_CHAR, MODE_DEC29, "DEC029", "DEC029", NULL, NULL, NULL,
              "DEC 029 punch encoding"},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {0}
};

REG                 cp_reg[] = {
    {BRDATA(BUFF, cp_buffer, 16, 16, sizeof(cp_buffer)/sizeof(uint16)), REG_HRO},
    {0}
};

DEVICE              cp_dev = {
    "CP", &cp_unit, cp_reg, cp_mod,
    NUM_DEVS_CP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cp_attach, &cp_detach,
    &cp_dib, DEV_DISABLE | DEV_DEBUG | DEV_CARD | CP_DIS, 0, crd_debug,
    NULL, NULL, &cp_help, NULL, NULL, &cp_description
};




/* Card punch routine
*/

t_stat cp_devio(uint32 dev, uint64 *data) {
     UNIT                *uptr = &cp_unit;
     uint16              col;

     switch(dev & 3) {
     case CONI:
        *data = uptr->STATUS;
         sim_debug(DEBUG_CONI, &cp_dev, "CP: CONI %012llo\n", *data);
        break;
     case CONO:
         clr_interrupt(dev);
         sim_debug(DEBUG_CONO, &cp_dev, "CP: CONO %012llo\n", *data);
         uptr->STATUS &= ~PIA;
         uptr->STATUS |= *data & PIA;
         if (*data & CLR_PUNCH) {
             uptr->STATUS &= ~(TROUBLE|ERROR|END_CARD|END_CARD_EN|TROUBLE_EN);
             break;
         }
         if (*data & SET_DATA_REQ) {
             uptr->STATUS |= DATA_REQ;
             set_interrupt(dev, uptr->STATUS);
         }
         if (*data & CLR_DATA_REQ)
             uptr->STATUS &= ~DATA_REQ;
         if (*data & CLR_END_CARD)
             uptr->STATUS &= ~END_CARD;
         if (*data & EN_END_CARD)
             uptr->STATUS |= END_CARD_EN;
         if (*data & DIS_END_CARD)
             uptr->STATUS &= ~END_CARD_EN;
         if (*data & EN_TROUBLE)
             uptr->STATUS |= TROUBLE_EN;
         if (*data & DIS_TROUBLE)
             uptr->STATUS &= ~TROUBLE_EN;
         if (*data & EJECT && uptr->STATUS & CARD_IN_PUNCH) {
             uptr->COL = 80;
             uptr->STATUS &= ~DATA_REQ;
             sim_activate(uptr, uptr->wait);
         }
         if ((uptr->STATUS & (TROUBLE|TROUBLE_EN)) == (TROUBLE|TROUBLE_EN))
             set_interrupt(CP_DEVNUM, uptr->STATUS);
         if ((uptr->STATUS & (END_CARD|END_CARD_EN)) == (END_CARD|END_CARD_EN))
             set_interrupt(CP_DEVNUM, uptr->STATUS);
         if (*data & PUNCH_ON) {
             uptr->STATUS |= PUNCH_ON;
             sim_activate(uptr, uptr->wait);
         }
         break;
     case DATAI:
         *data = 0;
         break;
    case DATAO:
         col = *data & 0xfff;
         cp_buffer[uptr->COL++] = col;
         uptr->STATUS &= ~DATA_REQ;
         clr_interrupt(dev);
         sim_debug(DEBUG_DATAIO, &cp_dev, "CP: DATAO %012llo %d\n", *data,
                 uptr->COL);
         sim_activate(uptr, uptr->wait);
         break;
    }
    return SCPE_OK;
}

/* Handle transfer of data for card punch */
t_stat
cp_srv(UNIT *uptr) {

    if (uptr->STATUS & PUNCH_ON) {

       uptr->STATUS |= CARD_IN_PUNCH;
       if (uptr->STATUS & DATA_REQ) {
           sim_activate(uptr, uptr->wait);
           return SCPE_OK;
       }
       if (uptr->COL < 80) {
           if ((uptr->STATUS & DATA_REQ) == 0) {
               uptr->STATUS |= DATA_REQ;
               set_interrupt(CP_DEVNUM, uptr->STATUS);
           }
           sim_activate(uptr, uptr->wait);
           return SCPE_OK;
        }
        uptr->COL = 0;
        uptr->STATUS &= ~(PUNCH_ON|CARD_IN_PUNCH);
        uptr->STATUS |= END_CARD;
        switch(sim_punch_card(uptr, cp_buffer)) {
        case CDSE_EOF:
        case CDSE_EMPTY:
            uptr->STATUS |= PICK_FAIL|TROUBLE;
            break;
           /* If we get here, something is wrong */
        case CDSE_ERROR:
            uptr->STATUS |= EJECT_FAIL|TROUBLE;
            break;
        case CDSE_OK:
            break;
        }
        if ((uptr->STATUS & (TROUBLE|TROUBLE_EN)) == (TROUBLE|TROUBLE_EN))
            set_interrupt(CP_DEVNUM, uptr->STATUS);
        if (uptr->STATUS & END_CARD_EN)
            set_interrupt(CP_DEVNUM, uptr->STATUS);
    }

    return SCPE_OK;
}


t_stat
cp_attach(UNIT * uptr, CONST char *file)
{
    sim_switches |= SWMASK ('A');   /* Position to EOF */
    return sim_card_attach(uptr, file);
}

t_stat
cp_detach(UNIT * uptr)
{

    if (uptr->STATUS & CARD_IN_PUNCH)
        sim_punch_card(uptr, cp_buffer);
    return sim_card_detach(uptr);
}

t_stat
cp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Punch\n\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cp_description(DEVICE *dptr)
{
   return "Card Punch";
}

#endif

