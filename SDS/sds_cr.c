/* sds_cr.c: SDS-930 card reader simulator

   Copyright (c) 2020, Ken Rector

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
   KEN RECTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Ken Rector shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Ken Rector.

   03-Mar-20    kenr    Initial Version

   */

/*
 This card reader simulator uses sim_card.c to attach and read input records
 in CBN format.  When BCD mode is specified by the buffer control EOM, input data
 is translated from the Hollerith encoded data in the card columns to the
 SDS Internal Code as defined by the SDS 930 Computer Reference Manual.  The
 translation function was modified from the sim_card.c code to provide SDS
 Internal BCD codes.
 
 The card reader delays the disconnect after the last character until the trailing
 edge of the card is detected.  In this simulator, this delay is accomplished
 by scheduling a final service request after the last characters have been
 delivered too the channel.  The timing for this service has been adjusted to
 handle some example SDS programs.  Too long a delay causes errors in some, too short
 a delay affects others. 
 */

#include "sds_defs.h"
#include "sim_card.h"

#define FEEDING         00001000    /* feeding card to read station */
#define READING         00004000    /* Card at read station */

#define STATUS u3                   /* status */

#define CARD_RDY(u)       (sim_card_input_hopper_count(u) > 0 || \
sim_card_eof(u) == 1)

extern  uint32 xfr_req;
extern  int32 stop_invins, stop_invdev, stop_inviop;
extern  uint8 chan_cpw[NUM_CHAN];       /* char per word */
extern  uint8 chan_cnt[NUM_CHAN];       /* char count */

int32   cr_bptr = 0;                    /* buf ptr */
int32   cr_blnt = 0;                    /* buf length */
int32   cr_chr = 0;                     /* char no.*/
int32   cr_inst = 0;                    /* saved instr */
int32   cr_eor  = 0;                    /* end of record */
uint16  cr_buffer[80];                  /* card record */

DSPT    cr_tplt[] = {{1,0},{0,0}};      /* template */

t_stat  cr_svc(UNIT *);
t_stat  cr_boot(int32, DEVICE *);
t_stat  cr_reset(DEVICE *);
t_stat  cr_attach(UNIT *, CONST char *);
t_stat  cr_detach(UNIT *);
t_stat  cr_devio(uint32 fnc, uint32 inst, uint32 *dat);
t_stat  cr_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat  cr_readrec (UNIT *uptr);
void    cr_set_err (UNIT *uptr);

DIB     cr_dib = { CHAN_W, DEV_CR, XFR_CR, cr_tplt, &cr_devio };

UNIT    cr_unit = {
    UDATA(&cr_svc, UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_029 | MODE_CBN,0),
    60
};

REG     cr_reg[] = {
    { DRDATA (BPTR, cr_bptr, 18), PV_LEFT },
    { DRDATA (BLNT, cr_blnt, 18), PV_LEFT },
    { FLDATA (XFR, xfr_req, XFR_V_CR) },
    { ORDATA (INST, cr_inst, 24) },
    { DRDATA (POS, cr_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
};

MTAB    cr_mod[] = {
    {MTAB_XTD | MTAB_VDV, 0, "CHANNEL", "CHANNEL",
        &set_chan,&show_chan,NULL, "Device Channel"},
    {MTAB_XTD | MTAB_VDV, 0, "FORMAT", "FORMAT",
        &sim_card_set_fmt, &sim_card_show_fmt, 
        NULL,"Card Format"},
    { MTAB_XTD|MTAB_VDV, 0, "CAPACITY", NULL,
        NULL, &cr_show_cap, NULL, "Card Input Status" },
    {0}
};

DEVICE  cr_dev = {
    "CR", &cr_unit, cr_reg, cr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &cr_reset, &cr_boot, &cr_attach,NULL,
    &cr_dib, DEV_DISABLE | DEV_CARD, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

/* Returns the SDS Internal style BCD of the
 hollerith code or 0x7f if error
 */
uint8 hol_to_sdsbcd(uint16 hol) {
    uint8 bcd;
    
    /* Convert 10,11,12 rows */
    switch (hol & 0xe00) {
        case 0x000:
            if ((hol & 0x1ff) == 0)
                return 060;
            bcd = 000;        // digits 1-9
            break;
        case 0x200:  /* 0 */
            if ((hol & 0x1ff) == 0)
                return 00;      // digit 0
            bcd = 060;        // /,S-Z
            break;
        case 0x400: /* 11 */
            bcd = 040;       // -,J-R
            break;
        case 0x600: /* 11-10 Punch */
            bcd = 052;
            break;
        case 0x800:  /* 12 */
            bcd = 020;       // +,A-I
            break;
        case 0xA00: /* 12-10 Punch */
            bcd = 032;
            break;
        default: /* Double punch in 10,11,12 rows */
            return 0x7f;
    }
    
    hol &= 0x1ff;       /* Mask rows 0-9 */
    /* Check row 8 punched */
    if (hol & 0x2) {
        bcd += 8;
        hol &= ~0x2;
    }
    
    /* Convert rows 0-9 */
    while (hol != 0 && (hol & 0x200) == 0) {
        bcd++;
        hol <<= 1;
    }
    
    /* Any more columns punched? */
    if ((hol & 0x1ff) != 0)
        return 0x7f;
    return bcd;
}

/* device i/o routine   */
t_stat cr_devio (uint32 fnc, uint32 inst, uint32 *dat) {
    UNIT *uptr = &cr_unit;                                   /* get unit ptr */
    int32 new_ch;
    int32 t;
    t_stat r;
    unsigned char chr;
    
    switch (fnc) {                                      /* case function */
        case IO_CONN:                                   /* bufer control EOM */
            new_ch = I_GETEOCH (inst);                  /* get new chan */
            if (new_ch != cr_dib.chan)                  /* wrong chan?  */
                return SCPE_IERR;
            if (sim_is_active(uptr))                    /* busy ?*/
                CRETIOP;
            /* if not reading and no card in reader and hopper has cards */
            if ((uptr->STATUS & (FEEDING|READING)) == 0 &&
                (sim_card_input_hopper_count(uptr) > 0)) {
                uptr->STATUS = FEEDING;
                cr_inst = inst;
                cr_blnt = 0;
                cr_bptr = 0;
                xfr_req = xfr_req & ~XFR_CR;            /* clr xfr flag */
                sim_activate (uptr,2*uptr->wait);        /* start timer  */
            }
            else {
                /* if feeding or reading in different mode */
                if ((inst & 01000) != (cr_inst & 01000)) {
                    if (cr_inst & 01000) {
                        if (cr_chr & 1) {               /* was binary - at 2nd 6 bits
                                                         */
                            cr_bptr++;                  /* skip to next column*/
                        }
                    }
                    cr_chr = 0;
                }
            }
            cr_inst = inst;                             /* save EOM with mode */
            break;
        case IO_EOM1:                                   /* I/O Control EOM */
            new_ch = I_GETEOCH (inst);                  /* get new chan */
            if (new_ch != cr_dib.chan)                  /* wrong chan? err */
                return SCPE_IERR;
            if ((inst & 07700) == 02000) {              /* skip remainder of card*/
                sim_cancel (uptr);                      /* stop timer */
                chan_set_flag (cr_dib.chan, CHF_EOR);   /* end record */
                uptr->STATUS = 0;
                chan_disc (cr_dib.chan);
                xfr_req = xfr_req & ~XFR_CR;            /* clr xfr flag */
            }
            break;
        case IO_DISC:                                   /* disconnect */
            xfr_req = xfr_req & ~XFR_CR;                /* clr xfr flag */
            sim_cancel (uptr);                          /* deactivate unit */
            break;
        case IO_SKS:                                    /* SKS */
            new_ch = I_GETSKCH (inst);                  /* get chan # */
            if (new_ch != cr_dib.chan)                  /* wrong chan? */
                return SCPE_IERR;
            t = I_GETSKCND (inst);                      /* get skip cond */
            switch (t) {                                /* case sks cond */
                case 004:                               /* sks 1100n */
                    // CFT
                    if ((uptr->STATUS & (FEEDING|READING)) == 0 &&
                        (sim_card_input_hopper_count(uptr) > 0))
                        *dat = 1;                       /* skip if not EOF */
                    break;
                case 010:                               /* sks 1200n */
                    // CRT
                    // hopper not empty
                    // no feed or read cycle is in progress
                    if ((uptr->STATUS & (FEEDING|READING)) == 0 &&
                        (sim_card_input_hopper_count(uptr) > 0))
                        *dat = 1;                       /* skip if reader ready */
                    break;
                case 020:                               /* sks 1400n */
                    if ((uptr->STATUS & READING) &&    /* first column test */
                        (((cr_inst & 01000) && (cr_chr < 2)) ||
                        (cr_chr < 1)))
                        *dat = 1;                       /* skip if first column*/
                    break;
            }
            break;
        case IO_READ:
            xfr_req = xfr_req & ~XFR_CR;
            if (cr_blnt == 0) {                     /* first read? */
                r = cr_readrec (uptr);                      /* get data */
                if ((r != SCPE_OK) || (cr_blnt == 0))       /* err, inv reclnt? */
                    return r;
            }
            if (cr_blnt) {
                if (cr_inst & 01000) {
                    if (cr_chr & 1)                  /* binary */
                        chr =cr_buffer[cr_bptr++] & 077;
                    else
                        chr = (cr_buffer[cr_bptr] >> 6) & 077;
                    cr_chr++;
                    *dat = chr & 077;
                }
                else {
                    chr = hol_to_sdsbcd(cr_buffer[cr_bptr++]); /* bcd   */
                    *dat = chr & 077;
                }
            }
            if (cr_bptr >= cr_blnt) {
                /* The card reader doesn't disconnect from the channel until the
                 trailing edge of the card passes the read station so we need to
                 schedule another service event here.  But if it disconnects too
                 soon some programs (Fortran and 850647 (unencode) don't work right
                 and if it takes too long Symbol will try to connect to the LP
                 before it's disconnected.
                 */
                cr_eor = 1;
                sim_cancel(uptr);
                sim_activate (uptr, 50);
            }
            break;
        case IO_WREOR:
        case IO_WRITE:
            CRETINS;
    }
    return SCPE_OK;
}


/* Service routine  */
t_stat cr_svc(UNIT * uptr) {
    xfr_req = xfr_req & ~XFR_CR;
    if (cr_eor) {
        cr_eor = 0;
        sim_cancel (uptr);
        chan_set_flag (cr_dib.chan, CHF_EOR);
        uptr->STATUS = 0;
        return SCPE_OK;
    }
    xfr_req = xfr_req | XFR_CR;
    sim_activate (uptr, 50);
    return SCPE_OK;
}


/* Read start - get next record */
t_stat cr_readrec (UNIT *uptr) {
    int r;
    
    switch(r = sim_read_card(uptr, cr_buffer)) {
        case CDSE_EOF:                  /* parser found tape mark attach */
        case CDSE_EMPTY:                /* not attached or hopper empty */
        case CDSE_ERROR:                /* parser found error during attach */
        default:
            uptr->STATUS = 0;           /* read failed, no card in reader */
            cr_set_err(uptr);
            return r;
        case CDSE_OK:
            uptr->STATUS = READING;
            cr_bptr = 0;
            cr_blnt = 80;
            cr_chr = 0;
            break;
    }
    return SCPE_OK;
}

/* Fatal error */
void cr_set_err (UNIT *uptr) {
    chan_set_flag (cr_dib.chan, CHF_EOR | CHF_ERR);     /* eor, error */
    chan_disc (cr_dib.chan);                /* disconnect */
    xfr_req = xfr_req & ~XFR_CR;            /* clear xfr */
    sim_cancel (uptr);                      /* stop */
    cr_bptr = 0;                            /* buf empty */
    return;
}

t_stat cr_reset (DEVICE *dptr) {
    chan_disc (cr_dib.chan);                /* disconnect */
    cr_bptr = cr_blnt = 0;
    xfr_req = xfr_req & ~XFR_CR;            /* clr xfr flag */
    sim_cancel (&cr_unit);                  /* deactivate unit */
    return SCPE_OK;
}

t_stat cr_attach (UNIT *uptr, CONST char *cptr) {
    return sim_card_attach(uptr, cptr);
}

/* Boot routine - simulate FILL console command */
t_stat cr_boot (int32 unitno, DEVICE *dptr) {
    extern uint32 P, M[];
    
    cr_reset(dptr);
    M[0] = 077777771;       /* -7B */
    M[1] = 007100000;       /* LDX 0 */
    M[2] = 000203606;       /* EOM 3606 read card binary  */
    M[3] = 003200002;       /* WIM 2 */
    M[4] = 000100002;       /* BRU 2 */
    P = 1;                  /* start at 1 */
    return SCPE_OK;
}
   
t_stat cr_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    int n;
    
    if ((n = sim_card_input_hopper_count(uptr)) == 0)
        fprintf(st,"hopper empty");
    else {
        if (n == 1)
            fprintf(st,"1 card");
        else
            fprintf(st,"%d cards",n);
        fprintf(st," in hopper");
    }
    return SCPE_OK;
}
