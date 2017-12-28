/* i7000_con.c: IBM 7000 Inquiry Console.

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

   This is the standard inquiry or console interface.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"

#ifdef NUM_DEVS_CON


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

/* Device status information stored in u5 */

struct _con_data
{
    uint8               ibuff[145];     /* Input line buffer */
    uint8               inptr;
}
con_data[NUM_DEVS_CON];

uint32              con_cmd(UNIT *, uint16, uint16);
void                con_ini(UNIT *, t_bool);
t_stat              con_srv(UNIT *);
t_stat              con_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *con_description(DEVICE *dptr);

extern char         ascii_to_six[128];

UNIT                con_unit[] = {
    {UDATA(con_srv, UNIT_S_CHAN(CHAN_CHUREC), 0), 0},   /* A */
};

DEVICE              con_dev = {
    "INQ", con_unit, NULL, NULL,
    NUM_DEVS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &con_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &con_help, NULL, NULL, &con_description
};



/*
 *Console printer routines.
 */
void
con_ini(UNIT *uptr, t_bool f) {
     int                 u = (uptr - con_unit);
     con_data[u].inptr = 0;
     uptr->u5 = 0;
     sim_activate(uptr, 1000);
}

uint32
con_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - con_unit);

    /* Are we currently tranfering? */
    if (uptr->u5 & (URCSTA_READ|URCSTA_WRITE|URCSTA_BUSY))
        return SCPE_BUSY;

    switch (cmd) {
    /* Test ready */
    case IO_TRS:
        sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd TRS\n", u);
        return SCPE_OK;

    /* Get record from CPU */
    case IO_WRS:
        sim_putchar('R');
        sim_putchar(' ');
        sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd WRS\n", u);
        chan_set_sel(chan, 1);
        uptr->u5 |= URCSTA_WRITE;
        uptr->u3 = 0;
        return SCPE_OK;

    /* Send record to CPU */
    case IO_RDS:
        if (uptr->u5 & URCSTA_INPUT)
            return SCPE_BUSY;
        if (con_data[u].inptr == 0) {
            /* Activate input so we can get response */
            uptr->u5 |= URCSTA_INPUT;
            sim_putchar('I');
            sim_putchar(' ');
        }
        sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd RDS\n", u);
        chan_set_sel(chan, 0);
        uptr->u5 |= URCSTA_READ;
        uptr->u3 = 0;
        return SCPE_OK;
    }
    chan_set_attn(chan);
    return SCPE_IOERR;
}

/* Handle transfer of data for printer */
t_stat
con_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    uint8               ch;
    int                 u = (uptr - con_unit);
    t_stat              r;

    /* Waiting for disconnect */
    if (uptr->u5 & URCSTA_WDISCO) {
        if (chan_stat(chan, DEV_DISCO)) {
            sim_debug(DEBUG_DETAIL, &con_dev, " Disco\n");
            chan_clear(chan, DEV_SEL|DEV_WEOR);
            uptr->u5 &= ~ URCSTA_WDISCO;
            sim_activate(uptr, 25);
            return SCPE_OK;
        } else {
            /* No disco yet, try again in a bit */
            sim_activate(uptr, 50);
            return SCPE_OK;
        }
    }

    uptr->u5 &= ~URCSTA_BUSY;   /* Clear busy */

    /* Copy next column over */
    if (uptr->u5 & URCSTA_WRITE) {
        switch(chan_read_char(chan, &ch, 0)) {
        case TIME_ERROR:
        case END_RECORD:
             sim_putchar('\r');
             sim_putchar('\n');
        sim_debug(DEBUG_EXP, &con_dev, "\n\r");
             uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY;
             uptr->u5 &= ~URCSTA_WRITE;
             break;
       case DATA_OK:
             ch &= 077;
        sim_debug(DEBUG_EXP, &con_dev, "%c", sim_six_to_ascii[ch]);
             sim_putchar(sim_six_to_ascii[ch]);
             break;
       }
       sim_activate(uptr, 100);
       return SCPE_OK;
    }

    /* Copy next column over */
    if ((uptr->u5 & URCSTA_INPUT) == 0 &&  uptr->u5 & URCSTA_READ) {
        sim_debug(DEBUG_DATA, &con_dev, "%d: Char > %02o %x\n", u,
                        con_data[u].ibuff[uptr->u3], chan_flags[chan]);
        switch(chan_write_char(chan, &con_data[u].ibuff[uptr->u3],
            ((uptr->u3+1) == con_data[u].inptr)? DEV_REOR: 0)) {
        case TIME_ERROR:
        case END_RECORD:
            uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY;
            uptr->u5 &= ~URCSTA_READ;
        sim_debug(DEBUG_EXP, &con_dev, "EOR");
            chan_clear_attn_inq(chan);
            con_data[u].inptr = 0;
            break;
        case DATA_OK:
            uptr->u3++;
            break;
        }
        sim_activate(uptr, 10);
        return SCPE_OK;
    }

    r = sim_poll_kbd();
    if (r & SCPE_KFLAG) {
        ch = r & 0377;
        if (uptr->u5 & URCSTA_INPUT) {
           /* Handle end of buffer */
           switch (ch) {
           case '\r':
           case '\n':
                uptr->u5 &= ~URCSTA_INPUT;
                sim_putchar('\r');
                sim_putchar('\n');
                chan_set_attn_inq(chan);
                break;
           case 033:
                uptr->u5 &= ~URCSTA_INPUT;
                con_data[u].inptr = 0;
                break;
           case '\b':
                if (con_data[u].inptr != 0) {
                  con_data[u].inptr--;
                  sim_putchar(ch);
                }
                break;
           default:
                if (con_data[u].inptr < sizeof(con_data[u].ibuff)) {
                    ch = sim_ascii_to_six[0177&ch];
                    if (ch == 0xff) {
                        sim_putchar('\007');
                        break;
                    }
                    sim_putchar(sim_six_to_ascii[ch]);
                    con_data[u].ibuff[con_data[u].inptr++] = ch;
                }
                break;
           }
         } else {
            if (ch == 033) {
                if (con_data[u].inptr != 0) {
                    chan_clear_attn_inq(chan);
                } else {
#ifdef I7070
                    chan_set_attn_inq(chan);
#endif
                    sim_putchar('I');
                    sim_putchar(' ');
                    uptr->u5 |= URCSTA_INPUT;
                 }
                 con_data[u].inptr = 0;
            }
        }
    }
    sim_activate(uptr, 500);
    return SCPE_OK;
}

t_stat
con_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Supervisory Printer\n\n");
   fprintf (st, "This is the interface from the operator to the system. The printer\n");
   fprintf (st, "operated in a half duplex mode. To request the system to accept input\n");
   fprintf (st, "press the <esc> key and wait until the system responds with a line with\n");
   fprintf (st, "I as the first character. When you have finished typing your line, press\n");
   fprintf (st, "return or enter key. Backspace will delete the last character.\n");
   fprintf (st, "All responses from the system are prefixed with a R and blank as the\n");
   fprintf (st, "first character\n");
   return SCPE_OK;
}

const char *
con_description(DEVICE *dptr)
{
   return "Supervisory Printer";
}

#endif

