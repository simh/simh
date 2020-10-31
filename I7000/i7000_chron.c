/* i7090_chron.c: IBM 7090 Chrono clock on MT drive.

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


*/

#include "i7000_defs.h"

#ifdef NUM_DEVS_CHRON
#define BUFFSIZE        (12)

#define UNIT_MT(x)      UNIT_DISABLE | UNIT_ROABLE | \
                        UNIT_S_CHAN(x)

/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5 */
#define MT_RDS          1
#define MT_RDSB         2
#define MT_SKIP         11      /* Do skip to end of record */
#define MT_CMDMSK   000017      /* Command being run */
#define MT_RDY      000020      /* Device is ready for command */
#define MT_IDLE     000040      /* Tape still in motion */
#define MT_EOR      000200      /* Hit end of record */
#define MT_ERR      000400      /* Device recieved error */
#define MT_BOT      001000      /* Unit at begining of tape */
#define MT_EOT      002000      /* Unit at end of tape */

uint32              chron_cmd(UNIT *, uint16, uint16);
t_stat              chron_srv(UNIT *);
t_stat              chron_reset(DEVICE *);
t_stat              set_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              get_addr(FILE * st, UNIT *uptr, int32 v, CONST void *desc);
t_stat              chron_help(FILE *st, DEVICE *dptr, UNIT *uptr,
                             int32 flags, const char *ctxt);
const char          *chron_description (DEVICE *dptr);

/* One buffer per channel */
uint8               chron_buffer[BUFFSIZE];

UNIT                chron_unit[] = {
/* Controller 1 */
    {UDATA(&chron_srv, UNIT_MT(1) | UNIT_DIS, 0), 10},  /* 0 */
};

MTAB                chron_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "UNIT", "UNIT", &set_addr, &get_addr,
     NULL, "Chronoclock unit number"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan, &get_chan,
     NULL, "Chronoclock channel"},
    {0}
};

DEVICE              chron_dev = {
    "CHRON", chron_unit, NULL, chron_mod,
    NUM_DEVS_CHRON, 8, 15, 1, 8, 8,
    NULL, NULL, &chron_reset, NULL, NULL, NULL,
    &chron_dib, DEV_DISABLE, 0, NULL,
    NULL, NULL, &chron_help, NULL, NULL, &chron_description
};

uint32 chron_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 time = 30;
    int                 unit = (dev & 017);

    /* Make sure valid drive number */
    if (unit != uptr->u3)
        return SCPE_NODEV;
    if (uptr->flags & UNIT_DIS)
        return SCPE_NODEV;

    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & MT_RDY) == 0) {
        /* Return indication if not ready and doing TRS */
        if (cmd == IO_TRS) {
            return SCPE_IOERR;
        } else
            return SCPE_BUSY;
    }
    uptr->u5 &= ~(MT_CMDMSK | MT_RDY);
    switch (cmd) {
    case IO_RDS:
        if (dev & 020)
            uptr->u5 |= MT_RDSB;
        else
            uptr->u5 |= MT_RDS;
        time = 100;
        chan_set_sel(chan, 0);
        chan_clear_status(chan);
        uptr->u6 = 0;
        break;
    case IO_WRS:
        /* Can't write to it so return error */
        return SCPE_IOERR;

    case IO_BSR: /* Nop, just set us back at begining */
    case IO_BSF: /* Nop, just set flag and leave */
        chan_set(chan, CHS_BOT);
        /* All nops, just return success */
    case IO_WEF:
    case IO_REW:
    case IO_RUN:
    case IO_SDL:
    case IO_SDH:
    case IO_TRS:
        return SCPE_OK;
    }
    sim_cancel(uptr);
    sim_activate(uptr, us_to_ticks(time));
    return SCPE_OK;
}

/* Chronolog clock */

/* Convert number (0-99) to BCD */

static void
bcd_2d(int n, uint8 * b2)
{
    uint8               d1, d2;

    d1 = n / 10;
    d2 = n % 10;
    *b2++ = d1;
    *b2 = d2;
}

void
chron_read_buff(UNIT * uptr, int cmd)
{
    time_t              curtim;
    struct tm          *tptr;
    int                 ms;

    uptr->u6 = 0;               /* Set to no data */

    curtim = sim_get_time(NULL);/* get time */
    tptr = localtime(&curtim);  /* decompose */
    if (tptr == NULL)
        return;                 /* error? */

    ms = sim_os_msec() % 1000;
    ms /= 100;

    /* Convert and fill buffer */
    bcd_2d(tptr->tm_mon + 1, &chron_buffer[0]);
    bcd_2d(tptr->tm_mday, &chron_buffer[2]);
    bcd_2d(tptr->tm_hour, &chron_buffer[4]);
    bcd_2d(tptr->tm_min, &chron_buffer[6]);
    bcd_2d(tptr->tm_sec,  &chron_buffer[8]);
    bcd_2d(ms,  &chron_buffer[10]);
    return;
}

t_stat chron_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 cmd = uptr->u5 & MT_CMDMSK;

    /* Channel has disconnected, abort current read. */
    if ((uptr->u5 & MT_RDY) == 0 && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 &= ~MT_CMDMSK;
        if (cmd == MT_RDS || cmd == MT_RDSB) {
            uptr->u6 = 0;
        }
        uptr->u5 |= MT_RDY;
        chan_clear(chan, DEV_WEOR|DEV_SEL);
        return SCPE_OK;
    }

    switch (uptr->u5 & MT_CMDMSK) {
    case 0:                     /* No command, stop tape */
        uptr->u5 |= MT_RDY;     /* Ready since command is done */
        break;

    case MT_SKIP:               /* Record skip done, enable tape drive */
        sim_activate(uptr, us_to_ticks(500));
        break;

    case MT_RDS:
    case MT_RDSB:
        if (uptr->u6 == 0)
            chron_read_buff(uptr, cmd);
        switch (chan_write_char(chan, &chron_buffer[uptr->u6],
                           (uptr->u6 == (BUFFSIZE-1)) ? DEV_REOR : 0)) {
        case DATA_OK:
            uptr->u6++;
            sim_activate(uptr, us_to_ticks(100));
            break;

        case END_RECORD:
        case TIME_ERROR:
            uptr->u5 &= ~MT_CMDMSK;
            uptr->u5 |= MT_SKIP;
            sim_activate(uptr, us_to_ticks(100));
            uptr->u6 = 0;       /* Force read next record */
            break;
        }

    }
    return SCPE_OK;
}

t_stat
chron_reset(DEVICE * dptr)
{
    chron_unit[0].u5 = MT_RDY;
    return SCPE_OK;
}

/* Sets the address of the chrono clock */
t_stat
set_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
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
    if (i < 0 || i > 10)
        return SCPE_ARG;
    uptr->u3 = i;
    return SCPE_OK;
}

t_stat
get_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "Unit=%d", uptr->u3);
    return SCPE_OK;
}

t_stat
chron_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "Chronoclock\n\n");
  fprintf (st, "The Chronoclock replaces one of your tape drives, and is\n");
  fprintf (st, "for CTSS operation\n\n");
  fprintf (st, "   sim> SET %s ENABLE     to enable chronoclock\n", dptr->name);
  fprintf (st, "   sim> SET %s UNIT=#     sets unit to override [0-9]\n\n", dptr->name);
  help_set_chan_type(st, dptr, "Chrono clock");
  fprintf (st, "You must disable the corrosponding tape drive in order for\n");
  fprintf (st, "the chronoclook to be seen. The chronoclock replaces one of\n");
  fprintf (st, "your tape drives, and by reading the tape drive, it will\n");
  fprintf (st, "return a short record with the current date and time, no year\n");
  fprintf (st, "is returned\n");

  fprint_set_help (st, dptr) ;
  fprint_show_help (st, dptr) ;
  return SCPE_OK;
}

const char *
chron_description (DEVICE *dptr)
{
   return "Chronoclock";
}


#endif
