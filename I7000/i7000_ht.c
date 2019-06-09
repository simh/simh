/* i7090_ht.c: ibm 7090 hypertape

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

   Support for 7640 hypertape

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

   Hypertape orders appear to be of the following formats. Since there
   is no documentation on them, I can going with what is shown in the
   IBSYS sources.

   BCD translated:
   CTLW/CTLR     06u01      where u is unit numder
   CTLW/CTLR     07u01      Backwords reading, where u is unit numder
   CTL           06uoo01    Where u is unit number, and oo is order code
                                3x or 42
*/

#include "i7000_defs.h"
#include "sim_tape.h"

#ifdef NUM_DEVS_HT
#define BUFFSIZE        (MAXMEMSIZE * CHARSPERWORD)

#define UNIT_HT(x)      UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE|UNIT_S_CHAN(x)| \
                        UNIT_SELECT

#if defined(HTSIZE)
#undef HTSIZE
#endif
#define HTSIZE          31731000

/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5 */
#define HT_CMDMSK   00000077    /* Command being run */
#define HT_NOTRDY   00000100    /* Devices is running a command */
#define HT_IDLE     00000200    /* Tape still in motion */
#define HT_MARK     00000400    /* Hit tape mark */
#define HT_EOR      00001000    /* Hit end of record */
#define HT_ERR      00002000    /* Device recieved error */
#define HT_BOT      00004000    /* Unit at begining of tape */
#define HT_EOT      00010000    /* Unit at end of tape */
#define HT_ATTN     00020000    /* Unit requests attntion */
#define HT_MOVE     00040000    /* Unit is moving to new record */
#define HT_WRITE    00100000    /* Were we writing */
#define HT_SNS      00200000    /* We were doing sense */
#define HT_CMD      00400000    /* We are fetching a command */
#define HT_PEND     01000000    /* Hold channel while command runs */

/* Hypertape commands */
#define HNOP            0x00    /* Nop */
#define HEOS            0x01    /* End of sequence */
#define HRLF            0x02    /* Reserved Light Off */
#define HRLN            0x03    /* Reserved Light On */
#define HCLF            0x04    /* Check light off? Not documented by might
                                   be valid command */
#define HCLN            0x05    /* Check light on */
#define HSEL            0x06    /* Select */
#define HSBR            0x07    /* Select for backwards reading */
#define HCCR            0x28    /* Change cartridge and rewind */
#define HRWD            0x30    /* Rewind */
#define HRUN            0x31    /* Rewind and unload */
#define HERG            0x32    /* Erase long gap */
#define HWTM            0x33    /* Write tape mark */
#define HBSR            0x34    /* Backspace */
#define HBSF            0x35    /* Backspace file */
#define HSKR            0x36    /* Space */
#define HSKF            0x37    /* Space file */
#define HCHC            0x38    /* Change Cartridge */
#define HUNL            0x39    /* Unload Cartridge */
#define HFPN            0x42    /* File Protect On */

/* Hypertape sense word 1 codes */
                     /*   01234567 */
#define SEL_MASK        0x0F000000      /* Selected unit mask */
#define STAT_NOTRDY     0x80800000      /* Drive not ready */
#define PROG_NOTLOAD    0x40400000      /* *Drive not loaded */
#define PROG_FILEPROT   0x40200000      /* Drive write protected */
#define PROG_INVCODE    0x40080000      /* Invalid code */
#define PROG_BUSY       0x40040000      /* Drive Busy */
#define PROG_BOT        0x40020000      /* Drive at BOT BSR/BSF requested */
#define PROG_EOT        0x40010000      /* Drive at EOT forward motion requested. */
#define DATA_CHECK      0x20008000      /* *Error corrected */
#define DATA_PARITY     0x20004000      /* *Parity error */
#define DATA_CODECHK    0x20002000      /* *Code check */
#define DATA_ENVCHK     0x20001000      /* *Envelop error */
#define DATA_RESPONSE   0x20000800      /* Response check */
#define DATA_EXECSKEW   0x20000400      /* *Excessive skew check */
#define DATA_TRACKSKEW  0x20000200      /* *Track skew check */
#define EXP_MARK        0x10000080      /* Drive read a mark */
#define EXP_EWA         0x10000040      /* *Drive near EOT */
#define EXP_NODATA      0x10000020      /* *No data transfered */
#define READ_BSY        0x00000008      /* *Controller reading */
#define WRITE_BSY       0x00000004      /* *Controller writing */
#define BACK_MODE       0x00000002      /* *Backwards mode */

uint32              ht_cmd(UNIT *, uint16, uint16);
t_stat              ht_srv(UNIT *);
t_stat              htc_srv(UNIT *);
void                ht_tape_cmd(DEVICE *, UNIT *);
t_stat              ht_error(UNIT *, int, t_stat);
t_stat              ht_boot(int32, DEVICE *);
t_stat              ht_reset(DEVICE *);
t_stat              ht_attach(UNIT *, CONST char *);
t_stat              ht_detach(UNIT *);
t_stat              ht_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *ht_description (DEVICE *dptr);
void                ht_tape_posterr(UNIT * uptr, uint32 error);



/* One buffer per channel */
uint8               ht_unit[NUM_CHAN * 2];      /* Currently selected unit */
uint8               ht_buffer[NUM_DEVS_HT+1][BUFFSIZE];
int                 ht_cmdbuffer[NUM_CHAN];     /* Buffer holding command ids */
int                 ht_cmdcount[NUM_CHAN];      /* Count of command digits recieved */
uint32              ht_sense[NUM_CHAN * 2];     /* Sense data for unit */

UNIT                hta_unit[] = {
/* Controller 1 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 0 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 1 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 2 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 3 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 4 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 5 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 6 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 7 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 8 */
    {UDATA(&ht_srv, UNIT_HT(5), HTSIZE)},       /* 9 */
    {UDATA(&htc_srv, UNIT_S_CHAN(5)|UNIT_DISABLE|UNIT_DIS, 0)}, /* Controller */
#if NUM_DEVS_HT > 1
/* Controller 2 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 0 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 1 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 2 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 3 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 4 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 5 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 6 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 7 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 8 */
    {UDATA(&ht_srv, UNIT_HT(8), HTSIZE)},       /* 9 */
    {UDATA(&htc_srv, UNIT_S_CHAN(8)|UNIT_DISABLE|UNIT_DIS, 0)}, /* Controller */
#endif
};

MTAB                ht_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL, NULL, NULL,
     "Write ring in place"},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL, NULL, NULL,
     "no Write ring in place"},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL,
      "Set/Display tape format (SIMH, E11, TPC, P7B)" },
    {MTAB_XTD | MTAB_VUN, 0, "LENGTH", "LENGTH",
     NULL, &sim_tape_show_capac, NULL,
      "Set unit n capacity to arg MB (0 = unlimited)" },
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan, &get_chan,
     NULL, "Set Channel for device"},
#ifndef I7010   /* Not sure 7010 ever supported hypertapes */
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "SELECT", "SELECT",
     &chan9_set_select, &chan9_get_select, NULL,
       "Set unit number"},
#endif
    {0}
};

DEVICE              hta_dev = {
    "HTA", hta_unit, NULL, ht_mod,
    NUM_UNITS_HT + 1, 8, 15, 1, 8, 8,
    NULL, NULL, &ht_reset, &ht_boot, &ht_attach, &ht_detach,
    &ht_dib, DEV_BUF_NUM(0) | DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &ht_help, NULL, NULL, &ht_description
};

#if NUM_DEVS_HT > 1
DEVICE              htb_dev = {
    "HTB", &hta_unit[NUM_UNITS_HT + 1], NULL, ht_mod,
    NUM_UNITS_HT + 1, 8, 15, 1, 8, 8,
    NULL, NULL, &ht_reset, &ht_boot, &ht_attach, &ht_detach,
    &ht_dib, DEV_BUF_NUM(1) | DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &ht_help, NULL, NULL, &ht_description
};
#endif


uint32 ht_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 chan = UNIT_G_CHAN(dptr->units[0].flags);
    UNIT               *u = &dptr->units[NUM_UNITS_HT];

    /* Activate the device to start doing something */
    ht_cmdbuffer[chan] = 0;
    ht_cmdcount[chan] = 0;
    sim_activate(u, 10);
    return SCPE_OK;
}

t_stat htc_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 chan = UNIT_G_CHAN(dptr->units[0].flags);
    int                 sel;
    int                 schan;

    sel = (dptr->units[0].flags & UNIT_SELECT) ? 1 : 0;
    if (sel != chan_test(chan, CTL_SEL))
        return SCPE_OK;
    schan = (chan * 2) + sel;
    /* Drive is busy */
    if (uptr->u5 & HT_NOTRDY) {
        sim_debug(DEBUG_EXP, dptr, "Controller busy\n");
        return SCPE_OK;
    }

    /* Handle sense on unit */
    if (chan_test(chan, CTL_SNS)) {
        uint8           ch = 0;
        int             eor = 0;
        int             i;
        UNIT           *up;
        switch(ht_cmdcount[chan]) {
        case 0: ch = (ht_sense[schan] >> 24) & 0xF;
                uptr->u5 |= HT_SNS;
                chan9_clear_error(chan, sel);
                sim_debug(DEBUG_SNS, dptr, "Sense %08x\n", ht_sense[schan]);
                break;
        case 1: ch = ht_unit[schan];
                break;
        case 2: case 3: case 4: case 5: case 6: case 7:
                ch = (ht_sense[schan] >> (4 * (7 - ht_cmdcount[chan]))) & 0xF;
                break;
        case 10:
                eor = DEV_REOR;
                /* Fall through */
        case 9:
        case 8:
                i = 4 * (ht_cmdcount[chan] - 8);
                up = &dptr->units[i];
                ch = 0;
                for (i = 3; i >= 0; i--, up++) {
                    if (up->u5 & HT_ATTN)
                        ch |= 1 << i;
                }
                break;
        }

        /* Fix out of align bit */
        if (ch & 010)
            ch ^= 030;

        sim_debug(DEBUG_DATA, dptr, "sense %d %02o ", ht_cmdcount[chan], ch);
        ht_cmdcount[chan]++;
        switch(chan_write_char(chan, &ch, eor)) {
        case TIME_ERROR:
        case END_RECORD:
            ht_sense[schan] = 0;
            /* Fall through */
        case DATA_OK:
            uptr->u5 |= HT_SNS; /* So we catch disconnect */
            if (eor) {
               ht_sense[schan] = 0;
               for (up = dptr->units, i = NUM_UNITS_HT; i >= 0; i--, up++)
                    up->u5 &= ~HT_ATTN;
            }
            break;
        }
        sim_activate(uptr, us_to_ticks(50));
        return SCPE_OK;
    }

    /* If control, go collect it */
    if (chan_test(chan, CTL_CNTL)) {
        uptr->u5 |= HT_CMD;
        ht_tape_cmd(dptr, uptr);
        sim_activate(uptr, us_to_ticks(50));
        return SCPE_OK;
    }

    /* Channel has disconnected, abort current operation. */
    if (uptr->u5 & (HT_SNS|HT_CMD) && chan_stat(chan, DEV_DISCO)) {
        uptr->u5 &= ~(HT_SNS|HT_CMD);
        chan_clear(chan, DEV_WEOR|DEV_SEL);
        sim_debug(DEBUG_CHAN, dptr, "control disconnecting\n");
    }
    return SCPE_OK;
}

t_stat ht_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    UNIT               *ctlr = &dptr->units[NUM_UNITS_HT];
    int                 sel;
    int                 schan;
    t_stat              r;
    t_mtrlnt            reclen;

    sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;

    /* Handle seeking */
    if (uptr->wait > 0) {
        uptr->wait--;
        if (uptr->wait == 0) {
            if (uptr->u5 & HT_PEND) {
                chan_set(chan, DEV_REOR|CTL_END);
                ctlr->u5 &= ~(HT_NOTRDY);
                sim_activate(ctlr, us_to_ticks(50));    /* Schedule control to disco */
            } else {
                uptr->u5 |= HT_ATTN;
                chan9_set_attn(chan, sel);
            }
            uptr->u5 &= ~(HT_PEND | HT_NOTRDY | HT_CMDMSK);
            sim_debug(DEBUG_DETAIL, dptr, "%d Seek done\n", unit);
        } else
            sim_activate(uptr, us_to_ticks(1000));
        return SCPE_OK;
    }

    if (sel != chan_test(chan, CTL_SEL))
        return SCPE_OK;

    /* Channel has disconnected, abort current operation. */
    if ((uptr->u5 & HT_CMDMSK) == HSEL && chan_stat(chan, DEV_DISCO)) {
        if (uptr->u5 & HT_WRITE) {
            sim_debug(DEBUG_CMD, dptr,
                      "Write flush Block %d chars %d words\n", uptr->u6,
                      uptr->u6 / 6);
            r = sim_tape_wrrecf(uptr, &ht_buffer[GET_DEV_BUF(dptr->flags)][0],
                        uptr->u6);
            uptr->u5 &= ~HT_WRITE;
            if (r != MTSE_OK) {
                ht_error(uptr, schan, r);
                chan9_set_attn(chan, sel);
            }
            uptr->u6 = 0;
        }
        uptr->u5 &= ~(HT_NOTRDY | HT_CMDMSK);
        ctlr->u5 &= ~(HT_NOTRDY);
        chan_clear(chan, DEV_WEOR|DEV_SEL);
        sim_debug(DEBUG_CHAN, dptr, "disconnecting\n");
        return SCPE_OK;
    }

    /* Handle writing of data */
    if (chan_test(chan, CTL_WRITE) && (uptr->u5 & HT_CMDMSK) == HSEL) {
        uint8               ch;

        if (uptr->u6 == 0 && sim_tape_wrp(uptr)) {
            ctlr->u5 &= ~(HT_NOTRDY);
            ht_tape_posterr(uptr, PROG_FILEPROT);
            sim_activate(uptr, us_to_ticks(50));
            return SCPE_OK;
        }

        switch(chan_read_char(chan, &ch, 0)) {
        case TIME_ERROR:
                ht_tape_posterr(uptr, DATA_RESPONSE);
                break;
        case DATA_OK:
                uptr->u5 |= HT_WRITE|HT_NOTRDY;
                ctlr->u5 |= HT_NOTRDY;
                ht_buffer[GET_DEV_BUF(dptr->flags)][uptr->u6++] = ch;
                sim_debug(DEBUG_DATA, dptr, " write %d \n", ch);
                if (uptr->u6 < BUFFSIZE)
                    break;
                /* Overran tape buffer, give error */
                ht_tape_posterr(uptr, DATA_TRACKSKEW);
        case END_RECORD:
                if (uptr->u6 != 0) {
                    sim_debug(DEBUG_CMD, dptr,
                          " Write Block %d chars %d words\n", uptr->u6,
                          uptr->u6 / 6);
                    r = sim_tape_wrrecf(uptr,
                                &ht_buffer[GET_DEV_BUF(dptr->flags)][0],
                                 uptr->u6);
                    uptr->u5 &= ~HT_WRITE;
                    uptr->u6 = 0;
                    if (r != MTSE_OK) {
                        ht_error(uptr, schan, r);
                        chan9_set_error(chan, SNS_UEND);
                    }
                }
                chan_set(chan, DEV_REOR|CTL_END);
        }
        sim_activate(uptr, us_to_ticks(20));
        return SCPE_OK;
    }

    /* Handle reading of data */
    if (chan_test(chan, CTL_READ) && (uptr->u5 & HT_CMDMSK) == (HSEL)) {
        uint8           ch;

        if (uptr->u6 == 0) {
            if (ht_sense[schan] & BACK_MODE)
                r = sim_tape_rdrecr(uptr,
                        &ht_buffer[GET_DEV_BUF(dptr->flags)][0],
                         &reclen, BUFFSIZE);
            else
                r = sim_tape_rdrecf(uptr,
                        &ht_buffer[GET_DEV_BUF(dptr->flags)][0],
                         &reclen, BUFFSIZE);
            if (r == MTSE_TMK)
                sim_debug(DEBUG_CMD, dptr, "Read Mark\n");
            else
                sim_debug(DEBUG_CMD, dptr, "Read %d bytes\n", reclen);
            /* Handle EOM special */
            if (r == MTSE_EOM && (uptr->u5 & HT_EOT) == 0) {
                uptr->u5 |= HT_EOT;
                ht_sense[schan] |= EXP_NODATA;
                chan_set(chan, DEV_REOR|CTL_END);
                chan9_set_error(chan, SNS_UEND);
                ctlr->u5 &= ~HT_NOTRDY;
                sim_activate(uptr, us_to_ticks(20));
                return SCPE_OK;
            } else
            /* Not read ok, return error */
                if (r != MTSE_OK) {
                    ht_error(uptr, schan, r);
                    chan_set(chan, DEV_REOR|CTL_END);
                    chan9_set_error(chan, SNS_UEND);
                    ctlr->u5 &= ~HT_NOTRDY;
                    uptr->wait = 0;
                    sim_activate(uptr, us_to_ticks(50));
                    return SCPE_OK;
                }
            uptr->hwmark = reclen;
            uptr->u5 |= HT_NOTRDY;
            ctlr->u5 |= HT_NOTRDY;
        }

        if (uptr->u6 > (int32)uptr->hwmark) {
            chan_set(chan, DEV_REOR|CTL_END);
            sim_activate(uptr, us_to_ticks(50));
            return SCPE_OK;
        }
        ch = ht_buffer[GET_DEV_BUF(dptr->flags)][uptr->u6++];
        sim_debug(DEBUG_DATA, dptr, "data %02o\n", ch);
        switch(chan_write_char(chan, &ch,
                                (uptr->u6 > (int32)uptr->hwmark)?DEV_REOR:0)) {
        case TIME_ERROR:
            /* Nop flag as timming error */
            ht_tape_posterr(uptr, DATA_RESPONSE);
            break;
        case END_RECORD:
            sim_debug(DEBUG_DATA, dptr, "eor\n");
            chan_set(chan, DEV_REOR|CTL_END);
        case DATA_OK:
            break;
        }
        sim_activate(uptr, us_to_ticks(20));
        return SCPE_OK;
    }

    /* If we have a command, keep scheduling us. */
    if ((uptr->u5 & HT_CMDMSK) == (HSEL))
         sim_activate(uptr, us_to_ticks(50));
    return SCPE_OK;
}

/* Post a error on a given unit. */
void
ht_tape_posterr(UNIT * uptr, uint32 error)
{
    int                 chan;
    int                 schan;
    int                 sel;

    chan = UNIT_G_CHAN(uptr->flags);
    sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    schan = (chan * 2) + sel;
    uptr->u5 |= HT_ATTN;
    ht_sense[schan] = error;
    chan_set(chan, DEV_REOR|CTL_END);
    chan9_set_attn(chan, sel);
    if (error != 0)
        chan9_set_error(chan, SNS_UEND);
}

/* Convert error codes to sense codes */
t_stat ht_error(UNIT * uptr, int schan, t_stat r)
{
    switch (r) {
    case MTSE_OK:               /* no error */
        break;

    case MTSE_TMK:              /* tape mark */
        uptr->u5 |= HT_MARK;
        ht_sense[schan] |= EXP_MARK;
        break;

    case MTSE_WRP:              /* write protected */
        uptr->u5 |= HT_ATTN;
        ht_sense[schan] |= PROG_FILEPROT;
        break;

    case MTSE_UNATT:            /* unattached */
        uptr->u5 |= HT_ATTN;
        ht_sense[schan] = PROG_NOTLOAD;
        break;

    case MTSE_IOERR:            /* IO error */
    case MTSE_INVRL:            /* invalid rec lnt */
    case MTSE_FMT:              /* invalid format */
    case MTSE_RECE:             /* error in record */
        uptr->u5 |= HT_ERR;
        ht_sense[schan] |= DATA_CODECHK;
        break;
    case MTSE_BOT:              /* beginning of tape */
        uptr->u5 |= HT_BOT;
        ht_sense[schan] |= PROG_BOT;
        break;
    case MTSE_EOM:              /* end of medium */
        uptr->u5 |= HT_EOT;
        ht_sense[schan] |= PROG_EOT;
        break;
    default:                    /* Anything else if error */
        ht_sense[schan] = PROG_INVCODE;
        break;
    }
    return SCPE_OK;
}

/* Process command */
void
ht_tape_cmd(DEVICE * dptr, UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    int                 schan = (chan * 2) + sel;
    UNIT               *up;
    uint8               c;
    int                 i;
    int                 t;
    int                 cmd;
    int                 unit;
    t_stat              r;
    t_mtrlnt            reclen;

    /* Get information on unit */

    /* Check if we have a command yet */
    switch(chan_read_char(chan, &c, 0)) {
    case END_RECORD:
    case TIME_ERROR:
        return;
    case DATA_OK:
        break;
    }

    c &= 017;
    if (c == 012)
        c = 0;
    ht_cmdbuffer[chan] <<= 4;
    ht_cmdbuffer[chan] |= c;
    ht_cmdcount[chan]++;
    /* If did not find end of sequence, request more */
    if ((ht_cmdbuffer[chan] & 0xff) != HEOS) {
        if (ht_cmdcount[chan] >= 8) {
            /* command error */
            ht_cmdcount[chan] = 0;
            ht_sense[schan] = PROG_INVCODE;
            ht_unit[schan] = 0;
            chan_set(chan, DEV_REOR|SNS_UEND);
            uptr->u5 &= ~HT_CMD;
        }
        return;
    }

    sim_debug(DEBUG_DETAIL, dptr, " cmd = %08x %d nybbles ",
              ht_cmdbuffer[chan], ht_cmdcount[chan]);

    /* See if we have a whole command string yet */
    uptr->u5 &= ~HT_CMD;
    cmd = 0xff;
    unit = NUM_UNITS_HT + 1;
    for (i = ht_cmdcount[chan] - 2; i >= 2; i -= 2) {
        t = (ht_cmdbuffer[chan] >> (i * 4)) & 0xff;
        switch (t) {
        case HSEL:              /* Select */
        case HSBR:              /* Select for backwards reading */
            i--;
            unit = (ht_cmdbuffer[chan] >> (i * 4)) & 0xf;
            ht_sense[schan] = 0;        /* Clear sense codes */
            cmd = t;
            break;
        case HEOS:              /* End of sequence */
            break;
        case HRLF:              /* Reserved Light Off */
        case HRLN:              /* Reserved Light On */
        case HCLF:              /* Check light off */
        case HCLN:              /* Check light on */
        case HNOP:              /* Nop */
        case HCCR:              /* Change cartridge and rewind */
        case HRWD:              /* Rewind */
        case HRUN:              /* Rewind and unload */
        case HERG:              /* Erase long gap */
        case HWTM:              /* Write tape mark */
        case HBSR:              /* Backspace */
        case HBSF:              /* Backspace file */
        case HSKR:              /* Space */
        case HSKF:              /* Space file */
        case HCHC:              /* Change Cartridge */
        case HUNL:              /* Unload Cartridge */
        case HFPN:              /* File Protect On */
            if (cmd != HSEL)
                cmd = 0xff;
            else
                cmd = t;
            break;
        default:
            sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "Invalid command %x\n",
                  cmd);
            ht_sense[schan] = PROG_INVCODE;
            chan_set(chan, DEV_REOR|CTL_END);
            chan9_set_error(chan, SNS_UEND);
            return;
        }
    }

    /* Ok got a full command */
    ht_cmdcount[chan] = 0;

    /* Make sure we got a unit and command */
    if (unit <= NUM_UNITS_HT)
        ht_unit[schan] = unit;
    else {
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr,
                  "Invalid unit %d cmd=%x\n", unit, cmd);
        ht_sense[schan] = STAT_NOTRDY;
        chan_set(chan, DEV_REOR|CTL_END);
        chan9_set_error(chan, SNS_UEND);
        return;
    }

    if (cmd == 0xff) {
        /* command error */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "Invalid command %x\n",
                  cmd);
        ht_sense[schan] = PROG_INVCODE;
        chan_set(chan, DEV_REOR|CTL_END);
        chan9_set_error(chan, SNS_UEND);
        return;
    }

    /* Find real device this command is for */
    up = &dptr->units[unit];
    if ((up->flags & UNIT_ATT) == 0) {
        /* Not attached! */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "Not ready %d cmd=%x\n",
                  unit, cmd);
        ht_sense[schan] = STAT_NOTRDY;
        chan_set(chan, DEV_REOR|CTL_END);
        chan9_set_error(chan, SNS_UEND);
        return;
    }

    if (up->u5 & HT_NOTRDY || up->wait > 0) {
        /* Unit busy */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "Busy unit %d cmd=%x\n", unit,
                  cmd);
        ht_sense[schan] = PROG_BUSY;
        chan_set(chan, DEV_REOR|CTL_END);
        chan9_set_error(chan, SNS_UEND);
        return;
    }
    sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "Execute unit %d cmd=%x ",
              unit, cmd);

    /* Ok, unit is ready and not in motion, set up to run command */
    up->u5 &= ~(HT_PEND | HT_MARK | HT_ERR | HT_CMDMSK);
    up->wait = 0;
    up->u5 |= cmd;
    ht_sense[schan] &= ~BACK_MODE;
    r = MTSE_OK;
    switch (cmd) {
    case HSBR:                  /* Select for backwards reading */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "HSBR\n");
        up->hwmark = -1;
        up->u6 = 0;
        ht_sense[schan] |= BACK_MODE;
        up->u5 &= ~(HT_CMDMSK);
        up->u5 |= HSEL;
        chan_set(chan, DEV_REOR|DEV_SEL);
        break;

    case HSEL:                  /* Select */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "HSEL\n");
        up->hwmark = -1;
        up->u6 = 0;
        chan_set(chan, DEV_REOR|DEV_SEL);
        break;

    case HRLF:                  /* Reserved Light Off */
    case HRLN:                  /* Reserved Light On */
    case HCLF:                  /* Check light off */
    case HCLN:                  /* Check light on */
    case HFPN:                  /* File Protect On (Nop for now ) */
    case HEOS:                  /* End of sequence */
    case HNOP:                  /* Nop */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "NOP\n");
        up->u5 &= ~(HT_NOTRDY | HT_CMDMSK);
        chan_set(chan, DEV_REOR|CTL_END);
        return;

    case HRWD:                  /* Rewind */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "REW\n");
        if (up->u5 & HT_BOT) {
            r = MTSE_OK;
            up->wait = 1;
        } else {
            r = sim_tape_rewind(up);
            up->u5 &= ~HT_EOT;
            up->wait = 500;
        }
        up->u5 |= HT_BOT|HT_NOTRDY;
        chan_set(chan, DEV_REOR|CTL_END);
        break;

    case HERG:                  /* Erase long gap */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "ERG\n");
        if (sim_tape_wrp(up)) {
            r = MTSE_WRP;
        } else {
            up->wait = 10;
            up->u5 |= HT_PEND|HT_NOTRDY;
            uptr->u5 |= HT_NOTRDY;
            up->u5 &= ~HT_BOT;
        }
        break;

    case HWTM:                  /* Write tape mark */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "WTM\n");
        if (sim_tape_wrp(up)) {
            r = MTSE_WRP;
        } else {
            r = sim_tape_wrtmk(up);
            up->wait = 5;
            up->u5 |= HT_PEND|HT_NOTRDY;
            up->u5 &= ~(HT_BOT|HT_EOT);
            uptr->u5 |= HT_NOTRDY;
        }
        break;

    case HBSR:                  /* Backspace */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "BSR\n");
        if (sim_tape_bot(up)) {
            r = MTSE_BOT;
            break;
        }
        r = sim_tape_sprecr(up, &reclen);
        up->wait = reclen / 100;
        up->wait += 2;
        up->u5 |= HT_PEND|HT_NOTRDY;
        up->u5 &= ~(HT_BOT|HT_EOT);
        uptr->u5 |= HT_NOTRDY;
        if (r == MTSE_TMK) {
            r = MTSE_OK;
            up->u5 |= HT_MARK;
        }
        if (sim_tape_bot(up))
            up->u5 |= HT_BOT;
        else
            up->u5 &= ~HT_BOT;
        break;

    case HBSF:                  /* Backspace file */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "BSF\n");
        if (sim_tape_bot(up)) {
            r = MTSE_BOT;
            break;
        }
        while ((r = sim_tape_sprecr(up, &reclen)) == MTSE_OK) {
            up->wait += reclen;
        }
        up->wait /= 100;
        up->wait += 2;
        up->u5 |= HT_PEND|HT_NOTRDY;
        up->u5 &= ~(HT_BOT|HT_EOT);
        uptr->u5 |= HT_NOTRDY;
        if (r == MTSE_TMK) {
            r = MTSE_OK;
            up->u5 |= HT_MARK;
        }
        if (sim_tape_bot(up))
            up->u5 |= HT_BOT;
        else
            up->u5 &= ~HT_BOT;
        break;

    case HSKR:                  /* Space */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "SKR\n");
        r = sim_tape_sprecf(up, &reclen);
        up->u5 |= HT_PEND|HT_NOTRDY;
        uptr->u5 |= HT_NOTRDY;
        if (r == MTSE_TMK) {
            r = MTSE_OK;
            up->u5 |= HT_MARK;
        }
        up->wait = reclen / 100;
        up->wait += 2;
        up->u5 &= ~HT_BOT;
        break;

    case HSKF:                  /* Space file */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "SKF\n");
        while ((r = sim_tape_sprecf(up, &reclen)) == MTSE_OK) {
            up->wait += reclen;
        }
        up->wait /= 100;
        up->wait += 2;
        up->u5 |= HT_PEND|HT_NOTRDY;
        uptr->u5 |= HT_NOTRDY;
        if (r == MTSE_TMK) {
            r = MTSE_OK;
            up->u5 |= HT_MARK;
        }
        up->u5 &= ~HT_BOT;
        break;

    case HCCR:                  /* Change cartridge and rewind */
    case HRUN:                  /* Rewind and unload */
    case HCHC:                  /* Change Cartridge */
    case HUNL:                  /* Unload Cartridge */
        sim_debug((DEBUG_DETAIL | DEBUG_CMD), dptr, "RUN\n");
        r = sim_tape_detach(up);
        chan_set(chan, DEV_REOR|CTL_END);
        up->u5 |= HT_NOTRDY;
        up->wait = 100;
        break;
    }

    if (r != MTSE_OK) {
        ht_error(up, schan, r);
        chan9_set_error(chan, SNS_UEND);
        chan9_set_attn(chan, sel);
        chan_set(chan, DEV_REOR|CTL_END);
        up->u5 &= ~(HT_NOTRDY | HT_CMDMSK);
        uptr->u5 &= ~HT_NOTRDY;
        up->wait = 0;
    } else if (up->u5 & HT_CMDMSK) {
        sim_activate(up, us_to_ticks(1000));
    } else {
        chan9_set_attn(chan, sel);
    }

    return;
}

/* Boot Hypertape. Build boot card loader in memory and transfer to it */
t_stat
ht_boot(int unit_num, DEVICE * dptr)
{
#ifdef I7090
    UNIT               *uptr = &dptr->units[unit_num];
    int                 chan = UNIT_G_CHAN(uptr->flags) - 1;
    int                 sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    int                 dev = uptr->u3;
    int                 msk = (chan / 2) | ((chan & 1) << 11);
    extern uint16       IC;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    if (dev == 0)
        dev = 012;
    /* Build Boot program in memory */
    M[0] = 0000025000101LL;     /*      IOCD RSCQ,,21 */
    M[1] = 0006000000001LL;     /*      TCOA * */
    M[2] = 0002000000101LL;     /*      TRA RSCQ */

    M[0101] = 0054000000113LL;  /* RSCQ RSCC SMSQ  Mod */
    M[0101] |= ((t_uint64) (msk)) << 24;
    M[0102] = 0064500000000LL;  /* SCDQ SCDC 0  Mod */
    M[0102] |= ((t_uint64) (msk)) << 24;
    M[0103] = 0044100000000LL;  /*      LDI 0 */
    M[0104] = 0405400001700LL;  /*      LFT 1700 */
    M[0105] = 0002000000122LL;  /*      TRA HYP7 */
    M[0106] = 0006000000102LL;  /* TCOQ TCOC SCDQ  Mod */
    M[0106] |= ((t_uint64) (chan)) << 24;
    M[0107] = 0002000000003LL;  /*      TRA 3    Enter IBSYS */
    M[0110] = 0120600120112LL;
    M[0110] |= ((t_uint64) (dev)) << 18;
    M[0111] = 0120600030412LL;  /*LDVCY DVCY  Mod */
    M[0111] |= ((t_uint64) (dev)) << 18;
    M[0112] = 0010000000000LL;  /*      *    */
    M[0113] = 0700000000012LL;  /* HYP6 SMS   10 */
    M[0113] |= sel;
    M[0114] = 0200000200110LL;  /*      CTLR  *-4 */
    M[0115] = 0400001000116LL;  /*      CPYP  *+1,,1 */
    M[0116] = 0000000000116LL;  /*      WTR * */
    M[0117] = 0100000000115LL;  /*      TCH  *-2 */
    M[0120] = 0700000400113LL;  /*      SMS*  HYP6 */
    M[0121] = 0200000000111LL;  /*      CTL  HYP6-2 */
    M[0122] = 0076000000350LL;  /* HYP7 RICC **     */
    M[0122] |= ((t_uint64) (chan)) << 9;
    M[0123] = 0054000000120LL;  /*      RSCC *-3  Mod */
    M[0123] |= ((t_uint64) (msk)) << 24;
    M[0124] = 0500000000000LL;  /*      CPYD  0,,0 */
    M[0125] = 0340000000125LL;  /*      TWT   * */
    IC = 0101;
    return SCPE_OK;
#else
    return SCPE_NOFNC;
#endif
}

t_stat
ht_reset(DEVICE * dptr)
{
    int                 i;

    for (i = 0; i < NUM_CHAN; i++) {
        ht_cmdbuffer[i] = ht_cmdcount[i] = 0;
        ht_sense[i] = 0;
    }
    return SCPE_OK;
}


t_stat
ht_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_tape_attach_ex(uptr, file, 0, 0)) != SCPE_OK)
        return r;
    uptr->u5 = HT_BOT /*|HT_ATTN */ ;
    return SCPE_OK;
}

t_stat
ht_detach(UNIT * uptr)
{
    uptr->u5 = 0;
    if (uptr->flags & UNIT_DIS) return SCPE_OK;
    return sim_tape_detach(uptr);
}

t_stat
ht_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "IBM 7340 Hypertape unit\n\n");
   help_set_chan_type(st, dptr, "IBM 7340 Hypertape");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
ht_description(DEVICE *dptr)
{
   return "IBM 7340 Hypertape unit";
}

#endif
