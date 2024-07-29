/* sigma_cr.c: Sigma 7120/7122/7140 card reader
 
 Copyright (c) 2024, Ken Rector
 
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
 
 cr      7120 card reader
 
 27-Feb-2024       kenr      Initial version
 
    The 7120, 7122 and 7140 card readers are described in the SDS
    reference manual, 900970C.
 
    The simulator expects input data to be a file of 120 byte records with no control
    or other extraneous data, to simulate a punched card deck.  Each 120 byte record
    is translated to 80 16 bit columns with data in the loworder 12 bits.  In
    automatic mode each column (1-1/2 bytes) is translated from a hollerith code
    to an ebcdic character (1 byte).  In binary mode each pair of columns is
    translated to 3 data bytes.
 
    A length error in the input data will not be detected until the end of file
    and results in an Invalid Length and Unusual End status.  CPV sets the ignore
    incorrect length flag so this can cause trouble in the symbiont input process.
 
    Card reader speed for the 7120, 7122 and 7140 machines was 400, 400 and 1500
    cards per minute respectively, or 150, 150 and 40 msec per card.  The simulator
    runs much faster than this, transmitting 80 columns in ~400 instruction cycles,
    or 5 cycles per column.
 
    The cr device capacity indicates the number of cards in the hopper and stacker.
    There is no limit on the number of records in the hopper or stacker.  The
    stacker is never emptied and the count can overflow.  The hopper counter is
    set when a file is attached and reduced as each card is read.
 
    The cardreader is detached from the input file when the hopper count
    reaches zero.
 
    The card reader reports a Data Transmission Error if an incorrect EBCDIC character
    is detected, (more than 1 punch in rows 1-7).
 
 */

#include <sys/stat.h>
#include "sigma_io_defs.h"
#include "sim_card.h"


/* Unit status */

#define CDR_DTE        0x08                            /* data error */

/* Device States */

#define CRS_INIT        0x101                           /* feed card */
#define CRS_END         0x102                           /* end card */

#define UST             u3                              /* unit status */
#define UCMD            u4                              /* unit command */

uint32 cr_bptr;                                         /* buffer index */
uint32 cr_blnt;                                         /* buffer length */
uint32 cr_col;                                          /* current column */
uint32 cr_hopper;                                       /* hopper count */
uint32 *cr_stkptr;                                      /* selected stacker */
uint32 cr_stacker = 0;                                  /* stacker count */
uint32 cr_stacker1 = 0;                                 /* stacker 1 count */
uint32 cr_stacker2 = 0;                                 /* stacker 2 count */
uint16 cr_buffer[80];                                   /* 80 column data */
uint32 cr_ebcdic_init = 0;                              /* translate initial flag */
uint16 hol_to_ebcdic[4096];                             /* translation table */

uint8 cr_ord[] = {                                       /* valid order codes*/
    0, 0, 1, 0, 0, 0, 1, 0,
    0, 0, 1, 0, 0, 0, 1, 0,
    0, 0, 1, 0, 0, 0, 1, 0,
    0, 0, 1, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 1, 0,
    0, 0, 1, 0, 0, 0, 1, 0
};

extern uint32 chan_ctl_time;
extern uint16 ebcdic_to_hol[];


uint32 cr_disp (uint32 op, uint32 dva, uint32 *dvst);
t_stat cr_readrec (UNIT *uptr);
uint32 cr_tio_status (void);
uint32 cr_tdv_status (void);
t_stat cr_chan_err (uint32 st);
t_stat cr_svc (UNIT *uptr);
t_stat cr_reset (DEVICE *dptr);
t_stat cr_attach (UNIT *uptr, CONST char *cptr);
t_stat cr_detach (UNIT *uptr);
t_stat cr_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc);


dib_t   cr_dib = { DVA_CR, cr_disp, 0, NULL };

UNIT    cr_unit = {
    UDATA(&cr_svc, UNIT_ATTABLE | UNIT_RO , 0),
    60
};

REG     cr_reg[] = {
    { DRDATA (BPTR, cr_bptr, 17), PV_LEFT },
    { DRDATA (BLNT, cr_blnt, 17), PV_LEFT },
    { NULL }
};

MTAB    cr_mod[] = {
    {MTAB_XTD | MTAB_VDV, 0, "CHANNEL", "CHANNEL",
        &io_set_dvc,&io_show_dvc,NULL},
    {MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
        &io_set_dva, &io_show_dva, NULL },
    {MTAB_XTD|MTAB_VDV, 0, "CAPACITY", NULL,
        NULL, &cr_show_cap, NULL, "Card hopper size" },
    {0}
};

DEVICE  cr_dev = {
    "CR", &cr_unit, cr_reg, cr_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &cr_reset, &io_boot,&cr_attach,&cr_detach,
    &cr_dib, 0
};


/* Card Reader : IO dispatch routine  */

uint32 cr_disp (uint32 op, uint32 dva, uint32 *dvst)
{
    switch (op) {                                       /* case on op */
            
        case OP_SIO:                                    /* start I/O */
            *dvst = cr_tio_status ();                   /* get status */
            if ((*dvst & DVS_AUTO) && !sim_is_active(&cr_unit))  {
                cr_unit.UCMD = CRS_INIT;                /* start dev thread */
                cr_stkptr = &cr_stacker;
                sim_activate (&cr_unit, 0);
            }
            break;
            
        case OP_TIO:                                    /* test status */
            *dvst = cr_tio_status ();                   /* return status */
            break;
            
        case OP_TDV:                                    /* test status */
            *dvst = cr_tdv_status ();                   /* return status */
            break;
            
        case OP_HIO:                                    /* halt I/O */
            chan_clr_chi (cr_dib.dva);                  /* clear int */
            *dvst = cr_tio_status ();
            if ((*dvst & DVS_DST) != 0) {               /* busy? */
                sim_cancel (&cr_unit);                  /* stop dev thread */
                chan_uen (cr_dib.dva);                  /* uend */
            }
            break;
            
        case OP_AIO:                                    /* acknowledge int */
            chan_clr_chi (cr_dib.dva);                  /* clr int*/
            *dvst = 0;                                  /* no status */
            break;
            
        default:
            *dvst = 0;
            return SCPE_IERR;
    }
    
    return 0;
}

/* Service routine */

t_stat cr_svc (UNIT *uptr)
{
    uint32 cmd = uptr->UCMD;
    t_stat st;
    char   c;
    
    if (cmd == CRS_INIT) {                          /* init state */
        st = chan_get_cmd (cr_dib.dva, &cmd);       /* get order */
        if (CHS_IFERR (st))                         /* bad device id, inactive state */
            return cr_chan_err (st);
        if ((cmd > 0x3e) ||                         /* invalid order? */
            (cr_ord[cmd] == 0)) {
            chan_uen (cr_dib.dva);
            return SCPE_OK;
        }
        uptr->UCMD = cmd;                           /* save order */
        cr_blnt = 0;                                /* empty buffer */
        cr_col = 0;                                 /* initial column */
        switch (cmd & 0x30) {                       /* note stacker */
            case 0x10:
                cr_stkptr = &cr_stacker1;
                break;
            case 0x30:
                cr_stkptr = &cr_stacker2;
                break;
            default:
                cr_stkptr = &cr_stacker;
                break;
        }
        sim_activate (uptr, chan_ctl_time);
        return SCPE_OK;
    }
    if (cmd == CRS_END) {                           /* end of card */
        st = chan_end (cr_dib.dva);                 /* set channel end, inactive */
        if (CHS_IFERR (st))                         /* bad dev/inactive? */
            return cr_chan_err (st);
        ++*cr_stkptr;                               /* add to stacker */
        if (--cr_hopper == 0)                       /* remove from hopper */
            cr_detach(uptr);                        /* end input deck */
        if (st == CHS_CCH) {                        /* command chain? */
            uptr->UCMD = CRS_INIT;                  /* restart thread */
            sim_activate (uptr, chan_ctl_time);
        }
        return SCPE_OK;
    }
    
    if (cr_blnt == 0) {                             /* card arriving? */
        if (cr_readrec (uptr) == 0) {               /* unexpected EOF, inv reclnt? */
            uptr->UCMD = CRS_END;                   /* end state */
            sim_activate (uptr, chan_ctl_time);     /* sched ctlr */
            return SCPE_OK;
        }
        if ((cmd & 0x04) &&                         /* mode change? */
            ((cr_buffer[0] & 0x180) == 0x180)) {
            cmd &= ~0x04;                           /* automatic(EBCDIC) and row 1 2 */
            uptr->UCMD = cmd;                       /* switch to binary mode */
        }
    }
    if (cmd & 0x04) {                               /* mode?   */
        int i = 0;                                  /* invalid punches? */
        int n = cr_buffer[cr_bptr++] & 0x1fc;
        c = (char) hol_to_ebcdic[cr_buffer[cr_bptr-1]]; /* automatic  */
        while (n) {                                 /* Kerninghams bit count alg */
            n &= (n-1);                             /* count bits in row 1-7 */
            i++;
        }
        if (i > 1) {                                /* >2 punches? */
            c = 0x00;                               /* return 0x00 */
            uptr->UST |= CDR_DTE;                   /* Transmission Data Error*/
            chan_set_chf (cr_dib.dva, CHF_XMDE);    /* operational status byte */
        }
    }
    else {
        switch (cr_col % 3) {                      /* binary */
            case 0:
                c = ((cr_buffer[cr_bptr] >> 4) & 0xff);
                break;
            case 1:
                c = ((cr_buffer[cr_bptr] & 0x0f) << 4);
                cr_bptr++;
                c |= ((cr_buffer[cr_bptr] & 0xf00) >> 8);
                break;
            case 2:
                c = (cr_buffer[cr_bptr++] & 0xff);
                break;
        }
    }
    cr_col++;
    st = chan_WrMemB (cr_dib.dva, c);               /* write to memory */
    if (CHS_IFERR (st))                             /* channel error? */
        return cr_chan_err (st);
    if ((st != CHS_ZBC) && (cr_bptr != cr_blnt)) {  /* not done? */
        sim_activate (uptr, chan_ctl_time);         /* continue */
        return SCPE_OK;
    }
    if (((st == CHS_ZBC) ^ (cr_bptr == cr_blnt)) && /* length err? */
        chan_set_chf (cr_dib.dva, CHF_LNTE))        /* Incorrect Length */
        return SCPE_OK;                             /* to operational status byte */
    
    uptr->UCMD = CRS_END;                           /* end state */
    sim_activate (uptr, chan_ctl_time);             /* sched ctlr */
    return SCPE_OK;
}


/*  get next record */

t_stat cr_readrec (UNIT *uptr) {
    
    int    col;
    FILE   *fp = uptr->fileref;
    
    for (col = 0; col < 80; ) {
        int16    i;
        int    c1, c2, c3;
        
        c1 = fgetc (fp);                            /* read 3 bytes */
        c2 = fgetc (fp);
        c3 = fgetc (fp);
        if (feof(fp) || (c1 == EOF) || (c2 == EOF) || (c3 == EOF)) {
            cr_blnt = cr_bptr = 0;
            chan_set_chf (cr_dib.dva, CHF_LNTE);
            return 0;
        }
        i = ((c1 << 4) | ( c2 >> 4)) & 0xFFF;       /* pack 1-1/2 bytes per column */
        cr_buffer[col] = i;
        col++;
        i = (((c2 & 017) << 8) | c3) & 0xFFF;
        cr_buffer[col] = i;
        col++;
    }
    cr_bptr = 0;
    cr_blnt = 80;
    return 80;
}


/* CR status routine */

uint32 cr_tio_status (void)
{
    uint32 st;
    
    st = (cr_unit.flags & UNIT_ATT) ? DVS_AUTO: 0;  /* AUTO : MANUAL */
    if (sim_is_active (&cr_unit))                   /* dev busy? */
        st |= ( DVS_CBUSY | DVS_DBUSY | (CC2 << DVT_V_CC));
    return st;
}

uint32 cr_tdv_status (void)
{
    uint32 st;
    
    if (cr_unit.flags & UNIT_ATT &&
        (cr_hopper > 0))                            /* rdr att? */
        st = cr_unit.UST;
    else
        st = (CC2 << DVT_V_CC);
    return st;
}

/* Channel error */

t_stat cr_chan_err (uint32 st)
{
    chan_uen (cr_dib.dva);
    if (st < CHS_ERR)
        return st;
    return SCPE_OK;
}

/* Reset routine */

t_stat cr_reset (DEVICE *dptr)
{
    int i;
    if (!cr_ebcdic_init) {                          /* initialize translate table */
        for (i = 0; i < 4096; i++)
            hol_to_ebcdic[i] = 0x100;               /* a la sim_card */
        for (i = 0; i < 256; i++) {
            uint16     temp = ebcdic_to_hol[i];
            if (hol_to_ebcdic[temp] != 0x100) {
                fprintf(stderr, "Translation error %02x is %03x and %03x\n",
                    i, temp, hol_to_ebcdic[temp]);
            } else {
                hol_to_ebcdic[temp] = i;
            }
        }
        cr_ebcdic_init = 1;
    }
    sim_cancel (&cr_unit);                          /* stop dev thread */
    chan_reset_dev (cr_dib.dva);                    /* clr int, active */
    return SCPE_OK;
}

/* Attach routine */

t_stat cr_attach (UNIT *uptr, CONST char *cptr)
{
    char *saved_filename;
    int r;

    saved_filename = uptr->filename;
    uptr->filename = NULL;
    if ((r = attach_unit(uptr, cptr)) != SCPE_OK) {
        uptr->filename = saved_filename;
        return r;
        }
    r = sim_fsize(uptr->fileref);
    if ((r % 120) != 0) {                           /* multiple of 120 byte cards? */
        detach_unit(uptr);
        fprintf(stderr,"CR file size error\n");
        return SCPE_IOERR;
    }
    cr_hopper = r / 120;
    return SCPE_OK;
}

/* Detach routine */

t_stat cr_detach (UNIT *uptr)
{
    cr_hopper = 0;
    return  detach_unit(uptr);
}

t_stat cr_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    
    if (cr_hopper == 0)
        fprintf(st,"hopper empty");
    else {
        if (cr_hopper == 1)
            fprintf(st,"1 card");
        else
            fprintf(st,"%d cards",cr_hopper);
        fprintf(st," in hopper");
    }
    fprintf(st,"\nNormal Stacker %d\n",cr_stacker);
    fprintf(st,"Alt Stacker 1 %d\n",cr_stacker1);
    fprintf(st,"Alt Stacker 2 %d",cr_stacker2);
    return SCPE_OK;
}
