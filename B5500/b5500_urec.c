/* b5500_urec.c: Burrioughs 5500 Unit record devices.

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

   This is the standard card reader.            10,14
   This is the standard card punch.             10
   This is the standard line printer.           22,26
   This is the standard operators console.      30

*/

#include "b5500_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#include "sim_console.h"

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_029
#define UNIT_CDP        UNIT_ATTABLE | UNIT_SEQ | UNIT_DISABLE | MODE_029
#define UNIT_LPR        UNIT_ATTABLE | UNIT_SEQ | UNIT_DISABLE

#define TMR_RTC         0

#define LINENUM    u3
#define POS        u4
#define CMD        u5
#define LPP        u6


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

/* Device status information stored in CMD */
#define URCSTA_CHMASK   0003    /* Mask of I/O channel to send data on */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */
#define URCSTA_BIN      0020    /* Card reader in binary mode */
#define URCSTA_ACTIVE   0040    /* Unit is active */
#define URCSTA_EOF      0100    /* Flag the end of file */
#define URCSTA_INPUT    0200    /* Console fill buffer from keyboard */
#define URCSTA_FILL     010000  /* Fill unit buffer */
#define URCSTA_CMD_V    16

#define URCSTA_SKIP     000017  /* Skip mask */
#define URCSTA_DOUBLE   000020  /* Double space skip */
#define URCSTA_SINGLE   000040  /* Single space skip. */
#define URCSTA_READ     000400  /* Read flag */
#define URCSTA_WC       001000  /* Use word count */
#define URCSTA_DIRECT   002000  /* Direction, Long line */
#define URCSTA_BINARY   004000  /* Binary transfer */
#define URCSTA_INHIBIT  040000  /* Inhibit transfer to memory */

/* Simulator debug controls */
DEBTAB              cdr_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show console data"},
    {"CARD", DEBUG_CARD, "Show Card read/punches"},
    {0, 0}
};


#if NUM_DEVS_CDR > 0
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_ini(DEVICE *);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *dptr);
uint16              cdr_buffer[NUM_DEVS_CDR][80];
#endif

#if NUM_DEVS_CDP > 0
t_stat              cdp_ini(DEVICE *);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *dptr);
uint16              cdp_buffer[NUM_DEVS_CDP][80];
#endif

#if NUM_DEVS_LPR  > 0
t_stat              lpr_ini(DEVICE *);
t_stat              lpr_srv(UNIT *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat              lpr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *lpr_description(DEVICE *dptr);
uint8               lpr_buffer[NUM_DEVS_LPR][145];   /* Output line buffer */
#endif

#if NUM_DEVS_CON  > 0
struct _con_data
{
    uint8               ibuff[145];     /* Input line buffer */
    uint8               inptr;
    uint8               outptr;
}
con_data[NUM_DEVS_CON];

t_stat              con_ini(DEVICE *);
t_stat              con_srv(UNIT *);
t_stat              con_attach(UNIT *, CONST char *);
t_stat              con_detach(UNIT *);
t_stat              con_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *con_description(DEVICE *dptr);
#endif



#if NUM_DEVS_CDR > 0
UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_CDR, 0)},       /* A */
#if NUM_DEVS_CDR > 1
   {UDATA(cdr_srv, UNIT_CDR|UNIT_DIS, 0)},      /* B */
#endif
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
          &sim_card_set_fmt, &sim_card_show_fmt, NULL,
          "Sets card format"},
    {0}
};

REG                 cdr_reg[] = {
    {BRDATA(BUFF, cdr_buffer, 16, 16, sizeof(cdr_buffer)/sizeof(uint16)), REG_HRO},
    {0}
};  

DEVICE              cdr_dev = {
    "CR", cdr_unit, cdr_reg, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, &cdr_ini, &cdr_boot, &cdr_attach, &cdr_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, cdr_debug,
    NULL, NULL, &cdr_help, NULL, NULL,
    &cdr_description
};
#endif

#if NUM_DEVS_CDP > 0
UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_CDP, 0)},      /* A */
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
          &sim_card_set_fmt, &sim_card_show_fmt, NULL,
          "Sets card format"},
    {0}
};

REG                 cdp_reg[] = {
    {BRDATA(BUFF, cdp_buffer, 16, 16, sizeof(cdp_buffer)/sizeof(uint16)), REG_HRO},
    {0}
};  

DEVICE              cdp_dev = {
    "CP", cdp_unit, cdp_reg, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, &cdp_ini, NULL, &cdp_attach, &cdp_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, cdr_debug,
    NULL, NULL, &cdp_help, NULL, NULL,
    &cdp_description
};

#endif

#if NUM_DEVS_LPR > 0
UNIT                lpr_unit[] = {
    {UDATA(lpr_srv, UNIT_LPR, 59)},     /* A */
#if NUM_DEVS_LPR > 1
    {UDATA(lpr_srv, UNIT_LPR|UNIT_DIS, 59)},    /* B */
#endif
};

MTAB                lpr_mod[] = {
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lpr_setlpp, &lpr_getlpp, NULL,
        "Sets number of lines on a printed page"},
    {0}
};

REG                 lpr_reg[] = {
    {BRDATA(BUFF, lpr_buffer, 16, 8, sizeof(lpr_buffer)), REG_HRO},
    {0}
};  

DEVICE              lpr_dev = {
    "LP", lpr_unit, lpr_reg, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, &lpr_ini, NULL, &lpr_attach, &lpr_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL,
    &lpr_description
};
#endif

#if NUM_DEVS_CON > 0
UNIT                con_unit[] = {
    {UDATA(con_srv, UNIT_IDLE, 0), 0},  /* A */
};

REG                 con_reg[] = {
    {SAVEDATA(BUFF, con_data) },
    {0}
};  

DEVICE              con_dev = {
    "CON", con_unit, con_reg, NULL,
    NUM_DEVS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, &con_ini, NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &con_help, NULL, NULL,
    &con_description
};
#endif



#if ((NUM_DEVS_CDR > 0) | (NUM_DEVS_CDP > 0))
t_stat
cdr_ini(DEVICE *dptr) {
     int                i;

     for(i = 0; i < NUM_DEVS_CDR; i++) {
        cdr_unit[i].CMD = 0;
        sim_cancel(&cdr_unit[i]);
     }
     return SCPE_OK;
}

/*
 * Device entry points for card reader.
 * And Card punch.
 */
t_stat card_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT        *uptr;
    int          u;

    if (dev == CARD1_DEV)
        u = 0;
    else if (dev == CARD2_DEV)
        u = 1;
    else
        return SCPE_NXDEV;
    /* Check if card reader or card punch */
    if (cmd & URCSTA_READ) {
        uptr = &cdr_unit[u];
        if ((uptr->flags & UNIT_ATT) == 0)
            return SCPE_UNATT;

        /* Are we currently tranfering? */
        if (uptr->CMD & URCSTA_ACTIVE)
            return SCPE_BUSY;

        /* Check if we ran out of cards */
        if (uptr->CMD & URCSTA_EOF) {
            /* If end of file, return to system */
            if (sim_card_input_hopper_count(uptr) != 0) 
                uptr->CMD &= ~URCSTA_EOF;
            else {
                /* Clear unit ready */
                iostatus &= ~(CARD1_FLAG << u);
                return SCPE_UNATT;
            }
        }

        if (cmd & URCSTA_BINARY) {
            uptr->CMD |= URCSTA_BIN;
            *wc = 20;
        } else {
            uptr->CMD &= ~URCSTA_BIN;
            *wc = 10;
        }

        uptr->CMD &= ~URCSTA_CHMASK;
        uptr->CMD |= URCSTA_ACTIVE|chan;
        uptr->POS = 0;

        sim_activate(uptr, 500000);
        return SCPE_OK;
    } else {
        /* Talking to punch */
        if (u != 0)
             return SCPE_NXDEV;
        sim_debug(DEBUG_DETAIL, &cdr_dev, "cdp %d %d start\n", u, chan);
        uptr = &cdp_unit[0];
        if ((uptr->flags & UNIT_ATT) == 0)
            return SCPE_UNATT;
        if (uptr->CMD & URCSTA_ACTIVE)
             return SCPE_BUSY;
        uptr->CMD &= ~URCSTA_CHMASK;
        uptr->CMD |= URCSTA_ACTIVE|chan;
        uptr->POS = 0;
        *wc = 10;

        sim_activate(uptr, 500000);
        sim_debug(DEBUG_DETAIL, &cdr_dev, "cdp %d %d go\n", u, chan);
        return SCPE_OK;
    }
    return SCPE_IOERR;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int                 chan = URCSTA_CHMASK & uptr->CMD;
    int                 u = (uptr - cdr_unit);
    uint16              *image = &cdr_buffer[u][0];

    if (uptr->CMD & URCSTA_EOF) {
        sim_debug(DEBUG_DETAIL, &cdr_dev, "cdr %d %d unready\n", u, chan);
        iostatus &= ~(CARD1_FLAG << u);
        uptr->CMD &= ~ URCSTA_EOF;
        return SCPE_OK;
    }


    /* Check if new card requested. */
    if (uptr->POS == 0 && uptr->CMD & URCSTA_ACTIVE &&
                (uptr->CMD & URCSTA_CARD) == 0) {
        switch(sim_read_card(uptr, image)) {
        case CDSE_EMPTY:
             iostatus &= ~(CARD1_FLAG << u);
             uptr->CMD &= ~(URCSTA_ACTIVE);
             iostatus &= ~(CARD1_FLAG << u);
             chan_set_notrdy(chan);
             break;
        case CDSE_EOF:
             /* If end of file, return to system */
             uptr->CMD &= ~(URCSTA_ACTIVE);
             uptr->CMD |= URCSTA_EOF;
             chan_set_notrdy(chan);
             sim_activate(uptr, 500);
             break;
        case CDSE_ERROR:
             chan_set_error(chan);
             uptr->CMD &= ~(URCSTA_ACTIVE);
             uptr->CMD |= URCSTA_EOF;
             chan_set_end(chan);
             break;
        case CDSE_OK:
             uptr->CMD |= URCSTA_CARD;
             sim_activate(uptr, 500);
             break;
        }
        return SCPE_OK;
    }


    /* Copy next column over */
    if (uptr->CMD & URCSTA_CARD &&
        uptr->POS < ((uptr->CMD & URCSTA_BIN) ? 160 : 80)) {
        uint8                ch = 0;
        int                  u = (uptr - cdr_unit);

        if (uptr->CMD & URCSTA_BIN) {
            ch = (image[uptr->POS >> 1] >> ((uptr->POS & 1)?  0 : 6)) & 077;
        } else {
            ch = sim_hol_to_bcd(image[uptr->POS]);
            /* Remap some characters from 029 to BCL */
            /* Sim_hol_to_bcd translates cards by looking at the zones 
             *  12 - 11 and 10 and setting the two most significant
             * digits of the BCD word to 11xxxx, 10xxxx, 01xxxx
             * next if 8 is punched it add in 001000 then adds the one
             * other digit if it is punched to make the lower four bits
             * of the BCD number.
             *
             * A code of 10 only is returned as 1010 or 10.
             * Some of these codes need to be changed because of overlap
             * and minor variations in Burroughs code to IBM029 code.
             */
        sim_debug(DEBUG_DATA, &cdr_dev, "cdr %d: Char > %03o ", u, ch);
            switch(ch) {
            case 0:     ch = 020; break; /* Translate blanks */
            case 012:   if (image[uptr->POS] == 0x082) /* 8-2 punch to 015 */
                            ch = 015;
                        break;
            case 016:   ch = 035; break; /* Translate = */
            case 017:   ch = 037; break; /* Translate " */
            case 036:   ch = 016; break;
            case 037:   ch = 0;          /* Handle ? */
                        if (uptr->POS == 0)
                            chan_set_parity(chan);
                        break;
            case 052:   ch = 032;        /* Translate ! not equal */
                        break;
            case 074:   ch = 076; break; /* Translate < */
            case 076:   ch = 072; break; /* Translate + */
            case 077:   ch = 052; break; /* Translate | */
            case 0177:
                        if (image[uptr->POS] == 0x805)      /* Translate } */
                            ch = 017;
                        else if (image[uptr->POS] == 0xE42) /* Translate ] */
                            ch = 036;
                        else if (image[uptr->POS] == 0xE82) /* Translate [ */
                            ch = 074;
                        else if (image[uptr->POS] == 0xF02) /* Translate ~ */
                            ch = 077;
                        else {
                            ch = 0;
                            /* Handle invalid punch */
                            chan_set_parity(chan);
                        }
                        break;  /* Translate ? to error*/
            }
        }
        sim_debug(DEBUG_DATA, &cdr_dev, "-> %03o '%c' %d\n", ch,
                        sim_six_to_ascii[ch & 077], uptr->POS);
        if(chan_write_char(chan, &ch, 0)) {
            uptr->CMD &= ~(URCSTA_ACTIVE|URCSTA_CARD);
            chan_set_end(chan);
            /* Drop ready a bit after the last card is read */
            if (sim_card_eof(uptr)) {
                uptr->CMD |= URCSTA_EOF;
                sim_activate(uptr, 100);
            }
        } else {
            uptr->POS++;
            sim_activate(uptr, 100);
        }
    }

    /* Check if last column */
    if (uptr->CMD & URCSTA_CARD &&
        uptr->POS == ((uptr->CMD & URCSTA_BIN) ? 160 : 80)) {

        uptr->CMD &= ~(URCSTA_ACTIVE|URCSTA_CARD);
        chan_set_end(chan);
        /* Drop ready a bit after the last card is read */
        if (sim_card_eof(uptr)) {
            uptr->CMD |= URCSTA_EOF;
        }
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint8               dev;
    t_uint64            desc;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    dev = (uptr == &cdr_unit[0]) ? CARD1_DEV : CARD2_DEV;
    uptr->CMD &= ~URCSTA_ACTIVE;
    desc = ((t_uint64)dev) << DEV_V | DEV_IORD| DEV_BIN | 020LL;
    /* Read in one record */
    return chan_boot(desc);
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    int                 u = uptr-cdr_unit;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    iostatus |= (CARD1_FLAG << u);
    return SCPE_OK;
}

t_stat
cdr_detach(UNIT * uptr)
{
    int                 u = uptr-cdr_unit;

    iostatus &= ~(CARD1_FLAG << u);
    return sim_card_detach(uptr);
}

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "B124 Card Reader\n\n");
   fprintf (st, "The system supports up to two card readers, the second one is disabled\n");
   fprintf (st, "by default. To have the card reader return the EOF flag when the deck\n");
   fprintf (st, "has finished reading do:\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "B124 Card Reader";
}

#endif



#if NUM_DEVS_CDR > 0 | NUM_DEVS_CDP > 0

/* Handle transfer of data for card punch */
t_stat
cdp_ini(DEVICE *dptr) {
     int                i;

     for(i = 0; i < NUM_DEVS_CDP; i++) {
        cdp_unit[i].CMD = 0;
        sim_cancel(&cdp_unit[i]);
     }
     return SCPE_OK;
}

t_stat
cdp_srv(UNIT *uptr) {
    int                 chan = URCSTA_CHMASK & uptr->CMD;
    int                 u = (uptr - cdp_unit);
    uint16              *image = &cdp_buffer[u][0];

    if (uptr->CMD & URCSTA_BUSY) {
        /* Done waiting, punch card */
        if (uptr->CMD & URCSTA_FULL) {
              sim_debug(DEBUG_DETAIL, &cdp_dev, "cdp %d %d punch\n", u, chan);
              switch(sim_punch_card(uptr, image)) {
              case CDSE_EOF:
              case CDSE_EMPTY:
                  sim_debug(DEBUG_DETAIL, &cdp_dev, "cdp %d %d set eof\n", u,
                                 chan);
                  chan_set_eof(chan);
                  break;
                 /* If we get here, something is wrong */
              case CDSE_ERROR:
                  chan_set_error(chan);
                  break;
              case CDSE_OK:
                  break;
              }
              uptr->CMD &= ~URCSTA_FULL;
              chan_set_end(chan);
        }
        uptr->CMD &= ~URCSTA_BUSY;
    }

    /* Copy next column over */
    if (uptr->CMD & URCSTA_ACTIVE && uptr->POS < 80) {
        uint8               ch = 0;
        uint16              hol;

        if(chan_read_char(chan, &ch, 0)) {
             uptr->CMD |= URCSTA_BUSY|URCSTA_FULL;
             uptr->CMD &= ~URCSTA_ACTIVE;
        } else {
            hol = 0;
            switch (ch & 077) {
            case 000:  hol = 0x206; break;  /* ? */
            case 015:  hol = 0x082; break;  /* : */
            case 016:  hol = 0x20A; break;  /* > */
            case 017:  hol = 0x805; break;  /* } */
            case 032:  hol = 0x482; break;  /* ! */
            case 035:  hol = 0X00A; break;  /* = */
            case 036:  hol = 0xE42; break;  /* ] */
            case 037:  hol = 0x006; break;  /* " */
            case 052:  hol = 0x806; break;  /* | */
            case 072:  hol = 0x80A; break;  /* + */
            case 074:  hol = 0xE82; break;  /* [ */
            case 076:  hol = 0x822; break;  /* < */
            case 077:  hol = 0xF02; break;  /* ~ */
            default:
                       hol = sim_bcd_to_hol(ch & 077);
            }
            sim_debug(DEBUG_DATA, &cdp_dev, "cdp %d: Char %d < %02o %03x\n", u,
                         uptr->POS, ch, hol);
            image[uptr->POS++] = hol;
        }
        sim_activate(uptr, 10);
    }

    /* Check if last column */
    if (uptr->CMD & URCSTA_ACTIVE && uptr->POS == 80) {
        uptr->CMD |= URCSTA_BUSY|URCSTA_FULL;
        uptr->CMD &= ~URCSTA_ACTIVE;
    }
    return SCPE_OK;
}


t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    iostatus |= PUNCH_FLAG;
    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    int                 u = uptr-cdr_unit;
    uint16              *image = &cdp_buffer[u][0];

    if (uptr->CMD & URCSTA_FULL)
        sim_punch_card(uptr, image);
    iostatus &= ~PUNCH_FLAG;
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "B303 Card Punch\n\n");
   fprintf (st, "The B303 Card Punch is only capable of punching text decks, binary decks\n");
   fprintf (st, "where not supported.\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
   return "B303 Card Punch";
}

#endif


/* Line printer routines
*/
t_stat
lpr_ini(DEVICE *dptr) {
     int                i;

     for(i = 0; i < NUM_DEVS_LPR; i++) {
        lpr_unit[i].CMD = 0;
        sim_cancel(&lpr_unit[i]);
     }
     return SCPE_OK;
}

#if NUM_DEVS_LPR > 0
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
    uptr->LPP = i;
    uptr->LINENUM = 0;
    return SCPE_OK;
}

t_stat
lpr_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->LPP);
    return SCPE_OK;
}

void
print_line(UNIT * uptr, int unit)
{
/* Convert word record into column image */
/* Check output type, if auto or text, try and convert record to bcd first */
/* If failed and text report error and dump what we have */
/* Else if binary or not convertable, dump as image */

    char                out[150];       /* Temp conversion buffer */
    int                 i;

    if ((uptr->flags & (UNIT_ATT)) == 0)
        return; /* attached? */

    if (uptr->POS > 0) {
        /* Try to convert to text */
        memset(out, 0, sizeof(out));

        /* Scan each column */
        for (i = 0; i < uptr->POS; i++) {
            int                 bcd = lpr_buffer[unit][i] & 077;

            out[i] = con_to_ascii[bcd];
        }

        /* Trim trailing spaces */
        for (--i; i > 0 && out[i] == ' '; i--) ;
        out[i+1] = '\0';

        sim_debug(DEBUG_DETAIL, &lpr_dev, "lpr print %s\n", out);
        if (uptr->CMD & (URCSTA_DOUBLE << URCSTA_CMD_V)) {
            out[++i] = '\r';
            out[++i] = '\n';
            uptr->LINENUM ++;
        }
        out[++i] = '\r';
        out[++i] = '\n';
        uptr->LINENUM++;
        out[++i] = '\0';

        /* Print out buffer */
        sim_fwrite(&out, 1, i, uptr->fileref);
        uptr->pos += i;
        uptr->CMD &= ~URCSTA_EOF;
    }


    switch ((uptr->CMD >> URCSTA_CMD_V) & URCSTA_SKIP) {
    case 0:     /* No special skip */
        break;
    case 1:
    case 2:     /* Skip to top of form */
    case 12:
        uptr->LINENUM = uptr->LPP+1;
        break;

    case 3:     /* Even lines */
        if ((uptr->LINENUM & 1) == 1) {
            sim_fwrite("\r", 1, 1, uptr->fileref);
            sim_fwrite("\n", 1, 1, uptr->fileref);
            uptr->pos += 2;
            uptr->LINENUM++;
            uptr->CMD &= ~URCSTA_EOF;
        }
        break;
    case 4:     /* Odd lines */
        if ((uptr->LINENUM & 1) == 0) {
            sim_fwrite("\r", 1, 1, uptr->fileref);
            sim_fwrite("\n", 1, 1, uptr->fileref);
            uptr->pos += 2;
            uptr->LINENUM++;
            uptr->CMD &= ~URCSTA_EOF;
        }
        break;
    case 5:     /* Half page */
        while((uptr->LINENUM != (uptr->LPP/2)) ||
              (uptr->LINENUM != (uptr->LPP))) {
            sim_fwrite("\r", 1, 1, uptr->fileref);
            sim_fwrite("\n", 1, 1, uptr->fileref);
            uptr->pos += 2;
            uptr->LINENUM++;
            if (uptr->LINENUM > uptr->LPP) {
                uptr->LINENUM = 1;
                break;
            }
            uptr->CMD &= ~URCSTA_EOF;
        }
        break;
    case 6:     /* 1/4 Page */
        while((uptr->LINENUM != (uptr->LPP/4)) ||
              (uptr->LINENUM != (uptr->LPP/2)) ||
              (uptr->LINENUM != (uptr->LPP/2+uptr->LPP/4)) ||
              (uptr->LINENUM != (uptr->LPP))) {
            sim_fwrite("\r", 1, 1, uptr->fileref);
            sim_fwrite("\n", 1, 1, uptr->fileref);
            uptr->pos += 2;
            uptr->LINENUM++;
            if (uptr->LINENUM > uptr->LPP) {
                uptr->LINENUM = 1;
                break;
            }
            uptr->CMD &= ~URCSTA_EOF;
        }
        break;
    case 7:     /* User defined, now 1 line */
    case 8:
    case 9:
    case 10:
    case 11:
        sim_fwrite("\r", 1, 1, uptr->fileref);
        sim_fwrite("\n", 1, 1, uptr->fileref);
        uptr->pos += 2;
        uptr->LINENUM++;
        break;
    }


    if (uptr->LINENUM > uptr->LPP) {
        uptr->LINENUM = 1;
        uptr->CMD |= URCSTA_EOF;
        sim_fwrite("\f", 1, 1, uptr->fileref);
        uptr->pos ++;
        sim_fseek(uptr->fileref, 0, SEEK_CUR);
        sim_debug(DEBUG_DETAIL, &lpr_dev, "lpr %d page\n", unit);
    }

}



t_stat lpr_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT                *uptr;
    int                 u;

    if (dev == PRT1_DEV)
        u = 0;
    else if (dev == PRT2_DEV)
        u = 1;
    else
        return SCPE_NXDEV;
    uptr = &lpr_unit[u];

    /* Are we currently tranfering? */
    if (uptr->CMD & URCSTA_BUSY)
        return SCPE_BUSY;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;

    if (*wc == 0 && (cmd & URCSTA_INHIBIT) == 0)
        *wc = (cmd & URCSTA_DIRECT) ? 17 : 15;

    /* Remember not to drop the FULL */
    uptr->CMD &= ~((077 << URCSTA_CMD_V) | URCSTA_CHMASK);
    uptr->CMD |= URCSTA_BUSY|chan;
    uptr->CMD |= (cmd & (URCSTA_SKIP|URCSTA_SINGLE|URCSTA_DOUBLE))
                << URCSTA_CMD_V;
    uptr->POS = 0;
    sim_debug(DEBUG_CMD, &lpr_dev, "%d: Cmd WRS %d %02o %o\n", u, chan,
                 cmd & (URCSTA_SKIP|URCSTA_SINGLE|URCSTA_DOUBLE),uptr->CMD);
    sim_activate(uptr, 100);
    return SCPE_OK;
}

/* Handle transfer of data for printer */
t_stat
lpr_srv(UNIT *uptr) {
    int                 chan = URCSTA_CHMASK & uptr->CMD;
    int                 u = (uptr - lpr_unit);

    if (uptr->CMD & URCSTA_FULL) {
        sim_debug(DEBUG_CMD, &lpr_dev, "lpr %d: done\n", u);
        uptr->CMD &= ~URCSTA_FULL;
        IAR |= (IRQ_3 << u);
    }

    /* Copy next column over */
    if ((uptr->CMD & URCSTA_BUSY) != 0) {
        if(chan_read_char(chan, &lpr_buffer[u][uptr->POS], 0)) {
            /* Done waiting, print line */
            print_line(uptr, u);
            memset(&lpr_buffer[u][0], 0, 144);
            uptr->CMD |= URCSTA_FULL;
            uptr->CMD &= ~URCSTA_BUSY;
            chan_set_wc(chan, (uptr->POS/8));
            chan_set_end(chan);
            sim_activate(uptr, 20000);
            return SCPE_OK;
        } else {
            sim_debug(DEBUG_DATA, &lpr_dev, "lpr %d: Char < %02o\n", u,
                        lpr_buffer[u][uptr->POS]);
            uptr->POS++;
        }
        sim_activate(uptr, 50);
    }
    return SCPE_OK;
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    int                 u = (uptr - lpr_unit);

    sim_switches |= SWMASK ('A');   /* Position to EOF */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    if ((sim_switches & SIM_SW_REST) == 0) {
        uptr->CMD = 0;
        uptr->LINENUM = 0;
        uptr->POS = 0;
    }
    iostatus |= PRT1_FLAG << u;
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    int                 u = (uptr - lpr_unit);
    if (uptr->CMD & URCSTA_FULL)
        print_line(uptr, u);
    iostatus &= ~(PRT1_FLAG << u);
    return detach_unit(uptr);
}

t_stat
lpr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "B320 Line Printer\n\n");
   fprintf (st, "The system supports up to two line printers, the second one is disabled\n");
   fprintf (st, "by default. The B320 Line printer can be configured to any number of\n");
   fprintf (st, "lines per page with the:\n");
   fprintf (st, "        sim> SET LPn LINESPERPAGE=n\n\n");
   fprintf (st, "The default is 59 lines per page. The Line Printer has the following\n");
   fprintf (st, "control tape attached.\n");
   fprintf (st, "     Channel 1:     Skip to top of page\n");
   fprintf (st, "     Channel 2:     Skip to top of page\n");
   fprintf (st, "     Channel 3:     Skip to next even line\n");
   fprintf (st, "     Channel 4:     Skip to next odd line\n");
   fprintf (st, "     Channel 5:     Skip to middle or top of page\n");
   fprintf (st, "     Channel 6:     Skip 1/4 of page\n");
   fprintf (st, "     Channel 7:     Skip one line\n");
   fprintf (st, "     Channel 8:     Skip one line\n");
   fprintf (st, "     Channel 9:     Skip one line\n");
   fprintf (st, "     Channel 10:    Skip one line\n");
   fprintf (st, "     Channel 11:    Skip one line\n");
   fprintf (st, "     Channel 12:    Skip to top of page\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
lpr_description(DEVICE *dptr)
{
   return "B320 Line Printer";
}

#endif




#if NUM_DEVS_CON > 0
/*
 * Console printer routines.
 */
t_stat
con_ini(DEVICE *dptr) {
     UNIT               *uptr = &con_unit[0];
     uptr->CMD = 0;
     iostatus |= SPO_FLAG;
     if (!sim_is_active(uptr))
         sim_activate(uptr, 1000);
     return SCPE_OK;
}

t_stat
con_cmd(uint16 cmd, uint16 dev, uint8 chan, uint16 *wc)
{
    UNIT        *uptr = &con_unit[0];

    /* Are we currently tranfering? */
    if (uptr->CMD & (URCSTA_READ|URCSTA_FILL|URCSTA_BUSY|URCSTA_INPUT))
        return SCPE_BUSY;

    if (cmd & URCSTA_READ) {
        if (uptr->CMD & (URCSTA_INPUT|URCSTA_FILL))
            return SCPE_BUSY;
        /* Activate input so we can get response */
        uptr->CMD = 0;
        uptr->CMD |= URCSTA_INPUT|chan;
        sim_putchar('I');
        sim_putchar(' ');
        sim_debug(DEBUG_CMD, &con_dev, ": Cmd RDS\n");
        uptr->POS = 0;
    } else {
        if (uptr->CMD & (URCSTA_INPUT|URCSTA_FILL))
            return SCPE_BUSY;
        sim_putchar('R');
        sim_putchar(' ');
        sim_debug(DEBUG_CMD, &con_dev, ": Cmd WRS\n");
        uptr->CMD = 0;
        uptr->CMD |= URCSTA_FILL|chan;
        uptr->POS = 0;
    }
    return SCPE_OK;
}

/* Handle transfer of data for printer */
t_stat
con_srv(UNIT *uptr) {
    t_stat              r;
    uint8               ch;
    int                 chan = uptr->CMD & URCSTA_CHMASK;


    uptr->CMD &= ~URCSTA_BUSY;   /* Clear busy */

    /* Copy next column over */
    if (uptr->CMD & URCSTA_FILL) {
        if(chan_read_char(chan, &ch, 0)) {
             sim_putchar('\r');
             sim_putchar('\n');
             sim_debug(DEBUG_EXP, &con_dev, "\n\r");
             uptr->CMD &= ~URCSTA_FILL;
             chan_set_end(chan);
       } else {
             ch &= 077;
             sim_debug(DEBUG_EXP, &con_dev, "%c", con_to_ascii[ch]);
             sim_putchar((int32)con_to_ascii[ch]);
       }
    }

    if (uptr->CMD & URCSTA_READ) {
        ch = con_data[0].ibuff[con_data[0].outptr++];

        if(chan_write_char(chan, &ch,
                (con_data[0].inptr == con_data[0].outptr))) {
             sim_putchar('\r');
             sim_putchar('\n');
             sim_debug(DEBUG_EXP, &con_dev, "\n\r");
             uptr->CMD &= ~URCSTA_READ;
             chan_set_end(chan);
       }
    }

    r = sim_poll_kbd();
    if (r & SCPE_KFLAG) {
        ch = r & 0377;
        if (uptr->CMD & URCSTA_INPUT) {
           /* Handle end of buffer */
           switch (ch) {
           case 033:
                con_data[0].inptr = 0;
                /* Fall through */
           case '\r':
           case '\n':
                uptr->CMD &= ~URCSTA_INPUT;
                uptr->CMD |= URCSTA_READ;
                break;
           case '\b':
           case 0x7f:
                if (con_data[0].inptr != 0) {
                  con_data[0].inptr--;
                  sim_putchar('\b');
                  sim_putchar(' ');
                  sim_putchar('\b');
                }
                break;
           default:
                if (con_data[0].inptr < sizeof(con_data[0].ibuff)) {
                    ch = ascii_to_con[0177&ch];
                    if (ch == 0xff) {
                        sim_putchar('\007');
                        break;
                    }
                    sim_putchar((int32)con_to_ascii[ch]);
                    con_data[0].ibuff[con_data[0].inptr++] = ch;
                }
                break;
           }
         } else {
            if (ch == 033) {
                 IAR |= IRQ_2;
                 con_data[0].inptr = 0;
                 con_data[0].outptr = 0;
            }
        }
    }

    if (uptr->CMD & (URCSTA_FILL|URCSTA_READ))
        sim_activate(uptr, 1000);
    else
        sim_clock_coschedule_tmr (con_unit, TMR_RTC, 1);
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

