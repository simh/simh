/* b5500_mt.c: Burrioughs 5500 Magnetic tape controller

   Copyright (c) 2016, Richard Cornwell

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

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "b5500_defs.h"
#include "sim_tape.h"

#if (NUM_DEVS_MT > 0)

#define BUFFSIZE        10240
#define UNIT_MT         UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE
#define HT              500     /* Time per char high density */

/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5       Bits 30-16 of W */
#define URCSTA_SKIP     000017  /* Skip mask */
#define URCSTA_SINGLE   000020  /* Single space skip. */
#define URCSTA_DOUBLE   000040  /* Double space skip */
#define URCSTA_READ     000400  /* Read flag */
#define URCSTA_WC       001000  /* Use word count */
#define URCSTA_DIRECT   002000  /* Direction, Long line */
#define URCSTA_BINARY   004000  /* Binary transfer */
#define URCSTA_INHIBIT  040000  /* Inhibit transfer to memory */

#define MT_CHAN     0000003     /* Channel active on */
#define MT_BIN      0000004     /* Binary/BCD */
#define MT_BACK     0000010     /* Backwards */
#define MT_CMD      0000070     /* Command to tape drive */
#define MT_INT      0000010     /* Interrogate */
#define MT_RD       0000020     /* Reading */
#define MT_RDBK     0000030     /* Reading Backwards */
#define MT_WR       0000040     /* Writing */
#define MT_REW      0000050     /* Rewind */
#define MT_FSR      0000060     /* Space Forward */
#define MT_BSR      0000070     /* Space Backward record */
#define MT_RDY      0000100     /* Device is ready for command */
#define MT_IDLE     0000200     /* Tape still in motion */
#define MT_MARK     0001000     /* Hit tape mark */
#define MT_EOT      0002000     /* At End Of Tape */
#define MT_BOT      0004000     /* At Beginning Of Tape */
#define MT_EOR      0010000     /* Set EOR on next record */
#define MT_BSY      0020000     /* Tape busy after operation */
#define MT_LOADED   0040000     /* Tape loaded, return ready */


#define BUF_EMPTY(u) (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

t_stat              mt_srv(UNIT *);
t_stat              mt_attach(UNIT *, CONST char *);
t_stat              mt_detach(UNIT *);
t_stat              mt_reset(DEVICE *);
t_stat              mt_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *mt_description(DEVICE *dptr);

/* Channel level activity */
uint8               mt_chan[NUM_CHAN];

uint16              mt_busy = 0;        /* Busy bits */

/* One buffer per channel */
uint8               mt_buffer[NUM_CHAN][BUFFSIZE];

UNIT                mt_unit[] = {
/* Controller 1 */
#if (NUM_DEVS_MT > 0)
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 0 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 1 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 2 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 3 */
#if (NUM_DEVS_MT > 3)
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 4 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 5 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 6 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0},    /* 7 */
#if (NUM_DEVS_MT > 7)
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 8 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 9 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 10 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 11 */
#if (NUM_DEVS_MT > 11)
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 12 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 13 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 14 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_DIS, 0), 0},   /* 15 */
#endif
#endif
#endif
#endif
};

MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL, NULL, NULL,
     "Write ring in place"},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL, NULL, NULL,
     "no Write ring in place"},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL,
      "Set/Display tape format (SIMH, E11, TPC, P7B)" },
    {MTAB_XTD | MTAB_VUN, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL,
      "Set unit n capacity to arg MB (0 = unlimited)" },
    {MTAB_XTD | MTAB_VUN, 0, "DENSITY", "DENSITY",
     NULL, &sim_tape_show_dens, NULL},
    {0}
};


DEVICE              mt_dev = {
    "MT", mt_unit, NULL, mt_mod,
    NUM_DEVS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, NULL, &mt_attach, &mt_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL,
    &mt_description
};




/* Start off a mag tape command */
t_stat
mt_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT        *uptr;
    int         unit = dev >> 1;

    /* Make sure valid drive number */
    if (unit > NUM_DEVS_MT || unit < 0)
        return SCPE_NODEV;

    uptr = &mt_unit[unit];
    /* If unit disabled return error */
    if (uptr->flags & UNIT_DIS)
        return SCPE_NODEV;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;

    /* Not there until loading done */
    if ((uptr->u5 & MT_LOADED))
        return SCPE_UNATT;

    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & MT_BSY) != 0)
        return SCPE_BUSY;

    /* Determine actual command */
    uptr->u5 &= ~(MT_RDY|MT_CHAN|MT_CMD|MT_BIN);
    uptr->u5 |= chan;
    if (cmd & URCSTA_BINARY)
        uptr->u5 |= MT_BIN;
    if (cmd & URCSTA_READ) {
        if ((cmd & URCSTA_WC) && *wc == 0)
           uptr->u5 |= MT_FSR;
        else
           uptr->u5 |= MT_RD;
    } else {
        /* Erase gap not supported on sim, treat as
           write of null record */
        if ((cmd & URCSTA_WC) && *wc == 0)
           uptr->u5 |= MT_INT;
        else
           uptr->u5 |= MT_WR;
    }

    *wc = 0;    /* So no overide occurs */

    /* Convert command to correct type */ 
    if (cmd & URCSTA_DIRECT)
        uptr->u5 |= MT_BACK;
    uptr->u6 = 0;
    CLR_BUF(uptr);
    sim_debug(DEBUG_CMD, &mt_dev, "Command %d %o %o\n", unit, uptr->u5, cmd);
    if ((uptr->u5 & MT_IDLE) == 0) {
       sim_activate(uptr,50000);
    }
    return SCPE_OK;
}



/* Map simH errors into machine errors */
t_stat mt_error(UNIT * uptr, int chan, t_stat r, DEVICE * dptr)
{
    switch (r) {
    case MTSE_OK:               /* no error */
        sim_debug(DEBUG_EXP, dptr, "OK ");
        break;

    case MTSE_EOM:              /* end of medium */
        sim_debug(DEBUG_EXP, dptr, "EOT ");
        if (uptr->u5 & MT_BOT) {
           chan_set_blank(chan);
        } else {
           uptr->u5 &= ~MT_BOT;
           uptr->u5 |= MT_EOT;
           chan_set_eot(chan);
        }
        break;

    case MTSE_TMK:              /* tape mark */
        sim_debug(DEBUG_EXP, dptr, "MARK ");
        uptr->u5 &= ~(MT_BOT|MT_EOT);
        chan_set_eof(chan);
        break;

    case MTSE_WRP:              /* write protected */
        sim_debug(DEBUG_EXP, dptr, "WriteLocked ");
        chan_set_wrp(chan);
        break;

    case MTSE_INVRL:            /* invalid rec lnt */
    case MTSE_IOERR:            /* IO error */
    case MTSE_FMT:              /* invalid format */
    case MTSE_RECE:             /* error in record */
        chan_set_error(chan);   /* Force redundency error */
        sim_debug(DEBUG_EXP, dptr, "ERROR %d ", r);
        break;
    case MTSE_BOT:              /* beginning of tape */
        uptr->u5 &= ~MT_EOT;
        uptr->u5 |= MT_BOT;
        chan_set_bot(chan);     /* Set flag */
        sim_debug(DEBUG_EXP, dptr, "BOT ");
        break;
    case MTSE_UNATT:            /* unattached */
    default: 
        sim_debug(DEBUG_EXP, dptr, "%d ", r);
    }
    uptr->u5 &= ~(MT_CMD|MT_BIN);
    uptr->u5 |= MT_RDY|MT_IDLE;
    chan_set_end(chan);
    return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    int                 chan = uptr->u5 & MT_CHAN;
    int                 unit = uptr - mt_unit;
    int                 cmd = uptr->u5 & MT_CMD;
    DEVICE              *dptr = find_dev_from_unit(uptr);
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;   /* Force error if not set */
    uint8               ch;
    int                 mode;
    t_mtrlnt            loc;


    /* Simulate tape load delay */
    if (uptr->u5 & MT_LOADED) {
        uptr->u5 &= ~MT_LOADED;
        uptr->u5 |= MT_BSY|MT_RDY;
        sim_debug(DEBUG_DETAIL, dptr, "Unit=%d Loaded\n", unit);
        sim_activate(uptr, 50000);
        return SCPE_OK;
    }

    if (uptr->u5 & MT_BSY) {
        uptr->u5 &= ~MT_BSY;
        sim_debug(DEBUG_DETAIL, dptr, "Unit=%d Online\n", unit);
        iostatus |= 1 << (uptr - mt_unit);
        if (uptr->u5 & MT_IDLE)
           sim_activate(uptr, 50000);
        return SCPE_OK;
    }

    if (uptr->u5 & MT_IDLE) {
        uptr->u5 &= ~MT_IDLE;
        if (uptr->u5 & MT_RDY) {
           sim_debug(DEBUG_DETAIL, dptr, "Unit=%d idling\n", unit);
           return SCPE_OK;
        }
        sim_debug(DEBUG_DETAIL, dptr, "Unit=%d start %02o\n", unit, cmd);
    }

    switch (cmd) {
    /* Handle interrogate */
    case MT_INT: 
         if (sim_tape_wrp(uptr)) 
            chan_set_wrp(chan);
         uptr->u5 &= ~(MT_CMD|MT_BIN);
         uptr->u5 |= MT_RDY;
         chan_set_end(chan);
         sim_debug(DEBUG_DETAIL, dptr, "Status\n");
         return SCPE_OK;

    case  MT_RD:                /* Read */
        /* If at end of record, fill buffer */
        if (BUF_EMPTY(uptr)) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d %s ", unit,
                (uptr->u5 & MT_BIN)? "bin": "bcd");
            if (sim_tape_eot(uptr)) {
                sim_activate(uptr, 4000);
                return mt_error(uptr, chan, MTSE_EOM, dptr);
            }
            r = sim_tape_rdrecf(uptr, &mt_buffer[chan][0], &reclen, BUFFSIZE);
            if (r != MTSE_OK) {
                if (r == MTSE_TMK) {
                    sim_debug(DEBUG_DETAIL, dptr, "TM\n");
                    ch = 017;
                    (void)chan_write_char(chan, &ch, 1);
                    sim_activate(uptr, 4000);
                } else { 
                    sim_debug(DEBUG_DETAIL, dptr, "r=%d\n", r);
                    sim_activate(uptr, 5000);
                }
                return mt_error(uptr, chan, r, dptr);
            } else {
                uptr->u5 &= ~(MT_BOT|MT_EOT);
                uptr->hwmark = reclen;
            }
            sim_debug(DEBUG_DETAIL, dptr, "%d chars\n", uptr->hwmark);
            uptr->u6 = 0;
            if ((uptr->u5 & MT_BIN) == 0)
                mode = 0100;
            else
                mode = 0;
            for (loc = 0; loc < reclen; loc++) {
                ch = mt_buffer[chan][loc] & 0177;
                if (((parity_table[ch & 077]) ^ (ch & 0100) ^ mode) == 0) {
                    chan_set_error(chan);
                    break;
                }
            }
        }
        ch = mt_buffer[chan][uptr->u6++] & 0177;
        /* 00 characters are not transfered in BCD mode */
        if (ch == 0) {
              if (((uint32)uptr->u6) >= uptr->hwmark) {
                   sim_activate(uptr, 4000);
                   return mt_error(uptr, chan, MTSE_OK, dptr);
              } else {
                   sim_activate(uptr, HT);
                   return SCPE_OK;
              }
        }

        if (chan_write_char(chan, &ch, 
                             (((uint32)uptr->u6) >= uptr->hwmark) ? 1 : 0)) {
                sim_debug(DEBUG_DATA, dptr, "Read unit=%d %d EOR\n", unit,
                         uptr->hwmark-uptr->u6);
                sim_activate(uptr, 4000);
                return mt_error(uptr, chan, MTSE_OK, dptr);
        } else {
            sim_debug(DEBUG_DATA, dptr, "Read data unit=%d %d %03o\n",
                          unit, uptr->u6, ch);
            sim_activate(uptr, HT);
        }
        return SCPE_OK;

    case  MT_RDBK:              /* Read Backword */
        /* If at end of record, fill buffer */
        if (BUF_EMPTY(uptr)) {
            sim_debug(DEBUG_DETAIL, dptr, "Read back unit=%d %s ", unit,
                (uptr->u5 & MT_BIN)? "bin": "bcd");
            if (sim_tape_bot(uptr)) {
                sim_activate(uptr, 4000);
                return mt_error(uptr, chan, MTSE_BOT, dptr);
            }
            r = sim_tape_rdrecr(uptr, &mt_buffer[chan][0], &reclen, BUFFSIZE);
            if (r != MTSE_OK) {
                if (r == MTSE_TMK) {
                    sim_debug(DEBUG_DETAIL, dptr, "TM\n");
                    ch = 017;
                    (void)chan_write_char(chan, &ch, 1);
                    sim_activate(uptr, 4000);
                } else { 
                    uptr->u5 |= MT_BSY;
                    sim_debug(DEBUG_DETAIL, dptr, "r=%d\n", r);
                    sim_activate(uptr, 100);
                }
                return mt_error(uptr, chan, r, dptr);
            } else {
                uptr->u5 &= ~(MT_BOT|MT_EOT);
                uptr->hwmark = reclen;
            }
            sim_debug(DEBUG_DETAIL, dptr, "%d chars\n", uptr->hwmark);
            uptr->u6 = uptr->hwmark;
            if ((uptr->u5 & MT_BIN) == 0)
                mode = 0100;
            else
                mode = 0;
            for (loc = 0; loc < reclen; loc++) {
                ch = mt_buffer[chan][loc] & 0177;
                if (((parity_table[ch & 077]) ^ (ch & 0100) ^ mode) == 0) {
                    chan_set_error(chan);
                    break;
                }
            }
        }
        ch = mt_buffer[chan][--uptr->u6] & 0177;
        /* 00 characters are not transfered in BCD mode */
        if (ch == 0) {
              if (uptr->u6 <= 0) {
                    sim_activate(uptr, 4000);
                return mt_error(uptr, chan, MTSE_OK, dptr);
              } else {
                sim_activate(uptr, HT);
                return SCPE_OK;
              }
        }

        if (chan_write_char(chan, &ch, (uptr->u6 > 0) ? 0 : 1)) {
                sim_debug(DEBUG_DATA, dptr, "Read back unit=%d %d EOR\n",
                                 unit, uptr->hwmark-uptr->u6);
                sim_activate(uptr, 100);
                return mt_error(uptr, chan, MTSE_OK, dptr);
        } else {
            sim_debug(DEBUG_DATA, dptr, "Read  back data unit=%d %d %03o\n",
                          unit, uptr->u6, ch);
            sim_activate(uptr, HT);
        }
        return SCPE_OK;

    case MT_WR:                 /* Write */
        /* Check if write protected */
        if (uptr->u6 == 0 && sim_tape_wrp(uptr)) {
            sim_activate(uptr, 100);
            return mt_error(uptr, chan, MTSE_WRP, dptr);
        }
        if (chan_read_char(chan, &ch,
                          (uptr->u6 > BUFFSIZE) ? 1 : 0)) {
            reclen = uptr->u6;
            /* If no transfer, then either erase */
            if (reclen == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "Erase\n");
                 r = MTSE_OK;
            } else if ((reclen == 1) && (cmd & MT_BIN) == 0 &&
                 (mt_buffer[chan][0] == 017)) {
            /* Check if write rtape mark */
                 sim_debug(DEBUG_DETAIL, dptr, "Write Mark unit=%d\n", unit);
                 r = sim_tape_wrtmk(uptr);
            } else {
                sim_debug(DEBUG_DETAIL, dptr, 
                        "Write unit=%d Block %d %s chars\n", unit, reclen,
                                (uptr->u5 & MT_BIN)? "bin": "bcd");
                r = sim_tape_wrrecf(uptr, &mt_buffer[chan][0], reclen);
            }
            uptr->u5 &= ~(MT_BOT|MT_EOT);
            sim_activate(uptr, 4000);
            return mt_error(uptr, chan, r, dptr);       /* Record errors */
        } else {
            /* Copy data to buffer */
            ch &= 077;
            ch |= parity_table[ch];
            if ((uptr->u5 & MT_BIN)) 
                ch ^= 0100;
            /* Don't write out even parity zeros */
            if (ch != 0) 
                mt_buffer[chan][uptr->u6++] = ch;
            sim_debug(DEBUG_DATA, dptr, "Write data unit=%d %d %03o\n",
                      unit, uptr->u6, ch);
            uptr->hwmark = uptr->u6;
        }
        sim_activate(uptr, HT);
        return SCPE_OK;

    case  MT_FSR:               /* Space forward one record */
        if (BUF_EMPTY(uptr)) {
            /* If at end of record, fill buffer */
            sim_debug(DEBUG_DETAIL, dptr, "Space unit=%d ", unit);
            if (sim_tape_eot(uptr)) {
                uptr->u5 &= ~MT_BOT;
                sim_debug(DEBUG_DETAIL, dptr, "EOT\n");
                sim_activate(uptr, 4000);
                return mt_error(uptr, chan, MTSE_EOM, dptr);
            }
            r = sim_tape_rdrecf(uptr, &mt_buffer[chan][0], &reclen, BUFFSIZE);
            if (r != MTSE_OK) {
                if (r == MTSE_TMK) {
                    sim_debug(DEBUG_DETAIL, dptr, "TM ");
                    reclen = 1;
                    chan_set_eof(chan);
                } else { 
                    sim_debug(DEBUG_DETAIL, dptr, "r=%d ", r);
                    reclen = 10;
                }
            }
            uptr->u5 &= ~(MT_BOT|MT_EOT);
            uptr->hwmark = reclen;
            sim_debug(DEBUG_DETAIL, dptr, "%d chars\n", uptr->hwmark);
            sim_activate(uptr, uptr->hwmark * HT);
            return SCPE_OK;
        }
        sim_activate(uptr, 4000);
        return mt_error(uptr, chan, MTSE_OK, dptr);

    case  MT_BSR:               /* Backspace record */
        if (BUF_EMPTY(uptr)) {
            /* If at end of record, fill buffer */
            sim_debug(DEBUG_DETAIL, dptr, "backspace unit=%d ", unit);
            if (sim_tape_bot(uptr)) {
                sim_debug(DEBUG_DETAIL, dptr, "BOT\n");
                sim_activate(uptr, 100);
                return mt_error(uptr, chan, MTSE_BOT, dptr);
            }
            r = sim_tape_rdrecr(uptr, &mt_buffer[chan][0], &reclen, BUFFSIZE);
            if (r != MTSE_OK) {
                if (r == MTSE_TMK) {
                    sim_debug(DEBUG_DETAIL, dptr, "TM ");
                    reclen = 1;
                    chan_set_eof(chan);
                } else { 
                    reclen = 10;
                    sim_debug(DEBUG_DETAIL, dptr, "r=%d ", r);
                }
            }
            uptr->u5 &= ~(MT_BOT|MT_EOT);
            uptr->hwmark = reclen;
            sim_debug(DEBUG_DETAIL, dptr, "%d chars\n", uptr->hwmark);
            sim_activate(uptr, uptr->hwmark * HT);
            return SCPE_OK;
        }
        sim_activate(uptr, 4000);
        return mt_error(uptr, chan, MTSE_OK, dptr);

    case MT_REW:                /* Rewind */
        sim_debug(DEBUG_DETAIL, dptr, "Rewind unit=%d pos=%d\n", unit,
                        uptr->pos);
        uptr->u5 &= ~(MT_CMD | MT_BIN | MT_IDLE | MT_RDY);
        uptr->u5 |= MT_BSY|MT_RDY;
        iostatus &= ~(1 << (uptr - mt_unit));
        sim_activate(uptr, (uptr->pos/100) + 100);
        r = sim_tape_rewind(uptr);
        uptr->u5 &= ~MT_EOT;
        uptr->u5 |= MT_BOT;
        chan_set_end(chan);
        return r;
    }
    return mt_error(uptr, chan, r, dptr);
}


t_stat
mt_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 |= MT_LOADED|MT_BOT; 
    sim_activate(uptr, 50000);
    return SCPE_OK;
}

t_stat
mt_detach(UNIT * uptr)
{
    uptr->u5 = 0;
    iostatus &= ~(1 << (uptr - mt_unit));
    return sim_tape_detach(uptr);
}

t_stat
mt_reset(DEVICE *dptr)
{
    int i;

    /* Scan all devices and enable those that 
       are loaded. This is to allow tapes that 
       are mounted prior to boot to be recognized
       at later. Also disconnect all devices no
       longer connected. */
    for ( i = 0; i < NUM_DEVS_MT; i++) {
        mt_unit[i].dynflags = MT_DENS_556 << UNIT_V_DF_TAPE;
        if ((mt_unit[i].flags & UNIT_ATT) == 0)
            iostatus &= ~(1 << i);
        else if (mt_unit[i].u5 & (MT_LOADED|MT_RDY)) {
            iostatus |= 1 << i;
            mt_unit[i].u5 &= ~(MT_LOADED);
            mt_unit[i].u5 |= MT_RDY;
        }
    }
    return SCPE_OK;
}

t_stat
mt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "B422/B423 Magnetic tape unit\n\n");
   fprintf (st, "The magnetic tape controller assumes that all tapes are 7 track\n");
   fprintf (st, "with valid parity. Tapes are assumed to be 555.5 characters per\n");
   fprintf (st, "inch. To simulate a standard 2400foot tape, do:\n");
   fprintf (st, "    sim> SET MTn LENGTH 15\n\n");
   fprintf (st, "By default only 8 drives are enabled, additional units up to 15 supported.\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
mt_description(DEVICE *dptr)
{
   return "B422/B423 Magnetic tape unit";
}
#endif


