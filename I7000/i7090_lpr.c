/* i7090_lpr.c: IBM 7090 Standard line printer.

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

   This is the standard line printer that all 70xx systems have.

   For WRS read next 24 words and fill print buffer.
      Row 9, 8, 7, 6, 5, 4, 3, 2, 1, 10, 11, 12
   For RDS read rows 9, 8, 7, 6, 5, 4, 3, 2, 1,
          Echo 8|4
          read row 10
          Echo 8|3
          read row 11
          Echo 9
          read row 12
          Echo 8, 7, 6, 5, 4, 3, 2, 1

*/

#include "i7090_defs.h"
#include "sim_console.h"
#include "sim_card.h"

#ifdef NUM_DEVS_LPR

#define UNIT_LPR        UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ
#define ECHO            (1 << UNIT_V_LOCAL)


/* std devices. data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

/* Output selection is stored in u3 */
/* Line count is stored in u4 */
/* Device status information stored in u5 */
/* Position is stored in u6 */
#define LPRSTA_RCMD     002000          /* Read command */
#define LPRSTA_WCMD     004000          /* Write command */

#define LPRSTA_EOR      010000          /* Hit end of record */
#define LPRSTA_BINMODE  020000          /* Line printer started in bin mode */
#define LPRSTA_CHANGE   040000          /* Turn DEV_WRITE on */
#define LPRSTA_COL72    0100000         /* Mask to last column printed */
#define LPRSTA_IMAGE    0200000         /* Image to print */


struct _lpr_data
{
    t_uint64            wbuff[24];      /* Line buffer */
    char                lbuff[74];      /* Output line buffer */
}
lpr_data[NUM_DEVS_LPR];

uint32              lpr_cmd(UNIT *, uint16, uint16);
t_stat              lpr_srv(UNIT *);
void                lpr_ini(UNIT *, t_bool);
t_stat              lpr_reset(DEVICE *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat              lpr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *lpr_description (DEVICE *dptr);

extern char         six_to_ascii[64];

UNIT                lpr_unit[] = {
#if NUM_DEVS_LPR > 1
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_A) | UNIT_LPR | ECHO, 55)},        /* A */
#endif
#if NUM_DEVS_LPR > 2
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_C) | UNIT_LPR, 55)},       /* B */
#endif
#if NUM_DEVS_LPR > 3
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_E) | UNIT_LPR | UNIT_DIS, 55)},    /* C */
#endif
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_CHPIO) | UNIT_LPR, 55)},   /* 704 */
};

MTAB                lpr_mod[] = {
    {ECHO, 0,     NULL, "NOECHO", NULL, NULL, NULL, "Done echo to console"},
    {ECHO, ECHO, "ECHO", "ECHO", NULL, NULL, NULL, "Echo output to console"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lpr_setlpp, &lpr_getlpp, NULL, "Number of lines per page"},
#if NUM_CHAN != 1
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
     &get_chan, NULL},
#endif
    {0}
};

DEVICE              lpr_dev = {
    "LP", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 36,
    NULL, NULL, &lpr_reset, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
};

/* Line printer routines
*/

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

    uint16              buff[80];       /* Temp conversion buffer */
    int                 i, j;
    int                 outsel = uptr->u3;

    if ((uptr->flags & (UNIT_ATT | ECHO)) == 0)
        return SCPE_UNATT;      /* attached? */

    if (outsel & PRINT_3) {
        if (uptr->flags & UNIT_ATT) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            uptr->pos += 2;
        }
        if (uptr->flags & ECHO) {
            sim_putchar('\r');
            sim_putchar('\n');
        }
        uptr->u5 &= ~LPRSTA_COL72;
        uptr->u4++;
    }

    if (outsel & PRINT_4) {
        if (uptr->flags & UNIT_ATT) {
            sim_fwrite("\r\n\r\n", 1, 4, uptr->fileref);
            uptr->pos += 4;
        }
        if (uptr->flags & ECHO) {
            sim_putchar('\r');
            sim_putchar('\n');
            sim_putchar('\r');
            sim_putchar('\n');
        }
        uptr->u5 &= ~LPRSTA_COL72;
        uptr->u4++;
        uptr->u4++;
    }


    /* Try to convert to text */
    memset(buff, 0, sizeof(buff));
    /* Bit flip into temp buffer */
    for (i = 0; i < 24; i++) {
        int                 bit = 1 << (i / 2);
        t_uint64            mask = 1;
        t_uint64            wd = 0;
        int                 b = 36 * (i & 1);
        int                 col;

        wd = lpr_data[unit].wbuff[i];
        for (col = 35; col >= 0; mask <<= 1, col--) {
            if (wd & mask)
                buff[col + b] |= bit;
        }
        lpr_data[unit].wbuff[i] = 0;
    }

    /* Space out printer based on last output */
    if ((outsel & PRINT_9)) {
        /* Trim trailing spaces */
        for (j = 72; j > 0 && lpr_data[unit].lbuff[j] == ' '; j--) ;
        j++;
        if ((uptr->u5 & LPRSTA_COL72) == 0)
            j = 0;

        for (i = j; i < 72; i++) {
            if (uptr->flags & UNIT_ATT) {
                sim_fwrite(" ", 1, 1, uptr->fileref);
                uptr->pos += 1;
            }
            if (uptr->flags & ECHO)
                sim_putchar(' ');
        }
    } else {
        if (uptr->flags & UNIT_ATT) {
            sim_fwrite("\n\r", 1, 2, uptr->fileref);
            uptr->pos += 2;
        }
        if (uptr->flags & ECHO) {
            sim_putchar('\n');
            sim_putchar('\r');
        }
        uptr->u4++;
        uptr->u5 &= ~LPRSTA_COL72;
    }

    /* Scan each column */
    for (i = 0; i < 72; i++) {
        int                 bcd = sim_hol_to_bcd(buff[i]);

        if (bcd == 0x7f)
            lpr_data[unit].lbuff[i] = '{';
        else {
            if (bcd == 020)
                bcd = 10;
            if (uptr->u5 & LPRSTA_BINMODE) {
                char ch = (buff[i] != 0) ? '1' : ' ';
                lpr_data[unit].lbuff[i] = ch;
            } else
                lpr_data[unit].lbuff[i] = sim_six_to_ascii[bcd];
        }
    }
    sim_debug(DEBUG_DETAIL, &lpr_dev, "WRS unit=%d %3o [%72s]\n", unit,
              outsel >> 3, &lpr_data[unit].lbuff[0]);

    /* Trim trailing spaces */
    for (j = 71; j > 0 && lpr_data[unit].lbuff[j] == ' '; j--) ;

    /* Print out buffer */
    if (uptr->flags & UNIT_ATT) {
        sim_fwrite(lpr_data[unit].lbuff, 1, j+1, uptr->fileref);
        uptr->pos += j+1;
    }
    if (uptr->flags & ECHO) {
        for(i = 0; i <= j; i++)
            sim_putchar(lpr_data[unit].lbuff[i]);
    }
    uptr->u5 |= LPRSTA_COL72;

    /* Put output to column where we left off */
    if (outsel != 0) {
        uptr->u5 &= ~LPRSTA_COL72;
    }

    /* Space printer */
    if (outsel & PRINT_2) {
        if (uptr->flags & UNIT_ATT) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            uptr->pos += 2;
        }
        if (uptr->flags & ECHO) {
            sim_putchar('\r');
            sim_putchar('\n');
        }
        uptr->u4++;
    }

    if (outsel & PRINT_1) {
        while (uptr->u4 < (int32)uptr->capac) {
            if (uptr->flags & UNIT_ATT) {
                sim_fwrite("\r\n", 1, 2, uptr->fileref);
                uptr->pos += 2;
            }
            if (uptr->flags & ECHO) {
                sim_putchar('\r');
                sim_putchar('\n');
            }
            uptr->u4++;
        }
    }

    if (uptr->u4 >= (int32)uptr->capac) {
       uptr->u4 -= (int32)uptr->capac;
       dev_pulse[chan] |= PRINT_I;
    }

    return SCPE_OK;
}

uint32 lpr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
    int                 i;

    /* Check if valid */
    if ((dev & 03) == 0 || (dev & 03) == 3)
        return SCPE_NODEV;
    /* Check if attached */
    if ((uptr->flags & (UNIT_ATT | ECHO)) == 0) {
        chan_set_error(chan);
        sim_debug(DEBUG_EXP, &lpr_dev, "unit=%d not ready\n", u);
        return SCPE_IOERR;
    }
    /* Check if still active */
    if (uptr->u5 & URCSTA_CMD) {
        sim_debug(DEBUG_EXP, &lpr_dev, "unit=%d busy\n", u);
        return SCPE_BUSY;
    }
    /* Ok, issue command if correct */
    if (cmd == IO_WRS || cmd == IO_RDS) {
        /* Start device */
        if (((uptr->u5 & (URCSTA_ON | URCSTA_IDLE)) ==
             (URCSTA_ON | URCSTA_IDLE)) && uptr->wait <= 30) {
            uptr->wait += 85;   /* Wait for next latch point */
        } else
            uptr->wait = 330;   /* Startup delay */
        for (i = 0; i < 24; lpr_data[u].wbuff[i++] = 0) ;
        uptr->u6 = 0;
        uptr->u5 &= ~(LPRSTA_WCMD | LPRSTA_RCMD | URCSTA_WRITE | URCSTA_READ);
        uptr->u3 = 0;
        dev_pulse[chan] = 0;
        if (cmd == IO_WRS) {
            sim_debug(DEBUG_CMD, &lpr_dev, "WRS %o unit=%d %d\n", dev, u, uptr->wait);
            uptr->u5 |= LPRSTA_WCMD | URCSTA_CMD | URCSTA_WRITE;
        } else {
            sim_debug(DEBUG_CMD, &lpr_dev, "RDS %o unit=%d %d\n", dev, u, uptr->wait);
            uptr->u5 |= LPRSTA_RCMD | URCSTA_CMD | URCSTA_READ;
        }
        if ((dev & 03) == 2)
            uptr->u5 |= LPRSTA_BINMODE;
        else
            uptr->u5 &= ~LPRSTA_BINMODE;
        chan_set_sel(chan, 1);
        chan_clear_status(chan);
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    } else {
        chan_set_attn(chan);
    }
    return SCPE_IOERR;
}

t_stat lpr_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
    int                 pos;
    int                 r;
    int                 eor = 0;

    /* Channel has disconnected, abort current line. */
    if (uptr->u5 & URCSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        print_line(uptr, chan, u);
        uptr->u5 &= ~(URCSTA_WRITE | URCSTA_READ | URCSTA_CMD | LPRSTA_EOR | LPRSTA_CHANGE);
        uptr->u6 = 0;
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &lpr_dev, "unit=%d disconnect\n", u);
        return SCPE_OK;
    }

    /* If change requested, do that first */
    if (uptr->u5 & LPRSTA_CHANGE) {
        /* Wait until word read by CPU or timeout */
        if (chan_test(chan, DEV_FULL)) {
            uptr->wait -= 50;
            if (uptr->wait == 50)
                uptr->u5 &= ~LPRSTA_CHANGE;
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        } else {
            chan_set(chan, DEV_WRITE);
            sim_activate(uptr, uptr->wait);
            uptr->u5 &= ~LPRSTA_CHANGE;
            uptr->wait = 0;
            return SCPE_OK;
        }
    }

    /* Check to see if we have timed out */
    if (uptr->wait != 0) {
        uptr->wait--;
        /* If at end of record and channel is still active, do another print */
        if (((uptr->u5 & (URCSTA_IDLE|URCSTA_CMD|URCSTA_WRITE|URCSTA_READ|
                 URCSTA_ON)) == (URCSTA_IDLE|URCSTA_CMD|URCSTA_ON))
            && uptr->wait == 1 && chan_test(chan, STA_ACTIVE)) {
            /* Restart same command */
            uptr->u5 |= (URCSTA_WRITE | URCSTA_READ) & (uptr->u5 >> 5);
            uptr->u6 = 0;
            chan_set(chan, DEV_WRITE);
            sim_debug(DEBUG_CHAN, &lpr_dev, "unit=%d restarting\n", u);
        }
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    }

    /* If no request, go to idle mode */
    if ((uptr->u5 & (URCSTA_READ | URCSTA_WRITE)) == 0) {
        if ((uptr->u5 & (URCSTA_IDLE | URCSTA_ON)) == (URCSTA_IDLE | URCSTA_ON)) {
            uptr->wait = 85;    /* Delay 85ms */
            uptr->u5 &= ~URCSTA_IDLE;   /* Not running */
            sim_activate(uptr, us_to_ticks(1000));
        } else {
            uptr->wait = 330;   /* Delay 330ms */
            uptr->u5 &= ~URCSTA_ON;     /* Turn motor off */
        }
        return SCPE_OK;
    }

    /* Motor is on and up to speed */
    uptr->u5 |= URCSTA_ON;
    uptr->u5 &= ~URCSTA_IDLE;
    pos = uptr->u6;

    uptr->u3 |= dev_pulse[chan] & PRINT_M;

    /* Check if he write out last data */
    if (uptr->u5 & URCSTA_READ) {
        int                 wrow = 0;
        t_uint64            wd = 0;
        int                 action = 0;

        /* Case 0: Read word from MF memory, DEV_WRITE=1 */
        /* Case 1: Read word from MF memory, write echo back */
        /* Case 2: Write echoback, after gone switch to read */
        /* Case 3: Write echoback */
        /* Case 4: No update, DEV_WRITE=1 */
        eor = (uptr->u5 & LPRSTA_BINMODE) ? 1 : 0;
        switch (pos) {
        case 46:
            print_line(uptr, chan, u);
            pos = 0;
            /* Fall through */
        case 0:
        case 1:                 /* Row 9 */
        case 2:
        case 3:                 /* Row 8 */
        case 4:
        case 5:                 /* Row 7 */
        case 6:
        case 7:                 /* Row 6 */
        case 8:
        case 9:                 /* Row 5 */
        case 10:
        case 11:                /* Row 4 */
        case 12:
        case 13:                /* Row 3 */
        case 14:
        case 15:                /* Row 2 */
        case 16:                /* Row 1R */
            wrow = pos;
            break;
        case 17:                /* Row 1L and start Echo */
            wrow = pos;
            action = 1;
            break;
        case 18:                /* Echo 8-4 R */
            wd = lpr_data[u].wbuff[2];
            wd &= lpr_data[u].wbuff[10];
            action = 2;
            wrow = pos;
            break;
        case 19:                /* Echo 8-4 L */
            wd = lpr_data[u].wbuff[3];
            wd &= lpr_data[u].wbuff[11];
            action = 3;
            wrow = pos;
            break;
        case 20:                /* Row 10 R */
            wrow = 18;
            break;
        case 21:                /* Row 10 L */
            wrow = 19;
            action = 1;
            break;
        case 22:                /* Echo 8-3 */
            /* Fill for echo back */
            wd = lpr_data[u].wbuff[12];
            wd &= lpr_data[u].wbuff[2];
            action = 2;
            wrow = pos;
            break;
        case 23:
            wd = lpr_data[u].wbuff[13];
            wd &= lpr_data[u].wbuff[3];
            action = 3;
            wrow = pos;
            break;
        case 24:                /* Row 11 R */
            wrow = 20;
            break;
        case 25:                /* Row 11 L */
            wrow = 21;
            action = 1;
            break;
        case 26:                /* Echo 9 */
            wd = lpr_data[u].wbuff[0];
            action = 2;
            wrow = pos;
            break;
        case 27:
            wd = lpr_data[u].wbuff[1];
            action = 3;
            wrow = pos;
            break;
        case 28:
            wrow = 22;
            break;
        case 29:                /* Row 12 */
            wrow = 23;
            action = 1;
            break;
        case 45:                /* Echo 1 */
            eor = 1;
            /* Fall through */

        case 30:
        case 31:                /* Echo 8 */
        case 32:
        case 33:                /* Echo 7 */
        case 34:
        case 35:                /* Echo 6 */
        case 36:
        case 37:                /* Echo 5 */
        case 38:
        case 39:                /* Echo 4 */
        case 40:
        case 41:                /* Echo 3 */
        case 42:
        case 43:                /* Echo 2 */
        case 44:                /* Echo 1 */
            wrow = pos - 28;
            wd = lpr_data[u].wbuff[wrow];
            action = 2;
            break;
        }

        if (action == 0 || action == 1) {
        /* If reading grab next word */
            r = chan_read(chan, &lpr_data[u].wbuff[wrow], 0);
            sim_debug(DEBUG_DATA, &lpr_dev, "print read row < %d %d %012llo eor=%d\n", 
                 pos, wrow, lpr_data[u].wbuff[wrow], 0);
            if (action == 1)
                chan_clear(chan, DEV_WRITE);
        } else { /* action == 2 || action == 3 */
        /* Place echo data in buffer */
            sim_debug(DEBUG_DATA, &lpr_dev, "print read row > %d %d %012llo eor=%d\n", 
                pos, wrow, wd, eor);
            r = chan_write(chan, &wd, 0);
            /* Change back to reading */
            if (action == 3) {
                uptr->wait = 650;
                uptr->u6 = ++pos;
                uptr->u5 &= ~(LPRSTA_EOR);
                uptr->u5 |= LPRSTA_CHANGE;
                sim_activate(uptr, us_to_ticks(100));
                return SCPE_OK;
            }
        }
    } else {
        eor = (pos == 23 || (uptr->u5 & LPRSTA_BINMODE && pos == 1)) ? 1 : 0;
        if (pos == 24 || (uptr->u5 & LPRSTA_BINMODE && pos == 2)) {
            print_line(uptr, chan, u);
            pos = 0;
        }
        r = chan_read(chan, &lpr_data[u].wbuff[pos], 0);
        sim_debug(DEBUG_DATA, &lpr_dev, "print row %d %012llo %d\n", pos,
                lpr_data[u].wbuff[pos], eor);
    }

    uptr->u6 = pos + 1;
    switch (r) {
    case END_RECORD:
        uptr->wait = 100;        /* Print wheel gap */
        uptr->u5 |= LPRSTA_EOR | URCSTA_IDLE;
        uptr->u5 &= ~(URCSTA_WRITE | URCSTA_READ);
        chan_set(chan, DEV_REOR);
        break;
    case DATA_OK:
        if (eor) {
            uptr->wait = 100;    /* Print wheel gap */
            uptr->u5 |= LPRSTA_EOR | URCSTA_IDLE;
            uptr->u5 &= ~(URCSTA_WRITE | URCSTA_READ);
            chan_set(chan, DEV_REOR);
        } else {
            uptr->wait = 0;
            uptr->u5 &= ~(LPRSTA_EOR);
            sim_activate(uptr, (pos & 1) ? us_to_ticks(500) : us_to_ticks(16000));
            return SCPE_OK;
        }
        break;
    case TIME_ERROR:
        chan_set_attn(chan);
        chan_set(chan, DEV_REOR);
        uptr->wait = 13 * (12 - (pos / 2)) + 85;
        uptr->u5 &= ~(URCSTA_READ | URCSTA_WRITE);
        uptr->u5 |= URCSTA_IDLE;
        break;
    }

    sim_activate(uptr, us_to_ticks(1000));
    return SCPE_OK;
}

void
lpr_ini(UNIT * uptr, t_bool f)
{
    int                 u = (uptr - lpr_unit);

    uptr->u3 = 0;
    uptr->u4 = 0;
    uptr->u5 = 0;
    memset(&lpr_data[u].lbuff, ' ', sizeof(lpr_data[u].lbuff));
}

t_stat
lpr_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    sim_switches |= SWMASK ('A');   /* Position to EOF */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    return detach_unit(uptr);
}

t_stat
lpr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   const char *cpu = cpu_description(&cpu_dev);
   extern void fprint_attach_help_ex (FILE *st, DEVICE *dptr, t_bool silent);

   fprintf (st, "%s\n\n", lpr_description(dptr));
#if NUM_DEVS_LPR > 3
   fprintf (st, "The %s supports up to four line printers", cpu);
#elif NUM_DEVS_LPR > 2
   fprintf (st, "The %s supports up to three line printers", cpu);
#elif NUM_DEVS_LPR > 1
   fprintf (st, "The %s supports up to two line printers", cpu);
#elif NUM_DEVS_LPR > 0
   fprintf (st, "The %s supports one line printer", cpu);
#endif
   fprintf (st, "by default. The Line printer can\n");
   fprintf (st, "The printer acted as the console printer:\n\n");
   fprintf (st, "        sim> SET %s ECHO\n\n", dptr->name);
   fprintf (st, "Causes all output sent to printer to also go to console.\n");
   help_set_chan_type(st, dptr, "Line printers");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
lpr_description(DEVICE *dptr)
{
   return "716 Line Printer";
}


#endif
