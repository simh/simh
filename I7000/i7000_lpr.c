/* i7000_lpr.c: IBM 7000 Line Printer.

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

   This is the standard line printer.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_LPR

#define UNIT_LPR        UNIT_ATTABLE | UNIT_DISABLE


/* Flags for line printer. */
#define ECHO            (1 << (UNIT_V_UF+0))
#ifdef I7070
#define ATTENA          (1 << (UNIT_V_UF+1))
#define ATTENB          (1 << (UNIT_V_UF+2))
#endif
#ifdef I7080
#define DOUBLE          (1 << (UNIT_V_UF+1))
#define PROGRAM         (1 << (UNIT_V_UF+2))
#endif


/* std devices. data structures

   lpr_dev      Line Printer device descriptor
   lpr_unit     Line Printer unit descriptor
   lpr_reg      Line Printer register list
   lpr_mod      Line Printer modifiers list
*/


struct _lpr_data
{
    uint8               lbuff[145];     /* Output line buffer */
}
lpr_data[NUM_DEVS_LPR];

uint32              lpr_cmd(UNIT *, uint16, uint16);
void                lpr_ini(UNIT *, t_bool);
t_stat              lpr_srv(UNIT *);
t_stat              lpr_reset(DEVICE *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat              lpr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *lpr_description(DEVICE *dptr);

UNIT                lpr_unit[] = {
    {UDATA(lpr_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_LPR, 55), 300},     /* A */
#if NUM_DEVS_LPR > 1
    {UDATA(lpr_srv, UNIT_S_CHAN(CHAN_CHUREC+1) | UNIT_LPR, 55), 300},   /* B */
#endif
};

MTAB                lpr_mod[] = {
    {ECHO, 0,     NULL, "NOECHO", NULL, NULL, NULL, "Don't echo to console"},
    {ECHO, ECHO, "ECHO", "ECHO", NULL, NULL, NULL, "Echo to console"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lpr_setlpp, &lpr_getlpp, NULL, "Number of lines per page"},
#ifdef I7080
    {DOUBLE|PROGRAM, 0, "SINGLE", "SINGLE", NULL, NULL, NULL, "Single space output"},
    {DOUBLE|PROGRAM, DOUBLE, "DOUBLE", "DOUBLE", NULL, NULL, NULL, "Double space output"},
    {DOUBLE|PROGRAM, PROGRAM, "PROGRAM", "PROGRAM", NULL, NULL, NULL, "Programatic spacing"},
#endif
#ifdef I7070
    {ATTENA|ATTENB, 0, NULL, "NOATTEN", NULL, NULL, NULL, "No attention signal"},
    {ATTENA|ATTENB, ATTENA, "ATTENA", "ATTENA", NULL, NULL, NULL, "Signal Attention A"},
    {ATTENA|ATTENB, ATTENB, "ATTENB", "ATTENB", NULL, NULL, NULL, "Signal Attention B"},
#endif
#ifdef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL, "Set device channel"},
#endif
    {0}
};

DEVICE              lpr_dev = {
    "LP", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
};



/*
 * Line printer routines
 */

t_stat
lpr_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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
    if (i < 20 || i > 100)
        return SCPE_ARG;
    uptr->capac = i;
    uptr->u4 = 0;
    return SCPE_OK;
}

t_stat
lpr_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat
print_line(UNIT * uptr, int chan, int unit)
{
/* Convert word record into column image */
/* Check output type, if auto or text, try and convert record to bcd first */
/* If failed and text report error and dump what we have */
/* Else if binary or not convertable, dump as image */

    char                out[150];       /* Temp conversion buffer */
    int                 i;

    if ((uptr->flags & (UNIT_ATT | ECHO)) == 0)
        return SCPE_UNATT;      /* attached? */

    /* Try to convert to text */
    memset(out, 0, sizeof(out));

#ifdef I7080
    if (uptr->flags & PROGRAM) {
        switch(lpr_data[unit].lbuff[0] & 077) {
        case 060: /* suppress space */
             uptr->u5 |= URCSTA_SKIPAFT | (0 << 12);
             break;
        case 020: /* single space */
             break;
        case 012: /* double space */
             uptr->u5 |= URCSTA_SKIPAFT | (1 << 12);
             break;
        default:
                  /* Skip channel */
             i = 0;
             switch(lpr_data[unit].lbuff[0]  & 017) {
             case 3:    i = 5 - (uptr->u4 % 5); break;
             case 2:    i = 8 - (uptr->u4 % 8); break;
             case 1:
             case 9:    if (uptr->u4 == 1)
                            break;
                        i = uptr->capac - uptr->u4 + 1; break;
             }
             if (i == 0)
                break;
             uptr->u5 |= URCSTA_SKIPAFT | (i << 12);
        }
        /* Scan each column */
        for (i = 0; i < 143; i++) {
            int                 bcd = lpr_data[unit].lbuff[i+1] & 077;

            out[i] = sim_six_to_ascii[bcd];
        }
     } else {
        if (uptr->flags & DOUBLE)
            uptr->u5 |= URCSTA_SKIPAFT | (1 << 12);
#endif
    /* Scan each column */
    for (i = 0; i < 144; i++) {
        int                 bcd = lpr_data[unit].lbuff[i] & 077;

        out[i] = sim_six_to_ascii[bcd];
    }

#ifdef I7080
    }
#endif

    /* Trim trailing spaces */
    for (--i; i > 0 && out[i] == ' '; i--) ;
    out[++i] = '\r';
    out[++i] = '\n';
    out[++i] = '\0';
    sim_debug(DEBUG_DETAIL, &lpr_dev, "WRS unit=%d [%s]\n", unit, &out[0]);

    /* Print out buffer */
    if (uptr->flags & UNIT_ATT) {
        sim_fwrite(&out, 1, i, uptr->fileref);
        uptr->pos += i;
    }
    if (uptr->flags & ECHO) {
        int                 j = 0;

        while (j <= i)
            sim_putchar(out[j++]);
    }
    uptr->u4++;
    if (uptr->u4 > (int32)uptr->capac) {
        uptr->u4 = 1;
    }

    if (uptr->u5 & URCSTA_SKIPAFT) {
        i = (uptr->u5 >> 12) & 0x7f;
        if (i == 0) {
            if (uptr->flags & UNIT_ATT) {
                sim_fwrite("\r\n", 1, 2, uptr->fileref);
                uptr->pos += 2;
            }
            if (uptr->flags & ECHO)
                sim_putchar('\r');
        } else {
            for (; i > 1; i--) {
                if (uptr->flags & UNIT_ATT) {
                    sim_fwrite("\r\n", 1, 2, uptr->fileref);
                    uptr->pos += 2;
                }
                if (uptr->flags & ECHO) {
                    sim_putchar('\r');
                    sim_putchar('\n');
                }
                uptr->u4++;
                if (uptr->u4 > (int32)uptr->capac) {
                    uptr->u4 = 1;
                }
            }
        }
        uptr->u5 &= ~(URCSTA_SKIPAFT|(0x7f << 12));
    }

    if (uptr->u4 == 1)
        lpr_chan9[chan] = 1;
#ifdef I7010
    if (uptr->u4 == uptr->capac)
        lpr_chan12[chan] = 1;
#endif

    return SCPE_OK;
}


uint32 lpr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
#ifdef I7010
    int                 i;
#endif

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_WRITE)
        return SCPE_BUSY;

    switch(cmd) {
        /* Test ready */
    case IO_TRS:
        if (uptr->flags & UNIT_ATT)
            return SCPE_OK;
        break;

    /* Suppress punch */
    case IO_RUN:
        sim_debug(DEBUG_CMD, &lpr_dev, "%d: Cmd RUN\n", u);
        uptr->u5 &= ~URCSTA_FULL;
        return SCPE_OK;

    /* Get record from CPU */
    case IO_WRS:
        sim_debug(DEBUG_CMD, &lpr_dev, "%d: Cmd WRS\n", u);
        lpr_chan9[chan] = 0;
#ifdef I7010
        lpr_chan12[chan] = 0;
        switch (dev & 017) {
        case 01:
                uptr->u5 |= URCSTA_WMKS;
                break;
        case 012:
                uptr->u5 &= ~URCSTA_WMKS;
                break;
        default:
                return SCPE_IOERR;
        }
#endif
        chan_set_sel(chan, 1);
        uptr->u5 |= URCSTA_WRITE;
        uptr->u3 = 0;
        if ((uptr->u5 & URCSTA_BUSY) == 0)
            sim_activate(uptr, 50);
        return SCPE_OK;

    case IO_CTL:
        sim_debug(DEBUG_CMD, &lpr_dev, "%d: Cmd CTL %02o\n", u, dev & 077);
#ifdef I7010
        /*    1-0 immediate skip to channel */
        /*    00xxxx    skip to channel immediate */
        /*    11xxxx    skip to channel after */
        /*    1000xx    space before */
        /*    0100xx    space after */
        switch(dev & 060) {
        case 020: /* Space after */
             uptr->u5 |= URCSTA_SKIPAFT | ((dev & 03) << 12);
             break;
        case 040: /* Space before */
             for (i = dev & 03; i > 1; i--) {
                if (uptr->flags & UNIT_ATT) {
                    sim_fwrite("\r\n", 1, 2, uptr->fileref);
                    uptr->pos += 2;
                }
                if (uptr->flags & ECHO) {
                    sim_putchar('\r');
                    sim_putchar('\n');
                }
             }
             break;
        case 0:   /* Skip channel immediate */
        case 060: /* Skip channel after */
             i = 0;
             switch(dev & 017) {
             case 3:    i = 5 - (uptr->u4 % 5); break;
             case 2:    i = 8 - (uptr->u4 % 8); break;
             case 1:
             case 9:    if (uptr->u4 == 1)
                            break;
                        i = uptr->capac - uptr->u4 + 1; break;
             case 12:   i = (uptr->capac/2) - uptr->u4; break;
             }
             if (i == 0)
                break;
             if (dev & 060) {
                uptr->u5 |= URCSTA_SKIPAFT | (i << 12);
                break;
             }
             for (; i > 0; i--) {
                if (uptr->flags & UNIT_ATT) {
                    sim_fwrite("\r\n", 1, 2, uptr->fileref);
                    uptr->pos += 2;
                }
                if (uptr->flags & ECHO) {
                    sim_putchar('\r');
                    sim_putchar('\n');
                }
             }
             break;
        }
        if (uptr->u4 == uptr->capac)
            lpr_chan12[chan] = 1;
#endif
        if (uptr->u4 == 1)
            lpr_chan9[chan] = 1;
        return SCPE_OK;
    }
    chan_set_attn(chan);
    return SCPE_IOERR;
}

/* Handle transfer of data for printer */
t_stat
lpr_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
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
        /* Done waiting, print line */
        if (uptr->u5 & URCSTA_FULL) {
              uptr->u5 &= ~URCSTA_FULL;
              switch(print_line(uptr, chan, u)) {
              case SCPE_EOF:
              case SCPE_UNATT:
                  chan_set_eof(chan);
                  break;
                 /* If we get here, something is wrong */
              case SCPE_IOERR:
                  chan_set_error(chan);
                  break;
              case SCPE_OK:
                  break;
              }
        }
        memset(&lpr_data[u].lbuff[0], 0, 144);
        uptr->u5 &= ~URCSTA_BUSY;
#ifdef I7070
        switch(uptr->flags & (ATTENA|ATTENB)) {
        case ATTENA: chan_set_attn_a(chan); break;
        case ATTENB: chan_set_attn_b(chan); break;
        }
#endif
#ifdef I7010
        chan_set_attn_urec(chan, lpr_dib.addr);
#endif
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_WRITE && uptr->u3 < 144) {
        switch(chan_read_char(chan, &lpr_data[u].lbuff[uptr->u3],
                (uptr->u3 == 143)?DEV_REOR: 0)) {
        case TIME_ERROR:
        case END_RECORD:
            uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY|URCSTA_FULL;
            uptr->u5 &= ~URCSTA_WRITE;
            break;
        case DATA_OK:
            sim_debug(DEBUG_DATA, &lpr_dev, "%d: Char < %02o\n", u,
                        lpr_data[u].lbuff[uptr->u3]);
#ifdef I7010
            if (uptr->u5 & URCSTA_WMKS) {
                if (lpr_data[u].lbuff[uptr->u3] & 0200)
                    lpr_data[u].lbuff[uptr->u3] = 1;
                else
                    lpr_data[u].lbuff[uptr->u3] = 012;
            }
#endif
            uptr->u3++;
            break;
        }
        sim_activate(uptr, 10);
    }
    return SCPE_OK;
}

void
lpr_ini(UNIT *uptr, t_bool f) {
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    uptr->u4 = 0;
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    if (uptr->u5 & URCSTA_FULL)
        print_line(uptr, UNIT_G_CHAN(uptr->flags), uptr - lpr_unit);
    return detach_unit(uptr);
}

t_stat
lpr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{

   fprintf (st, "%s\n\n", lpr_description(dptr));
   fprintf (st, "The line printer output can be echoed to the console to check");
   fprintf (st, "the \nprogress of jobs being run. This can be done with the\n");
   fprintf (st, "        sim> SET %s ECHO       set echo to console\n\n", dptr->name);
   fprintf (st, "The Line printer can be configured to any number of lines per page with the:\n");
   fprintf (st, "        sim> SET %s LINESPERPAGE=n\n\n", dptr->name);
   fprintf (st, "The default is 59 lines per page.\n\n");
#ifdef I7080
   fprintf (st, "The 716 printer can operate in one of three spacing modes\n");
   fprintf (st, "        sim> SET %s SINGLE     for single spacing\n", dptr->name);
   fprintf (st, "        sim> SET %s DOUBLE     for double spacing\n", dptr->name);
   fprintf (st, "        sim> SET %s PROGRAM    for program control of  spacing\n\n", dptr->name);
#endif
#ifdef I7070
   fprintf (st, "Unit record devices can be configured to interrupt the CPU on\n");
   fprintf (st, "one of two priority channels A or B, to set this\n\n");
   fprintf (st, "        sim> SET %s ATTENA     to set device to raise Atten A\n", dptr->name);
   fprintf (st, "        sim> SET %s ATTENB     to set device to raise Atten B\n\n", dptr->name);
#endif
#ifdef I7010
   help_set_chan_type(st, dptr, "Line printer");
#endif
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
lpr_description(DEVICE *dptr)
{
#ifdef I7010
   return "1403 Line Printer";
#endif
#ifdef I7070
   return "7400 Line Printer";
#endif
#ifdef I7080
   return "716 Line Printer";
#endif
}

#endif


