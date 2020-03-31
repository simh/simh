/* sigma_coc.c: Sigma character-oriented communications subsystem simulator

   Copyright (c) 2007-2008, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substanXIAl portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   coc          7611 communications multiplexor
*/

#include "sigma_io_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

/* Constants */

#define MUX_LINES       64                              /* max lines */
#define MUX_LINES_DFLT  8                               /* default lines */
#define MUX_INIT_POLL   8000
#define MUXL_WAIT       500
#define MUX_NUMLIN      mux_desc.lines                  /* curr # lines */

#define MUXC            0                               /* channel thread */
#define MUXI            1                               /* input thread */

/* Line status */

#define MUXL_XIA        0x01                            /* xmt intr armed */
#define MUXL_XIR        0x02                            /* xmt intr req */
#define MUXL_REP        0x04                            /* rcv enable pend */
#define MUXL_RBP        0x10                            /* rcv break pend */

/* Channel state */

#define MUXC_IDLE       0                               /* idle */
#define MUXC_INIT       1                               /* init */
#define MUXC_RCV        2                               /* receive */
#define MUXC_END        3                               /* end */

/* DIO address */

#define MUXDIO_V_FNC    0                               /* function */
#define MUXDIO_M_FNC    0xF
#define MUXDIO_V_COC    4                               /* ctlr num */
#define MUXDIO_M_COC    0xF
#define MUXDIO_GETFNC(x) (((x) >> MUXDIO_V_FNC) & MUXDIO_M_FNC)
#define MUXDIO_GETCOC(x) (((x) >> MUXDIO_V_COC) & MUXDIO_M_COC)

#define MUXDAT_V_LIN    0                               /* line num */
#define MUXDAT_M_LIN    (MUX_LINES - 1)
#define MUXDAT_V_CHR    8                               /* output char */
#define MUXDAT_M_CHR    0xFF
#define MUXDAT_GETLIN(x) (((x) >> MUXDAT_V_LIN) & MUXDAT_M_LIN)
#define MUXDAT_GETCHR(x) (((x) >> MUXDAT_V_CHR) & MUXDAT_M_CHR)

uint8 mux_rbuf[MUX_LINES];                              /* rcv buf */
uint8 mux_xbuf[MUX_LINES];                              /* xmt buf */
uint8 mux_sta[MUX_LINES];                               /* status */
uint32 mux_tps = RTC_HZ_50;                             /* polls/second */
uint32 mux_scan = 0;                                    /* scanner */
uint32 mux_slck = 0;                                    /* scanner locked */
uint32 muxc_cmd = MUXC_IDLE;                            /* channel state */
uint32 mux_rint = INTV (INTG_E2, 0);
uint32 mux_xint = INTV (INTG_E2, 1);

TMLN mux_ldsc[MUX_LINES] = { 0 };                       /* line descrs */
TMXR mux_desc = { MUX_LINES_DFLT, 0, 0, mux_ldsc };     /* mux descrr */

extern uint32 chan_ctl_time;
extern uint32 CC;
extern uint32 *R;

uint32 mux_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 mux_dio (uint32 op, uint32 rn, uint32 ad);
uint32 mux_tio_status (void);
t_stat mux_chan_err (uint32 st);
t_stat muxc_svc (UNIT *uptr);
t_stat muxo_svc (UNIT *uptr);
t_stat muxi_rtc_svc (UNIT *uptr);
t_stat mux_reset (DEVICE *dptr);
t_stat mux_attach (UNIT *uptr, CONST char *cptr);
t_stat mux_detach (UNIT *uptr);
t_stat mux_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void mux_reset_ln (int32 ln);
void mux_scan_next (t_bool clr);
t_stat muxi_put_char (uint32 c, uint32 ln);

/* MUX data structures

   mux_dev      MUX device descriptor
   mux_unit     MUX unit descriptor
   mux_reg      MUX register list
   mux_mod      MUX modifiers list
*/

dib_t mux_dib = { DVA_MUX, &mux_disp, DIO_MUX, &mux_dio };

UNIT mux_unit[] = {
    { UDATA (&muxc_svc, UNIT_ATTABLE, 0) },
    { UDATA (&muxi_rtc_svc, UNIT_DIS, 0) }
    };

REG mux_reg[] = {
    { BRDATA (STA, mux_sta, 16, 8, MUX_LINES) },
    { BRDATA (RBUF, mux_rbuf, 16, 8, MUX_LINES) },
    { BRDATA (XBUF, mux_xbuf, 16, 8, MUX_LINES) },
    { DRDATA (SCAN, mux_scan, 6) },
    { FLDATA (SLCK, mux_slck, 0) },
    { DRDATA (CMD, muxc_cmd, 2) },
    { DRDATA (TPS, mux_tps, 8), REG_HRO },
    { NULL }
    };

MTAB mux_mod[] = {
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &mux_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &mux_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat,  (void *) &mux_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat,  (void *) &mux_desc },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &mux_vlines, &tmxr_show_lines, (void *) &mux_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_COC, "POLL", "POLL",
      &rtc_set_tps, &rtc_show_tps, (void *) &mux_tps },
    { 0 }
    };

DEVICE mux_dev = {
    "MUX", mux_unit, mux_reg, mux_mod,
    2, 10, 31, 1, 16, 8,
    &tmxr_ex, &tmxr_dep, &mux_reset,
    NULL, &mux_attach, &mux_detach,
    &mux_dib, DEV_MUX | DEV_DISABLE
    };

/* MUXL data structures

   muxl_dev     MUXL device descriptor
   muxl_unit    MUXL unit descriptor
   muxl_reg     MUXL register list
   muxl_mod     MUXL modifiers list
*/

UNIT muxl_unit[] = {
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC|UNIT_DIS, 0), MUXL_WAIT }
    };

MTAB muxl_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &mux_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &mux_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &mux_desc },
    { 0 }
    };

REG muxl_reg[] = {
    { URDATA (TIME, muxl_unit[0].wait, 10, 24, 0,
              MUX_LINES, REG_NZ + PV_LEFT) },
    { NULL }
    };

DEVICE muxl_dev = {
    "MUXL", muxl_unit, muxl_reg, muxl_mod,
    MUX_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &mux_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* MUX: IO dispatch routine */

uint32 mux_disp (uint32 op, uint32 dva, uint32 *dvst)
{
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = mux_tio_status ();                      /* get status */
        if ((*dvst & DVS_CST) == 0) {                   /* ctrl idle? */
            muxc_cmd = MUXC_INIT;                       /* start dev thread */
            sim_activate (&mux_unit[MUXC], chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = mux_tio_status ();                      /* return status */
        break;

    case OP_TDV:                                        /* test status */
        *dvst = 0;                                      /* no status */
        break;

    case OP_HIO:                                        /* halt I/O */
        *dvst = mux_tio_status ();                      /* get status */
        muxc_cmd = MUXC_IDLE;                           /* stop dev thread */
        sim_cancel (&mux_unit[MUXC]);
        io_sclr_req (mux_rint, 0);                      /* clr rcv int */
        io_sclr_req (mux_xint, 0);
        break;

    case OP_AIO:                                        /* acknowledge int */
        *dvst = 0;                                      /* no status */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* MUX: DIO dispatch routine */

uint32 mux_dio (uint32 op, uint32 rn, uint32 ad)
{
int32 ln;
uint32 fnc = MUXDIO_GETFNC (ad);
uint32 coc = MUXDIO_GETCOC (ad);

if (op == OP_RD) {                                      /* read direct */
    if (coc != 0)                                       /* nx COC? */
        return 0;
    R[rn] = mux_scan | 0x40;                            /* return line num */
    mux_sta[mux_scan] &= ~MUXL_XIR;                     /* clear int req */
    return 0;
    }
ln = MUXDAT_GETLIN (R[rn]);                             /* get line num */
if (fnc & 0x4) {                                        /* transmit */
    if ((coc != 0) ||                                   /* nx COC or */
        (ln >= MUX_NUMLIN)) {                           /* nx line? */
        CC |= CC4;
        return 0;
        }
    if ((fnc & 0x7) == 0x5) {                           /* send char? */
        if (fnc & 0x8)                                  /* space? */
            mux_xbuf[ln] = 0;
        else mux_xbuf[ln] = MUXDAT_GETCHR (R[rn]);      /* no, get char */
        sim_activate (&muxl_unit[ln], muxl_unit[ln].wait);
        mux_sta[ln] = (mux_sta[ln] | MUXL_XIA) & ~MUXL_XIR;
        mux_scan_next (1);                              /* unlock scanner */
        }
    else if (fnc == 0x06) {                             /* stop transmit */
        mux_sta[ln] &= ~MUXL_XIA|MUXL_XIR;              /* disable int */
        mux_scan_next (1);                              /* unlock scanner */
        }
    else if (fnc == 0x07) {                             /* disconnect */
        tmxr_reset_ln (&mux_ldsc[ln]);                  /* reset line */
        mux_reset_ln (ln);                              /* reset state */
        }
    CC = (sim_is_active (&muxl_unit[ln])? 0: CC4) |
        (mux_ldsc[ln].conn? CC3: 0);
    }
else {                                                  /* receive */
    if ((coc != 0) ||                                   /* nx COC or */
        (ln >= MUX_NUMLIN))                             /* nx line */
        return 0;
    if (fnc == 0x01) {                                  /* set rcv enable */
        if (mux_ldsc[ln].conn)                          /* connected? */
            mux_ldsc[ln].rcve = 1;                      /* just enable */
        else mux_sta[ln] |= MUXL_REP;                   /* enable pending */
        }
    else if (fnc == 0x02) {                             /* clr rcv enable */
        mux_ldsc[ln].rcve = 0;
        mux_sta[ln] &= ~MUXL_REP;
        }
    else if (fnc == 0x03) {                             /* disconnect */
        tmxr_reset_ln (&mux_ldsc[ln]);                  /* reset line */
        mux_reset_ln (ln);                              /* reset state */
        }
    if (mux_sta[ln] & MUXL_RBP)                         /* break pending? */
        CC = CC3|CC4;
    else CC = mux_ldsc[ln].rcve? CC4: CC3;
    }
return 0;
}    

/* Unit service - channel overhead */

t_stat muxc_svc (UNIT *uptr)
{
uint32 st;
uint32 cmd;

if (muxc_cmd == MUXC_INIT) {                            /* init state? */
    st = chan_get_cmd (mux_dib.dva, &cmd);              /* get command */
    if (CHS_IFERR (st))                                 /* channel error? */
       mux_chan_err (st);                               /* go idle */
    else muxc_cmd = MUXC_RCV;                           /* no, receive */
    }
else if (muxc_cmd == MUXC_END) {                        /* end state? */
    st = chan_end (mux_dib.dva);                        /* set channel end */
    if (CHS_IFERR (st))                                 /* channel error? */
        mux_chan_err (st);                              /* go idle */
    else if (st == CHS_CCH) {                           /* command chain? */
        muxc_cmd = MUXC_INIT;                           /* restart thread */
        sim_activate (uptr, chan_ctl_time);             /* schedule soon */
        }
    else muxc_cmd = MUXC_IDLE;                          /* else idle */
    }
return SCPE_OK;
}

/* Unit service - polled input - called from rtc scheduler

   Poll for new connections
   Poll all connected lines for input
*/

t_stat muxi_rtc_svc (UNIT *uptr)
{
t_stat r;
int32 newln, ln, c;

if ((mux_unit[MUXC].flags & UNIT_ATT) == 0)             /* attached? */
    return SCPE_OK;
newln = tmxr_poll_conn (&mux_desc);                     /* look for connect */
if ((newln >= 0) && (mux_sta[newln] & MUXL_REP)) {      /* rcv enb pending? */
    mux_ldsc[newln].rcve = 1;                           /* enable rcv */
    mux_sta[newln] &= ~MUXL_REP;                        /* clr pending */
    }
tmxr_poll_rx (&mux_desc);                               /* poll for input */
for (ln = 0; ln < MUX_NUMLIN; ln++) {                   /* loop thru lines */
    if (mux_ldsc[ln].conn) {                            /* connected? */
        if ((c = tmxr_getc_ln (&mux_ldsc[ln]))) {       /* get char */
            if (c & SCPE_BREAK)                         /* break? */
                mux_sta[ln] |= MUXL_RBP;                /* set rcv brk */
            else {                                      /* normal char */
                mux_sta[ln] &= ~MUXL_RBP;               /* clr rcv brk */
                c = sim_tt_inpcvt (c, TT_GET_MODE (muxl_unit[ln].flags));
                mux_rbuf[ln] = c;                       /* save char */
                if ((muxc_cmd == MUXC_RCV) &&           /* chan active? */
                    (r = muxi_put_char (c, ln)))        /* char to chan */
                    return r;
                }                                       /* end else char */
            }                                           /* end if char */
        }                                               /* end if conn */
    else mux_sta[ln] &= ~MUXL_RBP;                      /* disconnected */
    }                                                   /* end for */
return SCPE_OK;
}

/* Put character and line number in memory via channel */

t_stat muxi_put_char (uint32 c, uint32 ln)
{
uint32 st;

st = chan_WrMemB (mux_dib.dva, c);                      /* write char */
if (CHS_IFERR (st))                                     /* channel error? */
    return mux_chan_err (st);
st = chan_WrMemB (mux_dib.dva, ln);                     /* write line */
if (CHS_IFERR (st))                                     /* channel error? */
    return mux_chan_err (st);
if (st == CHS_ZBC) {                                    /* bc == 0? */
    muxc_cmd = MUXC_END;                                /* end state */
    sim_activate (&mux_unit[MUXC], chan_ctl_time);      /* quick schedule */
    }
io_sclr_req (mux_rint, 1);                              /* req ext intr */
return SCPE_OK;
}

/* Channel error */

t_stat mux_chan_err (uint32 st)
{
chan_uen (mux_dib.dva);                                 /* uend */
muxc_cmd = MUXC_IDLE;                                   /* go idle */
if (st < CHS_ERR)
    return st;
return 0;
}

/* Unit service - transmit side */

t_stat muxo_svc (UNIT *uptr)
{
int32 c;
uint32 ln = uptr - muxl_unit;                           /* line # */

if (mux_ldsc[ln].conn) {                                /* connected? */
    if (mux_ldsc[ln].xmte) {                            /* xmt enabled? */
        c = sim_tt_outcvt (mux_xbuf[ln], TT_GET_MODE (muxl_unit[ln].flags));
        if (c >= 0)
            tmxr_putc_ln (&mux_ldsc[ln], c);            /* output char */
        tmxr_poll_tx (&mux_desc);                       /* poll xmt */
        if (mux_sta[ln] & MUXL_XIA) {                   /* armed? */
            mux_sta[ln] |= MUXL_XIR;                    /* req intr */
            mux_scan_next (0);                          /* kick scanner */
            }
        }
    else {                                              /* buf full */
        tmxr_poll_tx (&mux_desc);                       /* poll xmt */
        sim_activate (uptr, muxl_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }
return SCPE_OK;
}

/* MUX status routine */

uint32 mux_tio_status (void)
{
if (muxc_cmd == MUXC_IDLE)                              /* idle? */
    return DVS_AUTO;
else return (DVS_AUTO|DVS_CBUSY|DVS_DBUSY|(CC2 << DVT_V_CC));
}

/* Kick scanner */

void mux_scan_next (t_bool clr)
{
int32 i;

if (clr)                                                /* unlock? */
    mux_slck = 0;
else if (mux_slck)                                      /* locked? */
    return;
for (i = 0; i < MUX_NUMLIN; i++) {                      /* scan lines */
    mux_scan = mux_scan + 1;                            /* next line */
    if (mux_scan >= (uint32) MUX_NUMLIN)
        mux_scan = 0;
    if (mux_sta[mux_scan] & MUXL_XIR) {                 /* flag set? */
        mux_slck = 1;                                   /* lock scanner */
        io_sclr_req (mux_xint, 1);                      /* req ext int */
        return;
        }
    }
return;
}

/* Reset routine */

t_stat mux_reset (DEVICE *dptr)
{
int32 i;

if (mux_dev.flags & DEV_DIS)                            /* master disabled? */
    muxl_dev.flags = muxl_dev.flags | DEV_DIS;          /* disable lines */
else muxl_dev.flags = muxl_dev.flags & ~DEV_DIS;
if (mux_unit[MUXC].flags & UNIT_ATT)                    /* master att? */
    rtc_register (RTC_COC, mux_tps, &mux_unit[MUXI]);   /* register timer */
else rtc_register (RTC_COC, RTC_HZ_OFF, NULL);          /* else dereg */
for (i = 0; i < MUX_LINES; i++)                         /* reset lines */
    mux_reset_ln (i);
return SCPE_OK;
}

/* Attach master unit */

t_stat mux_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = tmxr_attach (&mux_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
rtc_register (RTC_COC, mux_tps, &mux_unit[MUXC]);       /* register timer */
return SCPE_OK;
}

/* Detach master unit */

t_stat mux_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&mux_desc, uptr);                      /* detach */
for (i = 0; i < MUX_LINES; i++)                         /* disable rcv */
    mux_reset_ln (i);
rtc_register (RTC_COC, RTC_HZ_OFF, NULL);               /* dereg */
return r;
}


/* Change number of lines */

t_stat mux_vlines (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, MUX_LINES, &r);
if ((r != SCPE_OK) || (newln == MUX_NUMLIN))
    return r;
if (newln == 0) return SCPE_ARG;
if (newln < MUX_NUMLIN) {
    for (i = newln, t = 0; i < MUX_NUMLIN; i++) t = t | mux_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
    for (i = newln; i < MUX_NUMLIN; i++) {
        if (mux_ldsc[i].conn) {
            tmxr_linemsg (&mux_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&mux_ldsc[i]);               /* reset line */
            }
        muxl_unit[i].flags = muxl_unit[i].flags | UNIT_DIS;
        mux_reset_ln (i);
        }
    }
else {
    for (i = MUX_NUMLIN; i < newln; i++) {
        muxl_unit[i].flags = muxl_unit[i].flags & ~UNIT_DIS;
        mux_reset_ln (i);
        }
    }
MUX_NUMLIN = newln;
return SCPE_OK;
}

/* Reset an individual line */

void mux_reset_ln (int32 ln)
{
sim_cancel (&muxl_unit[ln]);
mux_sta[ln] = 0;
mux_rbuf[ln] = 0;
mux_xbuf[ln] = 0;
mux_ldsc[ln].rcve = 0;
return;
}
