/* sds_cp.c  - SDS-930 Card Punch

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
 This card punch simulator uses sim_card.c to write output records
 in CBN format.  Data is passed directly to sim_card.c when binary
 mode is specified by the buffer control EOM.  When BCD mode is
 specified by the EOM, output data is translated into Hollerith code
 from SDS Internal Code as defined by the SDS 930 Computer Reference Manual
 
 The SDS card punch protocol defined by the 930 Computer Reference manual
 specifies that the output image be sent to the buffer 12 times, once
 for each row.  In this simulator the card image is only written after
 termination (TOP) of the twelfth image output.

 The Symbol assembler punch routine uses the PBT (Punch Buffer Test)
 before issueing a connect EOM to determine if it needs to write 12 rows
 per card, or just 1.  To make Symbol work right we always return TRUE,
 (skip) for this test.
  
 I can't find anything in the computer reference manuals that describes
 how this should work.  Why did Symbol do this?
 
 */


#include "sds_defs.h"
#include "sim_card.h"

#define CARD_IN_PUNCH 00004000          /* Card ready to punch */

#define STATUS      u3

extern uint32 xfr_req;
extern int32 stop_invins, stop_invdev, stop_inviop;

uint16  cp_buffer[80];                  /* card output image */
int32   cp_bptr = 0;                    /* buf ptr */
int32   cp_blnt = 0;                    /* buf length */
int32   cp_row = 0;                     /* row counter */
int32   cp_chr = 0;
int32  cp_eor;
int32  cp_inst;                        /* saved instr */

t_stat  cp_devio(uint32 fnc, uint32 inst, uint32 *dat);
t_stat  cp_svc(UNIT *);
t_stat  cp_attach(UNIT * uptr, CONST char *file);
t_stat  cp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat  cp_detach(UNIT * uptr);
t_stat  cp_wrend(UNIT * uptr);
t_stat  cp_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void    cp_set_err (UNIT *uptr);



DSPT cp_tplt[] = { { 1, 0 }, { 0, 0 }  }; /* template */

DIB cp_dib = { CHAN_W, DEV_CP, XFR_CP, cp_tplt, &cp_devio };

UNIT cp_unit = {UDATA(&cp_svc,  UNIT_ATTABLE , 0), 2000 };

MTAB  cp_mod[] = {
    {MTAB_XTD | MTAB_VDV, 0, "CHANNEL", "CHANNEL",
        &set_chan, &show_chan,
        NULL, "Device Channel"},
    {MTAB_XTD | MTAB_VDV, 0, "FORMAT", "FORMAT",
        &sim_card_set_fmt, &sim_card_show_fmt, 
        NULL,"Card Format"},
    { MTAB_XTD|MTAB_VDV, 0, "CAPACITY", NULL,
        NULL, &cp_show_cap, NULL, "Stacker Count" },
    {0}
};

REG  cp_reg[] = {
    { BRDATA (BUFF, cp_buffer, 16, 16, sizeof(cp_buffer)/sizeof(*cp_buffer)), REG_HRO},
    { DRDATA (BPTR, cp_bptr, 18), PV_LEFT },
    { DRDATA (BLNT, cp_blnt, 18), PV_LEFT },
    { FLDATA (XFR, xfr_req, XFR_V_CP) },
    { ORDATA (INST, cp_inst, 24) },
    { DRDATA (POS, cp_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
};

DEVICE cp_dev = {
    "CP", &cp_unit, cp_reg, cp_mod,
    1, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cp_attach, &cp_detach,
    &cp_dib, DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL
};

/* Convert SDS BCD character into hollerith code */

uint16 sdsbcd_to_hol(uint8 bcd) {
    uint16      hol;
    
    /* Handle space correctly */
    if (bcd == 0)                       /* 0 to row 10 */
        return 0x200;
    if (bcd == 060)                     /* 60 no punch */
        return 0;
    
    /* Convert to top column */
    switch (bcd & 060) {
        default:
        case 000:
            hol = 0x000;                /* no zone */
            break;
        case 020:
            hol = 0x800;                /*   0x200  row 12 */
            break;
        case 040:
            hol = 0x400;                /* row 11  */
            break;
        case 060:
            hol = 0x200;                /* row 10  */
            break;
    }
    
    /* Convert to 0-9 row */
    bcd &= 017;
    if (bcd > 9) {
        hol |= 0x2;                     /* row 8 */
        bcd -= 8;
    }
    if (bcd != 0)
        hol |= 1 << (9 - bcd);
    return hol;
}

t_stat cp_devio(uint32 fnc, uint32 inst, uint32 *dat) {
    UNIT   *uptr = &cp_unit;
    int32  new_ch;
    uint8  chr;
    t_stat r;
    uint32 t;
    
    switch (fnc) {
        case IO_CONN:
            new_ch = I_GETEOCH (inst);      /* get new chan */
            if (new_ch != cp_dib.chan)      /* wrong chan? err */
                return SCPE_IERR;
            if (sim_is_active(uptr))
                CRETIOP;
            if (uptr->flags & UNIT_ATT) {
                cp_inst = inst;
                cp_blnt = 0;
                cp_bptr = 0;
                xfr_req = xfr_req & ~XFR_CP;    /* clr xfr flag */
                sim_activate (uptr, uptr->wait);    /* start timer  */
            }
            else {
                cp_set_err (uptr);          /* no, err, disc */
                CRETIOP;
            }
            break;
        case IO_EOM1:                       /* I/O Control EOM */
            break;
        case IO_DISC:                       /* disconnect  TOP */
            xfr_req = xfr_req & ~XFR_CP;    /* clr xfr flag */
            cp_row++;
            if (cp_row >= 12) {
                if ((r = cp_wrend(uptr)) != SCPE_OK)
                    return r;
                uptr->STATUS &= ~CARD_IN_PUNCH;
            }
            sim_cancel (uptr);              /* deactivate unit */
            break;
        case IO_WREOR:                       /* write eor */
            break;
        case IO_SKS:
            new_ch = I_GETSKCH (inst);      /* get chan # */
            if (new_ch != cp_dib.chan)      /* wrong chan? */
                return SCPE_IERR;
            t = I_GETSKCND (inst);          /* get skip cond */
            switch (t) {                    /* case sks cond */
                case 010:                   /* sks 12046 */
                    // PBT
                    // /* skip if punch buffer empty */
                    *dat = 1;
                    break;
                case 020:                   /* sks 14046 */
                    // CPT
                    /* skip if punch is ready to accept connection */
                    if ((uptr->flags & UNIT_ATT) &&
                        !(uptr->STATUS & CARD_IN_PUNCH))
                        *dat = 1;
                    break;
            }
            break;
        case IO_WRITE:
            if (!(uptr->STATUS & CARD_IN_PUNCH))
                break;
            chr = (*dat) & 077;
            xfr_req = xfr_req & ~XFR_CP;    /* clr xfr flag */
            if (cp_bptr < cp_blnt) {
                if (cp_inst & 01000) {
                    if (cp_chr & 1)                 /* column binary */
                        cp_buffer[cp_bptr++] |= chr;
                    else
                        cp_buffer[cp_bptr] = (chr << 6);
                    cp_chr++;
                }
                else  {
                    cp_buffer[cp_bptr++] = sdsbcd_to_hol(chr); /* bcd */
                }
                chan_set_ordy (cp_dib.chan);
            }
            break;
        case IO_READ:
            CRETINS;
    }
    
    return SCPE_OK;
}


/* punch service */
t_stat cp_svc(UNIT *uptr) {
    
    uptr->STATUS |= CARD_IN_PUNCH;
    cp_bptr = 0;
    cp_blnt = 80;
    cp_chr = 0;
    chan_set_ordy (cp_dib.chan);
    return SCPE_OK;
}

t_stat cp_wrend(UNIT * uptr) {
    t_stat st;
    
    st = sim_punch_card(uptr, cp_buffer);
    cp_row = 0;
    if (st != CDSE_OK) {
        cp_set_err(uptr);
        return SCPE_IOERR;
    }
    uptr->STATUS = 0;
    return SCPE_OK;
}

/* Fatal error */
void cp_set_err (UNIT *uptr)
{
    chan_set_flag (cp_dib.chan, CHF_EOR | CHF_ERR);     /* eor, error */
    chan_disc (cp_dib.chan);                /* disconnect */
    xfr_req = xfr_req & ~XFR_CP;            /* clear xfr */
    sim_cancel (uptr);                      /* stop */
    cp_bptr = 0;                            /* buf empty */
    return;
}

t_stat cp_attach(UNIT * uptr, CONST char *cptr) {
    t_stat r;
    
    sim_card_set_fmt (uptr,0,"CBN",NULL);
    if ((r = sim_card_attach(uptr, cptr)) != SCPE_OK)
        return r;
    cp_row = 0;
    return SCPE_OK;
}

t_stat cp_detach(UNIT * uptr) {
    
    if (uptr->STATUS & CARD_IN_PUNCH)
        sim_punch_card(uptr, cp_buffer);
    return sim_card_detach(uptr);
}

/* Channel assignment routines */

t_stat cp_set_chan (UNIT *uptr, int32 val, CONST char *sptr, void *desc)
{
    t_stat r;
    r = set_chan (uptr, val, sptr, desc);
    return r;
}

t_stat cp_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    int n;
    
    if ((n = sim_card_output_hopper_count(uptr)) == 0)
        fprintf(st,"stacker empty");
    else {
        if (n == 1)
            fprintf(st,"1 card");
        else
            fprintf(st,"%d cards",n);
        fprintf(st," in stacker");
    }
    return SCPE_OK;
}
