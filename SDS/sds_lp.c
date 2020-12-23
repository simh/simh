/* sds_lp.c: SDS 940 line printer simulator

   Copyright (c) 2001-2008, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   lpt          line printer

   24-Nov-08    RMS     Fixed loss of carriage control position on space op
   19-Jan-07    RMS     Added UNIT_TEXT flag
   25-Apr-03    RMS     Revised for extended file support
*/

#include "sds_defs.h"

#define LPT_V_LN        9
#define LPT_M_LN        07
#define LPT_GETLN(x)    (((x) >> LPT_V_LN) & LPT_M_LN)
#define CHP(ch,val)     ((val) & (1 << (ch)))           /* CCL chan test */
#define SET_XFR         1                               /* set xfr */
#define SET_EOR         2                               /* print, set eor */
#define SET_SPC         4                               /* space */

extern uint32 xfr_req;
extern int32 stop_invins, stop_invdev, stop_inviop;
int32 lpt_spc = 0;                                      /* space instr */
int32 lpt_sta = 0;                                      /* timeout state */
int32 lpt_bptr = 0;                                     /* line buf ptr */
int32 lpt_err = 0;                                      /* error */
int32 lpt_ccl = 1, lpt_ccp = 0;                         /* cctl lnt, ptr */
int32 lpt_ctime = 10;                                   /* char time */
int32 lpt_ptime = 1000;                                 /* print time */
int32 lpt_stime = 10000;                                /* space time */
int32 lpt_stopioe = 1;                                  /* stop on err */
char lpt_buf[LPT_WIDTH + 1] = { 0 };                    /* line buffer */
uint8 lpt_cct[CCT_LNT] = { 0377 };                      /* car ctl tape */
DSPT lpt_tplt[] = {                                     /* template */
    { 1, 0 },
    { 0, 0 }
    };

t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat lpt_crctl (UNIT *uptr, int32 ch);
t_stat lpt_space (UNIT *uptr, int32 cnt);
t_stat lpt_status (UNIT *uptr);
t_stat lpt_bufout (UNIT *uptr);
void lpt_end_op (int32 fl);
t_stat lpt (uint32 fnc, uint32 inst, uint32 *dat);
int8 sds_to_ascii(int8 c);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { CHAN_W, DEV_LPT, XFR_LPT, lpt_tplt, &lpt };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0)
    };

REG lpt_reg[] = {
    { BRDATA (BUF, lpt_buf, 8, 8, LPT_WIDTH) },
    { DRDATA (BPTR, lpt_bptr, 8), PV_LEFT },
    { FLDATA (XFR, xfr_req, XFR_V_LPT) },
    { FLDATA (ERR, lpt_err, 0) },
    { ORDATA (STA, lpt_sta, 3) },
    { BRDATA (CCT, lpt_cct, 8, 8, CCT_LNT) },
    { DRDATA (CCTP, lpt_ccp, 8), PV_LEFT },
    { DRDATA (CCTL, lpt_ccl, 8), REG_RO + PV_LEFT },
    { ORDATA (SPCINST, lpt_spc, 24) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (CTIME, lpt_ctime, 24), REG_NZ + PV_LEFT },
    { DRDATA (PTIME, lpt_ptime, 24), REG_NZ + PV_LEFT },
    { DRDATA (STIME, lpt_stime, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, lpt_stopioe, 0) },
    { NULL }
    };

MTAB lpt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, NULL,
    &lpt_dib, DEV_DISABLE
    };

/* Line printer routine

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result

   The line printer is an asynchronous output device, that is, it
   can never set the channel rate error flag.
*/

t_stat lpt (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 i, t, new_ch;
char asc;

switch (fnc) {                                          /* case function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != lpt_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        for (i = 0; i < LPT_WIDTH; i++)                 /* clr buffer */
            lpt_buf[i] = 0;
        lpt_bptr = 0;                                   /* clr buf ptr */
        lpt_err = 0;                                    /* err = 0 */
        xfr_req = xfr_req & ~XFR_LPT;                   /* clr xfr flag */
        lpt_sta = lpt_sta | SET_XFR;                    /* need xfr */
        sim_activate (&lpt_unit, lpt_ctime);            /* start timer */
        break;

    case IO_EOM1:                                       /* EOM mode 1 */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != lpt_dib.chan)                     /* wrong chan? */
            CRETIOP;
        if (inst & 0400) {                              /* space? */
            lpt_spc = inst;                             /* save instr */
            lpt_sta = lpt_sta | SET_SPC;                /* need space */
            sim_cancel (&lpt_unit);                     /* cancel timer */
            sim_activate (&lpt_unit, lpt_stime);        /* start timer */
            }
        break;

    case IO_DISC:                                       /* disconnect */
        lpt_end_op (0);                                 /* normal term */
        return lpt_bufout (&lpt_unit);                  /* dump output */

    case IO_WREOR:                                      /* write eor */
        lpt_sta = (lpt_sta | SET_EOR) & ~SET_XFR;       /* need eor */
        sim_activate (&lpt_unit, lpt_ptime);            /* start timer */
        break;

    case IO_SKS:                                        /* SKS */
        new_ch = I_GETSKCH (inst);                      /* sks chan */
        if (new_ch != lpt_dib.chan)                     /* wrong chan? */
            return SCPE_IERR;
        t = I_GETSKCND (inst);                          /* sks cond */
        if (((t == 020) && (!CHP (7, lpt_cct[lpt_ccp]))) || /* 14062: !ch 7 */
            ((t == 010) && (lpt_unit.flags & UNIT_ATT)) ||  /* 12062: !online */
            ((t == 004) && !lpt_err))                   /* 11062: !err */
            *dat = 1;
        break;

    case IO_WRITE:                                      /* write */
        asc = sds_to_ascii(*dat);                       /* convert data */
        xfr_req = xfr_req & ~XFR_LPT;                   /* clr xfr flag */
        if (lpt_bptr < LPT_WIDTH)                       /* store data */
            lpt_buf[lpt_bptr++] = asc;
        lpt_sta = lpt_sta | SET_XFR;                    /* need xfr */
        sim_activate (&lpt_unit, lpt_ctime);            /* start ch timer */
        break;

    default:
        CRETINS;
        }

return SCPE_OK;
}

/* Unit service and write */

t_stat lpt_svc (UNIT *uptr)
{
t_stat r = SCPE_OK;

if (lpt_sta & SET_XFR)                                  /* need lpt xfr? */
    chan_set_ordy (lpt_dib.chan);
if (lpt_sta & SET_EOR) {                                /* printing? */
    chan_set_flag (lpt_dib.chan, CHF_EOR);              /* set eor flg */
    r = lpt_bufout (uptr);                              /* output buf */
    }
if (lpt_sta & SET_SPC) {                                /* spacing? */
    if (uptr->flags & UNIT_ATT) {                       /* attached? */
        int32 ln = LPT_GETLN (lpt_spc);                 /* get lines, ch */
        if (lpt_spc & 0200)                             /* n lines? */
            lpt_space (uptr, ln);                       /* upspace */
        else lpt_crctl (uptr, ln);                      /* carriage ctl */
        }
    r = lpt_status (uptr);                              /* update status */
    }
lpt_sta = 0;                                            /* clear state */
return r;
}

/* Trim and output buffer */

t_stat lpt_bufout (UNIT *uptr)
{
int32 i;

if ((uptr->flags & UNIT_ATT) && lpt_bptr) {             /* attached? */
    for (i = LPT_WIDTH - 1; (i >= 0) && (lpt_buf[i] == ' '); i--)
        lpt_buf[i] = 0;                                 /* trim line */
    fputs (lpt_buf, uptr->fileref);                     /* write line */
    lpt_bptr = 0;
    }
return lpt_status (uptr);                               /* return status */
}

/* Status update after I/O */

t_stat lpt_status (UNIT *uptr)
{
if (uptr->flags & UNIT_ATT) {                           /* attached? */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (uptr->fileref)) {                       /* I/O error? */
        lpt_end_op (CHF_EOR | CHF_ERR);                 /* set err, disc */
        sim_perror ("LPT I/O error");                       /* print msg */
        clearerr (uptr->fileref);
        return SCPE_IOERR;                              /* ret error */
        }
    }
else {
    lpt_end_op (CHF_EOR | CHF_ERR);                     /* set err, disc */
    CRETIOE (lpt_stopioe, SCPE_UNATT);                  /* ret error */
    }
return SCPE_OK;
}

/* Terminate LPT operation */

void lpt_end_op (int32 fl)
{
if (fl)                                                 /* set flags */
    chan_set_flag (lpt_dib.chan, fl);
xfr_req = xfr_req & ~XFR_LPT;                           /* clear xfr */
sim_cancel (&lpt_unit);                                 /* stop */
if (fl & CHF_ERR) {                                     /* error? */
    chan_disc (lpt_dib.chan);                           /* disconnect */
    lpt_err = 1;                                        /* set lpt err */
    }
return;
}

/* Carriage control */

t_stat lpt_crctl (UNIT *uptr, int32 ch)
{
int32 i, j;

if ((ch == 1) && CHP (ch, lpt_cct[0])) {                /* top of form? */
    fputs ("\f\n", uptr->fileref);                      /* ff + nl */
    lpt_ccp = 0;                                        /* top of page */
    return SCPE_OK;
    }
for (i = 1; i < lpt_ccl + 1; i++) {                     /* sweep thru cct */
    lpt_ccp = (lpt_ccp + 1) % lpt_ccl;                  /* adv pointer */
    if (CHP (ch, lpt_cct[lpt_ccp])) {                   /* chan punched? */
        for (j = 0; j < i; j++)
            fputc ('\n', uptr->fileref);
        return SCPE_OK;
        }
    }
return STOP_CCT;                                        /* runaway channel */
}

/* Spacing */

t_stat lpt_space (UNIT *uptr, int32 cnt)
{
int32 i;

if (cnt == 0)
     fputc ('\r', uptr->fileref);
else {
    for (i = 0; i < cnt; i++)
        fputc ('\n', uptr->fileref);
    lpt_ccp = (lpt_ccp + cnt) % lpt_ccl;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
chan_disc (lpt_dib.chan);                               /* disconnect */
lpt_spc = 0;                                            /* clr state */
lpt_sta = 0;
xfr_req = xfr_req & ~XFR_LPT;                           /* clr xfr flag */
sim_cancel (&lpt_unit);                                 /* deactivate */
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
lpt_ccp = 0;                                            /* top of form */
return attach_unit (uptr, cptr);
}
