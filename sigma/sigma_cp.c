/* sigma_cp.c Sigma 7160 Card Punch

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

   cp       7160 300 cpm card punch

   The 7160 card punch is described in the SDS Reference Manual, 900971A.

   The simulator writes 120 byte records to the cp attached output file.  There
   is no control or formatting meta data included in the file.
 
   Output requests in EBCDIC mode produce hollerith encoded card images.
   Output in binary mode produces column binary card images.
 
   Capacity describes the number of punched cards in the outout stacker.  This
   accumulates indefinitely.  It is not reset when the output file is detached.
  
*/

#include "sigma_io_defs.h"
#include "sim_card.h"


/* Local unit Commands */

#define CPS_INIT        0x101
#define CPS_STOP        0x00            /* stop*/
#define CPS_PU01        0x01            /* punch binary normal */
#define CPS_PU05        0x05            /* punch ebcdic normal */
#define CPS_PU09        0x09            /* punch binary, error alternate */
#define CPS_PU0D        0x0d            /* punch ebcdic, error alternate */
#define CPS_PU11        0x11            /* punch binary, alternate */
#define CPS_PU15        0x15            /* punch ebcdic, alternate */
#define CPS_PU19        0x19            /* punch binary, alternate */
#define CPS_PU1D        0x1d            /* punch ebcdic, alternate */
#define CPS_STOPI       0x80            /* stop and interrupt */


#define UST         u3                  /* unit status */
#define UCMD        u4                  /* unit command */

#define LEN         120                 /* output length */

#define DPS_UEN     0x04                /* unusual end occured */

char    cp_buffer[LEN];                 /* card output image */
int32   cp_bptr = 0;                    /* buf ptr */
int32   cp_row = 0;                     /* row counter */
int32   cp_stacker1;
int32   cp_stacker2;

uint32  cp_disp(uint32 fnc, uint32 inst, uint32 *dat);
uint32  cp_tio_status(void);
uint32  cp_tdv_status(void);
t_stat  cp_chan_err (uint32 st);
t_stat  cp_svc(UNIT *);
t_stat  cp_reset (DEVICE *dptr);
t_stat  cp_attach(UNIT * uptr, CONST char *file);
t_stat  cp_detach(UNIT * uptr);
t_stat  cp_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

uint8 cp_op[] = {
    1, 1, 0, 0, 0, 1, 0, 0,
    0, 1, 0, 0, 0, 1, 0, 0,
    0, 1, 0, 0, 0, 1, 0, 0,
    0, 1, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1
};


extern uint32 chan_ctl_time;
extern uint16 ebcdic_to_hol[];

dib_t cp_dib = { DVA_CP, cp_disp, 0, NULL };

UNIT cp_unit = {UDATA(&cp_svc,  UNIT_ATTABLE , 0), 2000 };

MTAB  cp_mod[] = {
    {MTAB_XTD | MTAB_VDV, 0, "CHANNEL", "CHANNEL",
        &io_set_dvc,&io_show_dvc,NULL},
    { MTAB_XTD|MTAB_VDV, 0, "CAPACITY", NULL,
        NULL, &cp_show_cap, NULL, "Punch stacker Count" },
    {0}
};

REG  cp_reg[] = {
    { BRDATA (BUFF, cp_buffer, 8, 8, sizeof(cp_buffer)/sizeof(*cp_buffer)), REG_HRO},
    { DRDATA (BPTR, cp_bptr, 18), PV_LEFT },
    { DRDATA (POS, cp_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
};

DEVICE cp_dev = {
    "CP", &cp_unit, cp_reg, cp_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &cp_reset, NULL, &cp_attach, &cp_detach,
    &cp_dib, 0
};

/* Card Punch : IO Dispatch rotine*/

uint32 cp_disp (uint32 op, uint32 dva, uint32 *dvst) {
   
    switch (op) {

        case OP_SIO:                                        /* start I/O */
            *dvst = cp_tio_status ();                       /* get status */
            /* if automatic mode and ready */
            if ((*dvst & DVS_AUTO) && !sim_is_active(&cp_unit))  {
                cp_unit.UCMD = CPS_INIT;                    /* start dev thread */
                cp_unit.UST = 0;
                cp_row = 0;
                sim_activate (&cp_unit, 0);
            }
            break;
            
        case OP_TIO:                                        /* test status */
            *dvst = cp_tio_status ();                       /* return status */
            break;
            
        case OP_TDV:                                        /* test status */
            *dvst = cp_tdv_status ();                       /* return status */
            break;
            
        case OP_HIO:                                        /* halt I/O */
            chan_clr_chi (cp_dib.dva);                      /* clear int */
            *dvst = cp_tio_status ();                       /* get status */
            if ((*dvst & DVS_DST) != 0) {                   /* busy? */
                sim_cancel (&cp_unit);                      /* stop dev thread */
                cp_unit.UST = DPS_UEN;
                chan_uen (cp_dib.dva);                      /* uend */
                cp_unit.UCMD = 0;                           /* ctlr idle */
            }
            break;
            
        case OP_AIO:                                        /* acknowledge int */
            chan_clr_chi (cp_dib.dva);                      /* clr int*/
            *dvst = 0;                                      /* no status */
            break;
            
        default:
            *dvst = 0;
            return SCPE_IERR;
    }
    
    return 0;
}


/* punch service */
t_stat cp_svc(UNIT *uptr) {
    uint32 cmd;
    uint32 dva = cp_dib.dva;
    uint32 st;
    uint32 i;
    uint32 c;
    
    if (uptr->UCMD == CPS_INIT) {                           /* init state? */
        st = chan_get_cmd (cp_dib.dva, &cmd);               /* get order */
        if (CHS_IFERR (st))
            return cp_chan_err (st);
        if ((cmd > 0x80) ||
            (cp_op[cmd] == 0)) {                            /* invalid order? */
            uptr->UST = DPS_UEN;
            chan_uen (cp_dib.dva);                          /* report uend */
            return SCPE_OK;
        }
        uptr->UCMD = cmd;
        sim_activate (uptr, chan_ctl_time);
        return SCPE_OK;
    }

    switch (uptr->UCMD) {
        case CPS_PU01:
        case CPS_PU05:
        case CPS_PU09:
        case CPS_PU0D:
        case CPS_PU11:
        case CPS_PU15:
        case CPS_PU19:
        case CPS_PU1D:
            if (cp_row++ == 11) {                   /* last row? */
                memset(cp_buffer,0,sizeof(cp_buffer));
                if (uptr->UCMD & 0x4) {             /* mode?  */
                    i = 0;                          /* ebcdic */
                    while (i < LEN) {
                        unsigned short int col;
                        st = chan_RdMemB(dva,&c);
                        if (CHS_IFERR (st))         /* channel error? */
                            return cp_chan_err (st);
                        col = ebcdic_to_hol[c]; /* byte and 1/2  */
                        cp_buffer[i++] = (col >> 4) & 0xff;
                        cp_buffer[i] = (col & 0x0f) << 4;
                        if (st == CHS_ZBC)          /* end request size? */
                            break;
                        st = chan_RdMemB(dva,&c);
                        if (CHS_IFERR (st))         /* channel error? */
                            return cp_chan_err (st);
                        col = ebcdic_to_hol[c];    /* 1/2  and byte */
                        cp_buffer[i++] |= (col >> 8) & 0xf;
                        cp_buffer[i++] = (col & 0xff);
                        if (st == CHS_ZBC)          /* end request size? */
                            break;
                    }
                }
                else {
                    for (i = 0; i < LEN; i++) {     /* Binary */
                        st = chan_RdMemB(dva,&c);
                        if (CHS_IFERR (st))         /* channel error? */
                            return cp_chan_err (st);
                        cp_buffer[i] = c;
                        if (st == CHS_ZBC)          /* end request size? */
                            break;

                    }
                }
                sim_fwrite (cp_buffer, LEN, 1, uptr->fileref);
                cp_stacker1++;
                chan_set_cm (dva);                  /* set Chaining Modifier flag */
            }
            st = chan_end (dva);
            uptr->UCMD = CPS_INIT;
            break;
        case CPS_STOPI:
            chan_set_chi(dva,CHI_END);              /* interrupt */
        case CPS_STOP:
            st = chan_end (dva);                    /* set channel end */
            if (CHS_IFERR (st))                     /* channel error? */
                return cp_chan_err (st);
            return SCPE_OK;                         /* done */
    }
    sim_activate (uptr, chan_ctl_time);
    return SCPE_OK;
}

/* CP status routine */

uint32 cp_tio_status (void)
{
    uint32 st;
    
    st = cp_unit.flags & UNIT_ATT? DVS_AUTO: 0;     /* AUTO : MANUAL */
    if (sim_is_active (&cp_unit))                   /* dev busy? */
        st |= ( DVS_DBUSY | (CC2 << DVT_V_CC));
    st |= cp_unit.UST;                              /* uend? */
    return st;
}

uint32 cp_tdv_status (void)
{

    if (cp_unit.flags & UNIT_ATT)                   /* rdr att? */
        return cp_unit.UST;                         /* uend  */
    return CC2 << DVT_V_CC;
}

/* Channel error */

t_stat cp_chan_err (uint32 st)
{
    cp_unit.UST = DPS_UEN;
    chan_uen (cp_dib.dva);                          /* uend */
    if (st < CHS_ERR)
        return st;
    return SCPE_OK;
}

/* Reset routine  */

t_stat cp_reset (DEVICE *dptr)
{

    sim_cancel (&cp_unit);                          /* stop dev thread */
    cp_bptr = 0;
    cp_row = 0;
    chan_reset_dev (cp_dib.dva);                    /* clr int, active */
    return SCPE_OK;
}

/* Attach routine */

t_stat cp_attach(UNIT * uptr, CONST char *cptr) {

    return  (attach_unit(uptr, cptr));
}

t_stat cp_detach(UNIT * uptr) {
    
    return  (detach_unit(uptr));
}


t_stat cp_show_cap (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
    int n;
    
    if ((n = cp_stacker1) == 0)
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
