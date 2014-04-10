/* pdp18b_stddev.c: 18b PDP's standard devices

   Copyright (c) 1993-2012, Robert M Supnik

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
   clk          clock

   18-Apr-12    RMS     Added clk_cosched routine
                        Revised clk and tti scheduling
   18-Jun-07    RMS     Added UNIT_IDLE to console input, clock
   18-Oct-06    RMS     Added PDP-15 programmable duplex control
                        Fixed handling of non-printable characters in KSR mode
                        Changed clock to be free-running
                        Fixed out-of-tape behavior for PDP-9 vs PDP-15
                        Synced keyboard to clock
   30-Jun-06    RMS     Fixed KSR-28 shift tracking
   20-Jun-06    RMS     Added KSR ASCII reader support
   13-Jun-06    RMS     Fixed Baudot letters/figures inversion for PDP-4
                        Fixed PDP-4/PDP-7 default terminal to be local echo
   22-Nov-05    RMS     Revised for new terminal processing routines
   28-May-04    RMS     Removed SET TTI CTRL-C
   16-Feb-04    RMS     Fixed bug in hardware read-in mode bootstrap
   14-Jan-04    RMS     Revised IO device call interface
                        CAF does not turn off the clock
   29-Dec-03    RMS     Added console backpressure support
   26-Jul-03    RMS     Increased PTP, TTO timeouts for PDP-15 operating systems
                        Added hardware read-in mode support for PDP-7/9/15
   25-Apr-03    RMS     Revised for extended file support
   14-Mar-03    RMS     Clean up flags on detach
   01-Mar-03    RMS     Added SET/SHOW CLK freq, SET TTI CTRL-C
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Added 7B/8B support to terminal
   05-Oct-02    RMS     Added DIBs, device number support, IORS call
   14-Jul-02    RMS     Added ASCII reader/punch support (Hans Pufal)
   30-May-02    RMS     Widened POS to 32b
   29-Nov-01    RMS     Added read only unit support
   25-Nov-01    RMS     Revised interrupt structure
   17-Sep-01    RMS     Removed multiconsole support
   07-Sep-01    RMS     Added terminal multiplexor support
   17-Jul-01    RMS     Moved function prototype
   10-Jun-01    RMS     Cleaned up IOT decoding to reflect hardware
   27-May-01    RMS     Added multiconsole support
   10-Mar-01    RMS     Added funny format loader support
   05-Mar-01    RMS     Added clock calibration support
   22-Dec-00    RMS     Added PDP-9/15 half duplex support
   30-Nov-00    RMS     Fixed PDP-4/7 bootstrap loader for 4K systems
   30-Oct-00    RMS     Standardized register naming
   06-Jan-97    RMS     Fixed PDP-4 console input
   16-Dec-96    RMS     Fixed bug in binary ptr service
*/

#include "pdp18b_defs.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define UNIT_V_RASCII   (UNIT_V_UF + 0)                 /* reader ASCII */
#define UNIT_RASCII     (1 << UNIT_V_RASCII)
#define UNIT_V_KASCII   (UNIT_V_UF + 1)                 /* KSR ASCII */
#define UNIT_KASCII     (1 << UNIT_V_KASCII)
#define UNIT_V_PASCII   (UNIT_V_UF + 0)                 /* punch ASCII */
#define UNIT_PASCII     (1 << UNIT_V_PASCII)

extern int32 M[];
extern int32 int_hwre[API_HLVL+1], PC, ASW;
extern UNIT cpu_unit;

int32 clk_state = 0;
int32 ptr_err = 0, ptr_stopioe = 0, ptr_state = 0;
int32 ptp_err = 0, ptp_stopioe = 0;
int32 tti_2nd = 0;                                      /* 2nd char waiting */
int32 tty_shift = 0;                                    /* KSR28 shift state */
int32 tti_fdpx = 0;                                     /* prog mode full duplex */
int32 clk_tps = 60;                                     /* ticks/second */
int32 tmxr_poll = 16000;                                /* term mux poll */
uint32 clk_task_last = 0;
uint32 clk_task_timer = 0;

const int32 asc_to_baud[128] = {
    000,000,000,000,000,000,000,064,                    /* bell */
    000,000,0110,000,000,0102,000,000,                  /* lf, cr */
    000,000,000,000,000,000,000,000,
    000,000,000,000,000,000,000,000,
    0104,066,061,045,062,000,053,072,                   /* space - ' */
    076,051,000,000,046,070,047,067,                    /* ( - / */
    055,075,071,060,052,041,065,074,                    /* 0 - 7 */
    054,043,056,057,000,000,000,063,                    /* 8 - ? */
    000,030,023,016,022,020,026,013,                    /* @ - G */
    005,014,032,036,011,007,006,003,                    /* H - O */
    015,035,012,024,001,034,017,031,                    /* P - W */
    027,025,021,000,000,000,000,000,                    /* X - _ */
    000,030,023,016,022,020,026,013,                    /* ` - g */
    005,014,032,036,011,007,006,003,                    /* h - o */
    015,035,012,024,001,034,017,031,                    /* p - w */
    027,025,021,000,000,000,000,000                     /* x - DEL */
    };

const char baud_to_asc[64] = {
     0 ,'T',015,'O',' ','H','N','M',
    012,'L','R','G','I','P','C','V',
    'E','Z','D','B','S','Y','F','X',
    'A','W','J', 0 ,'U','Q','K', 0,
     0 ,'5','\r','9',' ','#',',','.',
    012,')','4','&','8','0',':',';',
    '3','"','$','?','\a','6','!','/',
    '-','2','\'',0 ,'7','1','(', 0
    };

int32 ptr (int32 dev, int32 pulse, int32 dat);
int32 ptp (int32 dev, int32 pulse, int32 dat);
int32 tti (int32 dev, int32 pulse, int32 dat);
int32 tto (int32 dev, int32 pulse, int32 dat);
int32 clk_iors (void);
int32 ptr_iors (void);
int32 ptp_iors (void);
int32 tti_iors (void);
int32 tto_iors (void);
t_stat clk_svc (UNIT *uptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, char *cptr);
t_stat ptp_attach (UNIT *uptr, char *cptr);
t_stat ptr_detach (UNIT *uptr);
t_stat ptp_detach (UNIT *uptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat clk_set_freq (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, void *desc);
int32 clk_task_upd (t_bool clr);

extern int32 upd_iors (void);

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit
   clk_reg      CLK register list
*/

DIB clk_dib = { 0, 0, &clk_iors, { NULL } };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), 16000 };

REG clk_reg[] = {
    { FLDATA (INT, int_hwre[API_CLK], INT_V_CLK) },
    { FLDATA (DONE, int_hwre[API_CLK], INT_V_CLK) },
    { FLDATA (ENABLE, clk_state, 0) },
#if defined (PDP15)
    { ORDATA (TASKTIMER, clk_task_timer, 18) },
    { DRDATA (TASKLAST, clk_task_last, 32), REG_HRO },
#endif
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 8), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &clk_show_freq, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_devno },
    { 0 }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, 0
    };

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit
   ptr_reg      PTR register list
*/

DIB ptr_dib = { DEV_PTR, 1, &ptr_iors, { &ptr } };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 18) },
    { FLDATA (INT, int_hwre[API_PTR], INT_V_PTR) },
    { FLDATA (DONE, int_hwre[API_PTR], INT_V_PTR) },
#if defined (IOS_PTRERR)
    { FLDATA (ERR, ptr_err, 0) },
#endif
    { ORDATA (STATE, ptr_state, 5), REG_HRO },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB ptr_mod[] = {
    { UNIT_RASCII, UNIT_RASCII, "even parity ASCII", NULL },
    { UNIT_KASCII, UNIT_KASCII, "forced parity ASCII", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_devno },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, &ptr_attach, &ptr_detach,
    &ptr_dib, 0
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit
   ptp_reg      PTP register list
*/

DIB ptp_dib = { DEV_PTP, 1, &ptp_iors, { &ptp } };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { FLDATA (INT, int_hwre[API_PTP], INT_V_PTP) },
    { FLDATA (DONE, int_hwre[API_PTP], INT_V_PTP) },
#if defined (IOS_PTPERR)
    { FLDATA (ERR, ptp_err, 0) },
#endif
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] = {
    { UNIT_PASCII, UNIT_PASCII, "7b ASCII", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_devno },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, &ptp_attach, &ptp_detach,
    &ptp_dib, 0
    };

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit
   tti_reg      TTI register list
*/

#if defined (KSR28)
#define TTI_WIDTH       5
#define TTI_FIGURES     (1 << TTI_WIDTH)
#define TTI_BOTH        (1 << (TTI_WIDTH + 1))
#define BAUDOT_LETTERS  037
#define BAUDOT_FIGURES  033

#else

#define TTI_WIDTH       8
#endif

#define TTI_MASK        ((1 << TTI_WIDTH) - 1)
#define TTUF_V_HDX      (TTUF_V_UF + 0)                 /* half duplex */
#define TTUF_HDX        (1 << TTUF_V_HDX)

DIB tti_dib = { DEV_TTI, 1, &tti_iors, { &tti } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE+TT_MODE_KSR+TTUF_HDX, 0), 0 };

REG tti_reg[] = {
    { ORDATA (BUF, tti_unit.buf, TTI_WIDTH) },
#if defined (KSR28)
    { ORDATA (BUF2ND, tti_2nd, TTI_WIDTH), REG_HRO },
#endif
    { FLDATA (INT, int_hwre[API_TTI], INT_V_TTI) },
    { FLDATA (DONE, int_hwre[API_TTI], INT_V_TTI) },
#if defined (PDP15)
    { FLDATA (FDPX, tti_fdpx, 0) },
#endif
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
#if !defined (KSR28)
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7b",  NULL,  NULL },
#endif
    { TTUF_HDX, 0       , "full duplex", "FDX", NULL },
    { TTUF_HDX, TTUF_HDX, "half duplex", "HDX", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_devno, NULL },
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

#if defined (KSR28)
#define TTO_WIDTH       5
#define TTO_FIGURES     (1 << TTO_WIDTH)

#else

#define TTO_WIDTH       8
#endif

#define TTO_MASK        ((1 << TTO_WIDTH) - 1)

DIB tto_dib = { DEV_TTO, 1, &tto_iors, { &tto } };

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_KSR, 0), 1000 };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, TTO_WIDTH) },
#if defined (KSR28)
    { FLDATA (SHIFT, tty_shift, 0), REG_HRO },
#endif
    { FLDATA (INT, int_hwre[API_TTO], INT_V_TTO) },
    { FLDATA (DONE, int_hwre[API_TTO], INT_V_TTO) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
#if !defined (KSR28)
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &tty_set_mode },
#endif
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_devno },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, 0
    };

/* Clock: IOT routine */

int32 clk (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 001) {                                      /* CLSF */
    if (TST_INT (CLK))
        dat = dat | IOT_SKP;
    }
if (pulse & 004) {                                      /* CLON/CLOF */
    CLR_INT (CLK);                                      /* clear flag */
    if (pulse & 040)                                    /* CLON */
        clk_state = 1;
    else clk_state = 0;                                 /* CLOF */
    }
return dat;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{
int32 t;

t = sim_rtc_calb (clk_tps);                             /* calibrate clock */
tmxr_poll = t;                                          /* set mux poll */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
#if defined (PDP15)
clk_task_upd (FALSE);                                   /* update task timer */
#endif
if (clk_state) {                                        /* clock on? */
    M[7] = (M[7] + 1) & DMASK;                          /* incr counter */
    if (M[7] == 0)                                      /* ovrflo? set flag */
        SET_INT (CLK);
    }
return SCPE_OK;
}

#if defined (PDP15)

/* Task timer update (PDP-15 XVM only)

   The task timer increments monotonically at 100Khz. Since this can't be
   simulated accurately, updates are done by interpolation since the last
   reading.  The timer is also updated at clock events to keep the cycle
   counters from wrapping around more than once between updates. */

int32 clk_task_upd (t_bool clr)
{
uint32 delta, val, iusec10;
uint32 cur = sim_grtime ();
double usec10;

if (cur > clk_task_last)
    delta = cur - clk_task_last;
else delta = clk_task_last - cur;
usec10 = ((((double) delta) * 100000.0) /
    (((double) tmxr_poll) * ((double) clk_tps)));
iusec10 = (int32) usec10;
val = (clk_task_timer + iusec10) & DMASK;
if (clr)
    clk_task_timer = 0;
else clk_task_timer = val;
clk_task_last = cur;
return ((int32) val);
}

#endif

/* IORS service */

int32 clk_iors (void)
{
return (TST_INT (CLK)? IOS_CLK: 0);
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
int32 t;

sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
CLR_INT (CLK);                                          /* clear flag */
if (!sim_is_running) {                                  /* RESET (not CAF)? */
    t = sim_rtc_init (clk_unit.wait);                   /* init calibration */
    tmxr_poll = t;                                      /* set mux poll */
    sim_activate_abs (&clk_unit, t);                    /* activate unit */
    clk_state = 0;                                      /* clock off */
    clk_task_timer = 0;
    clk_task_last = 0;
    }
return SCPE_OK;
}

/* Set frequency */

t_stat clk_set_freq (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 50) && (val != 60))
    return SCPE_IERR;
clk_tps = val;
return SCPE_OK;
}

/* Show frequency */

t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, (clk_tps == 50)? "50Hz": "60Hz");
return SCPE_OK;
}

/* Paper tape reader out-of-tape handling

   The PDP-4 and PDP-7 readers behaved like most early DEC readers; when
   they ran out of tape, they hung.  It was up to the program to sense this
   condition by running a timer.

   The PDP-9 reader controller synthesized the out of tape condition by
   noticing whether there was a transition on the feed hole within a window.
   The out-of-tape flag was treated like the reader flag in most cases.

   The PDP-15 reader controller received the out-of-tape flag as a static
   condition from the reader itself and simply reported it via IORS. */

/* Paper tape reader: IOT routine */

int32 ptr (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 001) {                                      /* RSF */
    if (TST_INT (PTR))
        dat = dat | IOT_SKP;
    }
if (pulse & 002) {                                      /* RRB, RCF */
    CLR_INT (PTR);                                      /* clear flag */
    dat = dat | ptr_unit.buf;                           /* return buffer */
    }
if (pulse & 004) {                                      /* RSA, RSB */
    ptr_state = (pulse & 040)? 18: 0;                   /* set mode */
    CLR_INT (PTR);                                      /* clear flag */
#if !defined (PDP15)                                    /* except on PDP15 */
    ptr_err = 0;                                        /* clear error */
#endif
    ptr_unit.buf = 0;                                   /* clear buffer */
    sim_activate (&ptr_unit, ptr_unit.wait);
    }
return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
#if defined (IOS_PTRERR)
    SET_INT (PTR);                                      /* if err, set flag */
    ptr_err = 1;                                        /* set error */
#endif
    return IORETURN (ptr_stopioe, SCPE_UNATT);
    }
if ((temp = getc (ptr_unit.fileref)) == EOF) {          /* end of file? */
#if defined (IOS_PTRERR)
    SET_INT (PTR);                                      /* if err, set flag */
    ptr_err = 1;                                        /* set error */
#endif
    if (feof (ptr_unit.fileref)) {
        if (ptr_stopioe)
            sim_printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else perror ("PTR I/O error");
    clearerr (ptr_unit.fileref);
    return SCPE_IOERR;
    }
if (ptr_state == 0) {                                   /* ASCII */
    if (ptr_unit.flags & UNIT_RASCII) {                 /* want parity? */
        ptr_unit.buf = temp = temp & 0177;              /* parity off */
        while ((temp = temp & (temp - 1)))
            ptr_unit.buf = ptr_unit.buf ^ 0200;         /* count bits */
        ptr_unit.buf = ptr_unit.buf ^ 0200;             /* set even parity */
        }
    else if (ptr_unit.flags & UNIT_KASCII)              /* KSR ASCII? */
        ptr_unit.buf = (temp | 0200) & 0377;            /* forced parity */
    else ptr_unit.buf = temp & 0377;
    }
else if (temp & 0200) {                                 /* binary */
    ptr_state = ptr_state - 6;
    ptr_unit.buf = ptr_unit.buf | ((temp & 077) << ptr_state);
    }
if (ptr_state == 0)                                     /* if done, set flag */
    SET_INT (PTR);
else sim_activate (&ptr_unit, ptr_unit.wait);           /* else restart */
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_state = 0;                                          /* clear state */
ptr_unit.buf = 0;
CLR_INT (PTR);                                          /* clear flag */
#if defined (PDP15)                                     /* PDP15, static err */
if (((ptr_unit.flags & UNIT_ATT) == 0) || feof (ptr_unit.fileref))
    ptr_err = 1;
else
#endif
ptr_err = 0;                                            /* all other, clr err */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* IORS service */

int32 ptr_iors (void)
{
return ((TST_INT (PTR)? IOS_PTR: 0)
#if defined (IOS_PTRERR)
    | (ptr_err? IOS_PTRERR: 0)
#endif
    );
}

/* Attach routine */

t_stat ptr_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if (reason != SCPE_OK)
    return reason;
ptr_err = 0;                                             /* attach clrs error */
ptr_unit.flags = ptr_unit.flags & ~(UNIT_RASCII|UNIT_KASCII);
if (sim_switches & SWMASK ('A'))
    ptr_unit.flags = ptr_unit.flags | UNIT_RASCII;
if (sim_switches & SWMASK ('K'))
    ptr_unit.flags = ptr_unit.flags | UNIT_KASCII;
return SCPE_OK;
}

/* Detach routine */

t_stat ptr_detach (UNIT *uptr)
{
#if defined (PDP15)
ptr_err = 1;
#endif
ptr_unit.flags = ptr_unit.flags & ~UNIT_RASCII;
return detach_unit (uptr);
}

/* Hardware RIM loader routines, PDP-7/9/15 */

int32 ptr_getw (UNIT *uptr, int32 *hi)
{
int32 word, bits, st, ch;

word = st = bits = 0;
do {
    if ((ch = getc (uptr->fileref)) == EOF)
        return -1;
    uptr->pos = uptr->pos + 1;
    if (ch & 0200) {
        word = (word << 6) | (ch & 077);
        bits = (bits << 1) | ((ch >> 6) & 1);
        st++;
        }
    } while (st < 3);
if (hi != NULL)
    *hi = bits;
return word;
}

t_stat ptr_rim_load (UNIT *uptr, int32 origin)
{
int32 bits, val;

for (;;) {                                              /* word loop */
    if ((val = ptr_getw (uptr, &bits)) < 0)
        return SCPE_FMT;
    if (bits & 1) {                                     /* end of tape? */
        if ((val & 0760000) == OP_JMP) {
            PC = ((origin - 1) & 060000) | (val & 017777);
            return SCPE_OK;
            }
        else if (val == OP_HLT)
            return STOP_HALT;
        break;
        }
    else if (MEM_ADDR_OK (origin))
        M[origin++] = val;
    }
return SCPE_FMT;
}

#if defined (PDP4) || defined (PDP7)

/* Bootstrap routine, PDP-4 and PDP-7

   In a 4K system, the boostrap resides at 7762-7776.
   In an 8K or greater system, the bootstrap resides at 17762-17776.
   Because the program is so small, simple masking can be
   used to remove addr<5> for a 4K system. */

#define BOOT_START      017577
#define BOOT_FPC        017577                          /* funny format loader */
#define BOOT_RPC        017770                          /* RIM loader */
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
    0700144,                                            /* rsb */
    0117762,                                            /* ff,  jsb r1b */
    0057666,                                            /*      dac done 1 */
    0117762,                                            /*      jms r1b */
    0057667,                                            /*      dac done 2 */
    0117762,                                            /*      jms r1b */
    0040007,                                            /*      dac conend */
    0057731,                                            /*      dac conbeg */
    0440007,                                            /*      isz conend */
    0117762,                                            /* blk, jms r1b */
    0057673,                                            /*      dac cai */
    0741100,                                            /*      spa */
    0617665,                                            /*      jmp done */
    0117762,                                            /*      jms r1b */
    0057777,                                            /*      dac tem1 */
    0317673,                                            /*      add cai */
    0057775,                                            /*      dac cks */
    0117713,                                            /*      jms r1a */
    0140010,                                            /*      dzm word */
    0457777,                                            /* cont, isz tem1 */
    0617632,                                            /*      jmp cont1 */
    0217775,                                            /*      lac cks */
    0740001,                                            /*      cma */
    0740200,                                            /*      sza */
    0740040,                                            /*      hlt */
    0700144,                                            /*      rsb */
    0617610,                                            /*      jmp blk */
    0117713,                                            /* cont1, jms r1a */
    0057762,                                            /*      dac tem2 */
    0117713,                                            /*      jms r1a */
    0742010,                                            /*      rtl */
    0742010,                                            /*      rtl */
    0742010,                                            /*      rtl */
    0742010,                                            /*      rtl */
    0317762,                                            /*      add tem2 */
    0057762,                                            /*      dac tem2 */
    0117713,                                            /*      jms r1a */
    0742020,                                            /*      rtr */
    0317726,                                            /*      add cdsp */
    0057713,                                            /*      dac r1a */
    0517701,                                            /*      and ccma */
    0740020,                                            /*      rar */
    0317762,                                            /*      add tem2 */
    0437713,                                            /*      xct i r1a */
    0617622,                                            /*      jmp cont */
    0617672,                                            /* dsptch, jmp code0 */
    0617670,                                            /*      jmp code1 */
    0617700,                                            /*      jmp code2 */
    0617706,                                            /*      jmp code3 */
    0417711,                                            /*      xct code4 */
    0617732,                                            /*      jmp const */
    0740000,                                            /*      nop */
    0740000,                                            /*      nop */
    0740000,                                            /*      nop */
    0200007,                                            /* done, lac conend */
    0740040,                                            /*      xx */
    0740040,                                            /*      xx */
    0517727,                                            /* code1, and imsk */
    0337762,                                            /*      add i tem2 */
    0300010,                                            /* code0, add word */
    0740040,                                            /* cai, xx */
    0750001,                                            /*      clc */
    0357673,                                            /*      tad cai */
    0057673,                                            /*      dac cai */
    0617621,                                            /*      jmp cont-1 */
    0711101,                                            /* code2, spa cla */
    0740001,                                            /* ccma, cma */
    0277762,                                            /*      xor i tem2 */
    0300010,                                            /*      add word */
    0040010,                                            /* code2a, dac word */
    0617622,                                            /* jmp cont */
    0057711,                                            /* code3, dac code4 */
    0217673,                                            /*      lac cai */
    0357701,                                            /*      tad ccma */
    0740040,                                            /* code4, xx */
    0617622,                                            /*      jmp cont */
    0000000,                                            /* r1a, 0 */
    0700101,                                            /*      rsf */
    0617714,                                            /*      jmp .-1 */
    0700112,                                            /*      rrb */
    0700104,                                            /*      rsa */
    0057730,                                            /*      dac tem */
    0317775,                                            /*      add cks */
    0057775,                                            /*      dac cks */
    0217730,                                            /*      lac tem */
    0744000,                                            /*      cll */
    0637713,                                            /*      jmp i r1a */
    0017654,                                            /* cdsp, dsptch */
    0760000,                                            /* imsk, 760000 */
    0000000,                                            /* tem, 0 */
    0000000,                                            /* conbeg, 0 */
    0300010,                                            /* const, add word */
    0060007,                                            /*      dac i conend */
    0217731,                                            /*      lac conbeg */
    0040010,                                            /*      dac index */
    0220007,                                            /*      lac i conend */
    0560010,                                            /* con1, sad i index */
    0617752,                                            /*      jmp find */
    0560010,                                            /*      sad i index */
    0617752,                                            /*      jmp find */
    0560010,                                            /*      sad i index */
    0617752,                                            /*      jmp find */
    0560010,                                            /*      sad i index */
    0617752,                                            /*      jmp find */
    0560010,                                            /*      sad i index */
    0617752,                                            /*      jmp find */
    0617737,                                            /*      jmp con1 */
    0200010,                                            /* find, lac index */
    0540007,                                            /*      sad conend */
    0440007,                                            /*      isz conend */
    0617704,                                            /*      jmp code2a */
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,                                            /* r1b, 0 */
    0700101,                                            /*      rsf */
    0617763,                                            /*      jmp .-1 */
    0700112,                                            /*      rrb */
    0700144,                                            /*      rsb */
    0637762,                                            /*      jmp i r1b */
    0700144,                                            /* go,  rsb */
    0117762,                                            /* g,   jms r1b */
    0057775,                                            /*      dac cks */
    0417775,                                            /*      xct cks */
    0117762,                                            /*      jms r1b */
    0000000,                                            /* cks, 0 */
    0617771                                             /*      jmp g */
    };

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
int32 mask, wd;

#if defined (PDP7)
if (sim_switches & SWMASK ('H'))                        /* hardware RIM load? */
    return ptr_rim_load (&ptr_unit, ASW);
#endif
if (ptr_dib.dev != DEV_PTR)                             /* non-std addr? */
    return STOP_NONSTD;
if (MEMSIZE < 8192)                                     /* 4k? */
    mask = 0767777;
else mask = 0777777;
for (i = 0; i < BOOT_LEN; i++) {
    wd = boot_rom[i];
    if ((wd >= 0040000) && (wd < 0640000))
        wd = wd & mask;
    M[(BOOT_START & mask) + i] = wd;
    }
PC = ((sim_switches & SWMASK ('F'))? BOOT_FPC: BOOT_RPC) & mask;
return SCPE_OK;
}

#else

/* PDP-9 and PDP-15 have built-in hardware RIM loaders */

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
return ptr_rim_load (&ptr_unit, ASW);
}

#endif

/* Paper tape punch: IOT routine */

int32 ptp (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 001) {                                      /* PSF */
    if (TST_INT (PTP))
        dat = dat | IOT_SKP;
    }
if (pulse & 002)                                        /* PCF */
    CLR_INT (PTP);
if (pulse & 004) {                                      /* PSA, PSB, PLS */
    CLR_INT (PTP);                                      /* clear flag */
    ptp_unit.buf = (pulse & 040)?                       /* load punch buf */
        (dat & 077) | 0200: dat & 0377;                 /* bin or alpha */
    sim_activate (&ptp_unit, ptp_unit.wait);            /* activate unit */
    }
return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
SET_INT (PTP);                                          /* set flag */
if ((ptp_unit.flags & UNIT_ATT) == 0) {                 /* not attached? */
    ptp_err = 1;                                        /* set error */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
    }
if (ptp_unit.flags & UNIT_PASCII) {                     /* ASCII mode? */
    ptp_unit.buf = ptp_unit.buf & 0177;                 /* force 7b */
    if ((ptp_unit.buf == 0) || (ptp_unit.buf == 0177))
        return SCPE_OK;                                 /* skip null, del */
    }
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {     /* I/O error? */
    ptp_err = 1;                                        /* set error */
    perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* IORS service */

int32 ptp_iors (void)
{
return  ((TST_INT (PTP)? IOS_PTP: 0)
#if defined (IOS_PTPERR)
    | (ptp_err? IOS_PTPERR: 0)
#endif
    );
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
CLR_INT (PTP);                                          /* clear flag */
ptp_err = (ptp_unit.flags & UNIT_ATT)? 0: 1;
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat ptp_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if (reason != SCPE_OK)
    return reason;
ptp_err = 0;
ptp_unit.flags = ptp_unit.flags & ~UNIT_PASCII;
if (sim_switches & SWMASK ('A'))
    ptp_unit.flags = ptp_unit.flags | UNIT_PASCII;
return reason;
}

/* Detach routine */

t_stat ptp_detach (UNIT *uptr)
{
ptp_err = 1;
ptp_unit.flags = ptp_unit.flags & ~UNIT_PASCII;
return detach_unit (uptr);
}

/* Terminal input: IOT routine */

int32 tti (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 001) {                                      /* KSF */
    if (TST_INT (TTI))
        dat = dat | IOT_SKP;
    }
if (pulse & 002) {                                      /* KRS/KRB */
    CLR_INT (TTI);                                      /* clear flag */
    dat = dat | (tti_unit.buf & TTI_MASK);              /* return buffer */
#if defined (PDP15)
    if (pulse & 020)                                    /* KRS? */
        tti_fdpx = 1;
    else tti_fdpx = 0;                                  /* no, KRB */
#endif
    }
if (pulse & 004) {                                      /* IORS */
    dat = dat | upd_iors ();
    }
return dat;
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
#if defined (KSR28)                                     /* Baudot... */
int32 in, c, out;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
if (tti_2nd) {                                          /* char waiting? */
    uptr->buf = tti_2nd;                                /* return char */
    tti_2nd = 0;                                        /* not waiting */
    }
else {
    if ((in = sim_poll_kbd ()) < SCPE_KFLAG)
        return in;
    c = asc_to_baud[in & 0177];                         /* translate char */
    if (c == 0)                                         /* untranslatable? */
        return SCPE_OK;
    if ((c & TTI_BOTH) ||                               /* case insensitive? */
        (((c & TTI_FIGURES)? 1: 0) == tty_shift))       /* right case? */
        uptr->buf = c & TTI_MASK;
    else {                                              /* send case change */
        if (c & TTI_FIGURES) {                          /* to figures? */
            uptr->buf = BAUDOT_FIGURES;
            tty_shift = 1;
            }
        else {                                          /* no, to letters */
            uptr->buf = BAUDOT_LETTERS;
            tty_shift = 0;
            }
        tti_2nd = c & TTI_MASK;                         /* save actual char */
        }
    if ((uptr->flags & TTUF_HDX) &&                     /* half duplex? */
        ((out = sim_tt_outcvt (in, TT_GET_MODE (uptr->flags) | TTUF_KSR)) >= 0)) {
        sim_putchar (out);
        tto_unit.pos = tto_unit.pos + 1;
        }
    }

#else                                                   /* ASCII... */
int32 c, out;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
out = c & 0177;                                         /* mask echo to 7b */
if (c & SCPE_BREAK)                                     /* break? */
    c = 0;
else c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags) | TTUF_KSR);
if ((uptr->flags & TTUF_HDX) && !tti_fdpx && out &&     /* half duplex and */
    ((out = sim_tt_outcvt (out, TT_GET_MODE (uptr->flags) | TTUF_KSR)) >= 0)) {
    sim_putchar (out);                                  /* echo */
    tto_unit.pos = tto_unit.pos + 1;
    }
uptr->buf = c;                                          /* got char */

#endif
uptr->pos = uptr->pos + 1;
SET_INT (TTI);                                          /* set flag */
return SCPE_OK;
}

/* IORS service */

int32 tti_iors (void)
{
return (TST_INT (TTI)? IOS_TTI: 0);
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
CLR_INT (TTI);                                          /* clear flag */
if (!sim_is_running) {                                  /* RESET (not CAF)? */
    tti_unit.buf = 0;                                   /* clear buffer */
    tti_2nd = 0;
    tty_shift = 0;                                      /* clear state */
    tti_fdpx = 0;                                       /* clear dpx mode */
    }
sim_activate (&tti_unit, KBD_WAIT (tti_unit.wait, tmxr_poll));
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 dev, int32 pulse, int32 dat)
{
if (pulse & 001) {                                      /* TSF */
    if (TST_INT (TTO))
        dat = dat | IOT_SKP;
    }
if (pulse & 002)                                        /* clear flag */
    CLR_INT (TTO);
if (pulse & 004) {                                      /* load buffer */
    sim_activate (&tto_unit, tto_unit.wait);            /* activate unit */
    tto_unit.buf = dat & TTO_MASK;                      /* load buffer */
    }
return dat;
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

#if defined (KSR28)                                     /* Baudot... */
if (uptr->buf == BAUDOT_FIGURES)                        /* set figures? */
    tty_shift = 1;
else if (uptr->buf == BAUDOT_LETTERS)                   /* set letters? */
    tty_shift = 0;
else {
    c = baud_to_asc[uptr->buf | (tty_shift << 5)];      /* translate */

#else
c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags) | TTUF_KSR);
if (c >= 0) {

#endif

    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* retry? */
        return ((r == SCPE_STALL)? SCPE_OK: r);
        }
    }
SET_INT (TTO);                                          /* set flag */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* IORS service */

int32 tto_iors (void)
{
return (TST_INT (TTO)? IOS_TTO: 0);
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;                                       /* clear buffer */
tty_shift = 0;                                          /* clear state */
CLR_INT (TTO);                                          /* clear flag */
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Set mode */

t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~TT_MODE) | val;
tto_unit.flags = (tto_unit.flags & ~TT_MODE) | val;
return SCPE_OK;
}
