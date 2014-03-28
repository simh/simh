/* sds_stddev.c: SDS 940 standard devices

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

   ptr          paper tape reader
   ptp          paper tape punch
   tti          keyboard
   tto          teleprinter

   29-Dec-03    RMS     Added console backpressure support
   25-Apr-03    RMS     Revised for extended file support
*/

#include "sds_defs.h"
#include "sim_tmxr.h"

#define TT_CR           052                             /* typewriter */
#define TT_TB           072
#define TT_BS           032

extern uint32 xfr_req;
extern int32 stop_invins, stop_invdev, stop_inviop;
int32 ptr_sor = 0;                                      /* start of rec */
int32 ptr_stopioe = 1;                                  /* stop on err */
int32 ptp_ldr = 0;                                      /* no leader */
int32 ptp_stopioe = 1;
DSPT std_tplt[] = { { 1, 0 }, { 0, 0 }  };              /* template */

DEVICE ptr_dev, ptp_dev;
t_stat ptr (uint32 fnc, uint32 inst, uint32 *dat);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
void ptr_set_err (void);
t_stat ptp (uint32 fnc, uint32 inst, uint32 *dat);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptp_out (int32 dat);
void ptp_set_err (void);
t_stat tti (uint32 fnc, uint32 inst, uint32 *dat);
t_stat tti_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto (uint32 fnc, uint32 inst, uint32 *dat);
t_stat tto_svc (UNIT *uptr);
t_stat tto_reset (DEVICE *dptr);

extern const int8 ascii_to_sds[128];
extern const int8 sds_to_ascii[64];
extern const int8 odd_par[64];

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit
   ptr_reg      PTR register list
*/

DIB ptr_dib = { CHAN_W, DEV_PTR, XFR_PTR, std_tplt, &ptr };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 7) },
    { FLDATA (XFR, xfr_req, XFR_V_PTR) },
    { FLDATA (SOR, ptr_sor, 0) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB ptr_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, NULL, NULL,
    &ptr_dib, DEV_DISABLE
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit
   ptp_reg      PTP register list
*/

DIB ptp_dib = { CHAN_W, DEV_PTP, XFR_PTP, std_tplt, &ptp };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 7) },
    { FLDATA (XFR, xfr_req, XFR_V_PTP) },
    { FLDATA (LDR, ptp_ldr, 0) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, NULL },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    &ptp_dib, DEV_DISABLE
    };

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit
   tti_reg      TTI register list
*/

DIB tti_dib = { CHAN_W, DEV_TTI, XFR_TTI, std_tplt, &tti };

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATA (BUF, tti_unit.buf, 6) },
    { FLDATA (XFR, xfr_req, XFR_V_TTI) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, &tti_dib },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, 0
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit
   tto_reg      TTO register list
*/

DIB tto_dib = { CHAN_W, DEV_TTO, XFR_TTO, std_tplt, &tto };

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, 6) },
    { FLDATA (XFR, xfr_req, XFR_V_TTO) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", "CHANNEL",
      &set_chan, &show_chan, &tto_dib },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, 0
    };

/* Paper tape reader

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result

   The paper tape reader is a streaming input device.  Once started, it
   continues to read until disconnected.  Leader before the current record
   is ignored; leader after the current record sets channel EndOfRecord.
*/

t_stat ptr (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 new_ch;

switch (fnc) {                                          /* case function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != ptr_dib.chan)                     /* inv conn? err */
            return SCPE_IERR;
        ptr_sor = 1;                                    /* start of rec */
        xfr_req = xfr_req & ~XFR_PTR;                   /* clr xfr flag */
        sim_activate (&ptr_unit, ptr_unit.wait);        /* activate */
        break;

    case IO_DISC:                                       /* disconnect */
        ptr_sor = 0;                                    /* clear state */
        xfr_req = xfr_req & ~XFR_PTR;                   /* clr xfr flag */
        sim_cancel (&ptr_unit);                         /* deactivate unit */
        break;

    case IO_READ:                                       /* read */
        xfr_req = xfr_req & ~XFR_PTR;                   /* clr xfr flag */
        *dat = ptr_unit.buf & 077;                      /* get buf data */
        if (ptr_unit.buf != odd_par[*dat])              /* good parity? */
            chan_set_flag (ptr_dib.chan, CHF_ERR);      /* no, error */
        break;

    case IO_WREOR:                                      /* write eor */
        break;

    case IO_EOM1:                                       /* EOM mode 1*/
    case IO_WRITE:                                      /* write */
        CRETINS;                                        /* error */
        }

return SCPE_OK;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ptr_set_err ();                                     /* no, err, disc */
    CRETIOE (ptr_stopioe, SCPE_UNATT);
    }
if ((temp = getc (ptr_unit.fileref)) == EOF) {          /* end of file? */
    ptr_set_err ();                                     /* yes, err, disc */
    if (feof (ptr_unit.fileref)) {                      /* end of file? */
        if (ptr_stopioe)
            sim_printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else perror ("PTR I/O error");                      /* I/O error */
    clearerr (ptr_unit.fileref);
    return SCPE_IOERR;
    }
ptr_unit.pos = ptr_unit.pos + 1;                        /* inc position */
if (temp) {                                             /* leader/gap? */
    ptr_unit.buf = temp & 0177;                         /* no, save char */
    xfr_req = xfr_req | XFR_PTR;                        /* set xfr flag */
    ptr_sor = 0;                                        /* in record */
    }
else if (!ptr_sor)                                      /* end record? */
    chan_set_flag (ptr_dib.chan, CHF_EOR);              /* ignore leader */
sim_activate (&ptr_unit, ptr_unit.wait);                /* get next char */
return SCPE_OK;
}

/* Fatal error */

void ptr_set_err (void)
{
chan_set_flag (ptr_dib.chan, CHF_EOR | CHF_ERR);        /* eor, error */
chan_disc (ptr_dib.chan);                               /* disconnect */
xfr_req = xfr_req & ~XFR_PTR;                           /* clear xfr */
sim_cancel (&ptr_unit);                                 /* stop */
return;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
chan_disc (ptr_dib.chan);                               /* disconnect */
ptr_sor = 0;                                            /* clear state */
ptr_unit.buf = 0;
xfr_req = xfr_req & ~XFR_PTR;                           /* clr xfr flag */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Boot routine - simulate FILL console command */

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
extern uint32 P, M[];

M[0] = 077777771;                                       /* -7B */
M[1] = 007100000;                                       /* LDX 0 */
M[2] = 000203604;                                       /* EOM 3604B */
M[3] = 003200002;                                       /* WIM 2 */
M[4] = 000100002;                                       /* BRU 2 */
P = 1;                                                  /* start at 1 */
return SCPE_OK;
}

/* Paper tape punch

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result

   The paper tape punch is an asynchronous streaming output device.  That is,
   it can never cause a channel rate error; if no data is available, it waits.
*/

t_stat ptp (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 new_ch;

switch (fnc) {                                          /* case function */

    case IO_CONN:
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != ptp_dib.chan)                     /* inv conn? err */
            return SCPE_IERR;
        ptp_ldr = (inst & CHC_NLDR)? 0: 1;              /* leader? */
        xfr_req = xfr_req & ~XFR_PTP;                   /* clr xfr flag */
        sim_activate (&ptp_unit, ptp_unit.wait);        /* activate */
        break;

    case IO_DISC:                                       /* disconnect */
        ptp_ldr = 0;                                    /* clear state */
        xfr_req = xfr_req & ~XFR_PTP;                   /* clr xfr flag */
        sim_cancel (&ptp_unit);                         /* deactivate unit */
        break;

    case IO_WRITE:                                      /* write */
        xfr_req = xfr_req & ~XFR_PTP;                   /* clr xfr flag */
        sim_activate (&ptp_unit, ptp_unit.wait);        /* activate */
        ptp_unit.buf = odd_par[(*dat) & 077];           /* save data */
        return ptp_out (ptp_unit.buf);                  /* punch w/ par */

    case IO_WREOR:                                      /* write eor */
        break;

    case IO_EOM1:                                       /* EOM mode 1*/
    case IO_READ:                                       /* read */
        CRETINS;                                        /* error */
        }

return SCPE_OK;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int32 i;
t_stat r = SCPE_OK;

if (ptp_ldr) {                                          /* need leader? */
    for (i = 0; i < 12; i++) {                          /* punch leader */
        if ((r = ptp_out (0)))
            break;
        }
    }
ptp_ldr = 0;                                            /* clear flag */
chan_set_ordy (ptp_dib.chan);                           /* ptp ready */
return r;
}

/* Punch I/O */

t_stat ptp_out (int32 dat)
{
if ((ptp_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ptp_set_err ();                                     /* no, disc, err */
    CRETIOE (ptp_stopioe, SCPE_UNATT);
    }
if (putc (dat, ptp_unit.fileref) == EOF) {              /* I/O error? */
    ptp_set_err ();                                     /* yes, disc, err */
    perror ("PTP I/O error");                           /* print msg */
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ptp_unit.pos + 1;                        /* inc position */
return SCPE_OK;
}

/* Fatal error */

void ptp_set_err (void)
{
chan_set_flag (ptp_dib.chan, CHF_ERR);                  /* error */
chan_disc (ptp_dib.chan);                               /* disconnect */
xfr_req = xfr_req & ~XFR_PTP;                           /* clear xfr */
sim_cancel (&ptp_unit);                                 /* stop */
return;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
chan_disc (ptp_dib.chan);                               /* disconnect */
ptp_ldr = 0;                                            /* clear state */
ptp_unit.buf = 0;
xfr_req = xfr_req & ~XFR_PTP;                           /* clr xfr flag */
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Typewriter input

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result

   The typewriter input is an asynchronous input device.  That is, it can
   never cause a channel rate error; if no data is available, it waits.
*/

t_stat tti (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 new_ch;

switch (fnc) {                                          /* case function */

    case IO_CONN:                                       /* connect */
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != tti_dib.chan)                     /* inv conn? err */
            return SCPE_IERR;
        xfr_req = xfr_req & ~XFR_TTI;                   /* clr xfr flag */
        break;

    case IO_DISC:                                       /* disconnect */
        xfr_req = xfr_req & ~XFR_TTI;                   /* clr xfr flag */
        break;

    case IO_READ:                                       /* read */
        xfr_req = xfr_req & ~XFR_TTI;                   /* clr xfr flag */
        *dat = tti_unit.buf;                            /* get buf data */
        break;

    case IO_WREOR:                                      /* write eor */
        break;

    case IO_EOM1:                                       /* EOM mode 1*/
    case IO_WRITE:                                      /* write */
        CRETINS;                                        /* error */
        }

return SCPE_OK;
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);                /* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)              /* no char or error? */
    return temp;
if (temp & SCPE_BREAK)                                  /* ignore break */
    return SCPE_OK;
temp = temp & 0177;
tti_unit.pos = tti_unit.pos + 1;
if (ascii_to_sds[temp] >= 0) {
    tti_unit.buf = ascii_to_sds[temp];                  /* internal rep */
    sim_putchar (temp);                                 /* echo */
    if (temp == '\r')                                   /* lf after cr */
        sim_putchar ('\n');
    xfr_req = xfr_req | XFR_TTI;                        /* set xfr flag */
    }
else sim_putchar (007);                                 /* ding! */
return SCPE_OK;
}

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
chan_disc (tti_dib.chan);                               /* disconnect */
tti_unit.buf = 0;                                       /* clear state */
xfr_req = xfr_req & ~XFR_TTI;                           /* clr xfr flag */
sim_activate (&tti_unit, tti_unit.wait);                /* start poll */
return SCPE_OK;
}

/* Typewriter output

   conn -       inst = EOM0, dat = NULL
   eom1 -       inst = EOM1, dat = NULL
   sks -        inst = SKS, dat = ptr to result
   disc -       inst = device number, dat = NULL
   wreor -      inst = device number, dat = NULL
   read -       inst = device number, dat = ptr to data
   write -      inst = device number, dat = ptr to result

   The typewriter output is an asynchronous streaming output device.  That is,
   it can never cause a channel rate error; if no data is available, it waits.
*/

t_stat tto (uint32 fnc, uint32 inst, uint32 *dat)
{
int32 new_ch;

switch (fnc) {                                          /* case function */

    case IO_CONN:
        new_ch = I_GETEOCH (inst);                      /* get new chan */
        if (new_ch != tto_dib.chan)                     /* inv conn? err */
            return SCPE_IERR;
        xfr_req = xfr_req & ~XFR_TTO;                   /* clr xfr flag */
        sim_activate (&tto_unit, tto_unit.wait);        /* activate */
        break;

    case IO_DISC:                                       /* disconnect */
        xfr_req = xfr_req & ~XFR_TTO;                   /* clr xfr flag */
        sim_cancel (&tto_unit);                         /* deactivate unit */
        break;

    case IO_WRITE:                                      /* write */
        xfr_req = xfr_req & ~XFR_TTO;                   /* clr xfr flag */
        tto_unit.buf = (*dat) & 077;                    /* save data */
        sim_activate (&tto_unit, tto_unit.wait);        /* activate */
        break;

    case IO_WREOR:                                      /* write eor */
        break;
    
    case IO_EOM1:                                       /* EOM mode 1*/
    case IO_READ:                                       /* read */
        CRETINS;                                        /* error */
        }

return SCPE_OK;
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 asc;
t_stat r;

if (uptr->buf == TT_CR)                                 /* control chars? */
    asc = '\r';
else if (uptr->buf == TT_BS)
    asc = '\b';
else if (uptr->buf == TT_TB)
    asc = '\t';
else asc = sds_to_ascii[uptr->buf];                     /* translate */
if ((r = sim_putchar_s (asc)) != SCPE_OK) {             /* output; error? */
    sim_activate (uptr, uptr->wait);                    /* retry */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }
uptr->pos = uptr->pos + 1;                              /* inc position */
chan_set_ordy (tto_dib.chan);                           /* tto rdy */
if (asc == '\r') {                                      /* CR? */
    sim_putchar ('\n');                                 /* add lf */
    uptr->pos = uptr->pos + 1;                          /* inc position */
    }
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
chan_disc (tto_dib.chan);                               /* disconnect */
tto_unit.buf = 0;                                       /* clear state */
xfr_req = xfr_req & ~XFR_TTO;                           /* clr xfr flag */
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}
