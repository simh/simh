/* vax780_stddev.c: VAX 11/780 standard I/O devices

   Copyright (c) 1998-2008, Robert M Supnik

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

   tti          console input
   tto          console output
   rx           console floppy
   todr         TODR clock
   tmr          interval timer

   17-Aug-08    RMS     Resync TODR on any clock reset
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input, clock
   29-Oct-06    RMS     Added clock coscheduler function
                        Synced keyboard to clock for idling
   11-May-06    RMS     Revised timer logic for EVKAE
   22-Nov-05    RMS     Revised for new terminal processing routines
   10-Mar-05    RMS     Fixed bug in timer schedule routine (from Mark Hittinger)
   08-Sep-04    RMS     Cloned from vax_stddev.c, vax_sysdev.c, and pdp11_rx.c

   The console floppy protocol is based on the description in the 1982 VAX
   Architecture Reference Manual:

   TXDB<11:8> = 0       ->      normal console output
   TXDB<11:8> = 1       ->      data output to floppy
   TXDB<11:8> = 3       ->      read communications region
   TXDB<11:8> = 9       ->      command output to floppy
   TXDB<11:8> = F       ->      flag output (e.g., reboot)

   RXDB<11:8> = 0       ->      normal terminal input
   RXDB<11:8> = 1       ->      data input from floppy
   RXDB<11:8> = 3       ->      communications region data
   RXDB<11:8> = 2       ->      status input from floppy
   RXDB<11:8> = 9       ->      "command" input from floppy (protocol error)
*/

#include "vax_defs.h"
#include <time.h>

/* Terminal definitions */

#define RXCS_RD         (CSR_DONE + CSR_IE)             /* terminal input */
#define RXCS_WR         (CSR_IE)
#define RXDB_ERR        0x8000                          /* error */
#define RXDB_OVR        0x4000                          /* overrun */
#define RXDB_FRM        0x2000                          /* framing error */
#define TXCS_RD         (CSR_DONE + CSR_IE)             /* terminal output */
#define TXCS_WR         (CSR_IE)
#define TXDB_V_SEL      8                               /* unit select */
#define TXDB_M_SEL      0xF
#define  TXDB_FDAT      0x1                             /* floppy data */
#define  TXDB_COMM      0x3                             /* console mem read */
#define  TXDB_FCMD      0x9                             /* floppy cmd */
#define  TXDB_MISC      0xF                             /* console misc */
#define COMM_LNT        0200                            /* comm region lnt */
#define COMM_MASK       (COMM_LNT - 1)                  /* comm region mask */
#define  COMM_GH        0144                            /* GH flag */
#define  COMM_WRMS      0145                            /* warm start */
#define  COMM_CLDS      0146                            /* cold start */
#define  COMM_APTL      0147                            /* APT load */
#define  COMM_LAST      0150                            /* last position */
#define  COMM_AUTO      0151                            /* auto restart */
#define  COMM_PCSV      0152                            /* PCS version */
#define  COMM_WCSV      0153                            /* WCS version */
#define  COMM_WCSS      0154                            /* WCS secondary */
#define  COMM_FPLV      0155                            /* FPLA version */
#define COMM_DATA       0x300                           /* comm data return */
#define MISC_MASK        0xFF                           /* console data mask */
#define  MISC_SWDN       0x1                            /* software done */
#define  MISC_BOOT       0x2                            /* reboot */
#define  MISC_CLWS       0x3                            /* clear warm start */
#define  MISC_CLCS       0x4                            /* clear cold start */
#define TXDB_SEL        (TXDB_M_SEL << TXDB_V_SEL)      /* non-terminal */
#define TXDB_GETSEL(x)  (((x) >> TXDB_V_SEL) & TXDB_M_SEL)

/* Clock definitions */

#define TMR_CSR_ERR     0x80000000                      /* error W1C */
#define TMR_CSR_DON     0x00000080                      /* done W1C */
#define TMR_CSR_IE      0x00000040                      /* int enb RW */
#define TMR_CSR_SGL     0x00000020                      /* single WO */
#define TMR_CSR_XFR     0x00000010                      /* xfer WO */
#define TMR_CSR_RUN     0x00000001                      /* run RW */
#define TMR_CSR_RD      (TMR_CSR_W1C | TMR_CSR_WR)
#define TMR_CSR_W1C     (TMR_CSR_ERR | TMR_CSR_DON)
#define TMR_CSR_WR      (TMR_CSR_IE | TMR_CSR_RUN)
#define TMR_INC         10000                           /* usec/interval */
#define CLK_DELAY       5000                            /* 100 Hz */
#define TMXR_MULT       1                               /* 100 Hz */

/* Floppy definitions */

#define FL_NUMTR        77                              /* tracks/disk */
#define FL_M_TRACK      0377
#define FL_NUMSC        26                              /* sectors/track */
#define FL_M_SECTOR     0177
#define FL_NUMBY        128                             /* bytes/sector */
#define FL_SIZE         (FL_NUMTR * FL_NUMSC * FL_NUMBY)        /* bytes/disk */
#define UNIT_V_WLK      (UNIT_V_UF)                     /* write locked */
#define UNIT_WLK        (1u << UNIT_V_UF)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

#define FL_IDLE         0                               /* idle state */
#define FL_RWDS         1                               /* rw, sect next */
#define FL_RWDT         2                               /* rw, track next */
#define FL_READ         3                               /* read */
#define FL_READ1        4
#define FL_WRITE        5                               /* write */
#define FL_WRITE1       6
#define FL_FILL         7                               /* fill buffer */
#define FL_EMPTY        8                               /* empty buffer */
#define FL_READSTA      9                               /* read status */
#define FL_DONE         10                              /* cmd done */

#define FL_V_FNC        0                               /* floppy function */
#define FL_M_FNC        0xFF
#define  FL_FNCRD       0x0                             /* read */
#define  FL_FNCWR       0x1                             /* write */
#define  FL_FNCRS       0x2                             /* read status */
#define  FL_FNCWD       0x3                             /* write del data */
#define  FL_FNCCA       0x4                             /* cancel */
#define FL_CDATA        0x100                           /* returned data */
#define FL_CDONE        0x200                           /* completion code */
#define  FL_STACRC      0x001                           /* status bits */
#define  FL_STAPAR      0x002
#define  FL_STAINC      0x004
#define  FL_STADDA      0x040
#define  FL_STAERR      0x080
#define FL_CPROT        0x905                           /* protocol error */
#define FL_GETFNC(x)    (((x) >> FL_V_FNC) & FL_M_FNC)

#define TRACK u3                                        /* current track */
#define CALC_DA(t,s) (((t) * FL_NUMSC) + ((s) - 1)) * FL_NUMBY

int32 tti_csr = 0;                                      /* control/status */
int32 tti_buf = 0;                                      /* buffer */
int32 tti_int = 0;                                      /* interrupt */
int32 tto_csr = 0;                                      /* control/status */
int32 tto_buf = 0;                                      /* buffer */
int32 tto_int = 0;                                      /* interrupt */

int32 tmr_iccs = 0;                                     /* interval timer csr */
uint32 tmr_icr = 0;                                     /* curr interval */
uint32 tmr_nicr = 0;                                    /* next interval */
uint32 tmr_inc = 0;                                     /* timer increment */
int32 tmr_sav = 0;                                      /* timer save */
int32 tmr_int = 0;                                      /* interrupt */
int32 tmr_use_100hz = 1;                                /* use 100Hz for timer */
int32 clk_tps = 100;                                    /* ticks/second */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;                /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* pgm timer poll */
int32 todr_reg = 0;                                     /* TODR register */

int32 fl_fnc = 0;                                       /* function */
int32 fl_esr = 0;                                       /* error status */
int32 fl_ecode = 0;                                     /* error code */
int32 fl_track = 0;                                     /* desired track */
int32 fl_sector = 0;                                    /* desired sector */
int32 fl_state = FL_IDLE;                               /* controller state */
int32 fl_stopioe = 1;                                   /* stop on error */
int32 fl_swait = 100;                                   /* seek, per track */
int32 fl_cwait = 50;                                    /* command time */
int32 fl_xwait = 20;                                    /* tr set time */
uint8 fl_buf[FL_NUMBY] = { 0 };                         /* sector buffer */
int32 fl_bptr = 0;                                      /* buffer pointer */

uint8 comm_region[COMM_LNT] = { 0 };                    /* comm region */

extern int32 sim_switches;
extern jmp_buf save_env;

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat tmr_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
t_stat tmr_reset (DEVICE *dptr);
t_stat fl_svc (UNIT *uptr);
t_stat fl_reset (DEVICE *dptr);
int32 icr_rd (t_bool interp);
void tmr_incr (uint32 inc);
void tmr_sched (void);
t_stat todr_resync (void);
t_stat fl_wr_txdb (int32 data);
t_bool fl_test_xfr (UNIT *uptr, t_bool wr);
void fl_protocol_error (void);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, TT_MODE_8B, 0), 0 };

REG tti_reg[] = {
    { HRDATA (RXDB, tti_buf, 16) },
    { HRDATA (RXCS, tti_csr, 16) },
    { FLDATA (INT, tti_int, 0) },
    { FLDATA (DONE, tti_csr, CSR_V_DONE) },
    { FLDATA (IE, tti_csr, CSR_V_IE) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { HRDATA (TXDB, tto_buf, 16) },
    { HRDATA (TXCS, tto_csr, 16) },
    { FLDATA (INT, tto_int, 0) },
    { FLDATA (DONE, tto_csr, CSR_V_DONE) },
    { FLDATA (IE, tto_csr, CSR_V_IE) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT + REG_NZ },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* TODR and TMR data structures */

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), CLK_DELAY };          /* 100Hz */

REG clk_reg[] = {
    { DRDATA (TODR, todr_reg, 32), PV_LEFT },
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 8), REG_HIDDEN + REG_NZ + PV_LEFT },
    { NULL }
    };

DEVICE clk_dev = {
    "TODR", &clk_unit, clk_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

UNIT tmr_unit = { UDATA (&tmr_svc, 0, 0) };                     /* timer */

REG tmr_reg[] = {
    { HRDATA (ICCS, tmr_iccs, 32) },
    { HRDATA (ICR, tmr_icr, 32) },
    { HRDATA (NICR, tmr_nicr, 32) },
    { HRDATA (INCR, tmr_inc, 32), REG_HIDDEN },
    { HRDATA (SAVE, tmr_sav, 32), REG_HIDDEN },
    { FLDATA (USE100HZ, tmr_use_100hz, 0), REG_HIDDEN },
    { FLDATA (INT, tmr_int, 0) },
    { NULL }
    };

DEVICE tmr_dev = {
    "TMR", &tmr_unit, tmr_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &tmr_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* RX01 data structures

   fl_dev       RX device descriptor
   fl_unit      RX unit list
   fl_reg       RX register list
   fl_mod       RX modifier list
*/

UNIT fl_unit = { UDATA (&fl_svc,
      UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, FL_SIZE) };

REG fl_reg[] = {
    { HRDATA (FNC, fl_fnc, 8) },
    { HRDATA (ES, fl_esr, 8) },
    { HRDATA (ECODE, fl_ecode, 8) },
    { HRDATA (TA, fl_track, 8) },
    { HRDATA (SA, fl_sector, 8) },
    { DRDATA (STATE, fl_state, 4), REG_RO },
    { DRDATA (BPTR, fl_bptr, 7)  },
    { DRDATA (CTIME, fl_cwait, 24), PV_LEFT },
    { DRDATA (STIME, fl_swait, 24), PV_LEFT },
    { DRDATA (XTIME, fl_xwait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, fl_stopioe, 0) },
    { BRDATA (DBUF, fl_buf, 16, 8, FL_NUMBY) },
    { BRDATA (COMM, comm_region, 16, 8, COMM_LNT) },
    { NULL }
    };

MTAB fl_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { 0 }
    };

DEVICE fl_dev = {
    "RX", &fl_unit, fl_reg, fl_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &fl_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Terminal MxPR routines

   rxcs_rd/wr   input control/status
   rxdb_rd      input buffer
   txcs_rd/wr   output control/status
   txdb_wr      output buffer
*/

int32 rxcs_rd (void)
{
return (tti_csr & RXCS_RD);
}

void rxcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tto_int = 0;
else if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    tti_int = 1;
tti_csr = (tti_csr & ~RXCS_WR) | (data & RXCS_WR);
return;
}

int32 rxdb_rd (void)
{
int32 t = tti_buf;                                      /* char + error */

tti_csr = tti_csr & ~CSR_DONE;                          /* clr done */
tti_buf = tti_buf & BMASK;                              /* clr errors */
tti_int = 0;
return t;
}

int32 txcs_rd (void)
{
return (tto_csr & TXCS_RD);
}

void txcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tto_int = 0;
else if ((tto_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    tto_int = 1;
tto_csr = (tto_csr & ~TXCS_WR) | (data & TXCS_WR);
return;
}

void txdb_wr (int32 data)
{
tto_buf = data & WMASK;                                 /* save data */
tto_csr = tto_csr & ~CSR_DONE;                          /* clear flag */
tto_int = 0;                                            /* clear int */
if (tto_buf & TXDB_SEL)                                 /* floppy? */
    fl_wr_txdb (tto_buf);
else sim_activate (&tto_unit, tto_unit.wait);           /* no, console */
return;
}

/* Terminal input service (poll for character) */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_activate (uptr, KBD_WAIT (uptr->wait, tmr_poll));   /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    tti_buf = RXDB_ERR | RXDB_FRM;
else tti_buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    tti_int = 1;
return SCPE_OK;
}

/* Terminal input reset */

t_stat tti_reset (DEVICE *dptr)
{
tti_buf = 0;
tti_csr = 0;
tti_int = 0;
sim_activate_abs (&tti_unit, KBD_WAIT (tti_unit.wait, tmr_poll));
return SCPE_OK;
}

/* Terminal output service (output character) */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

if ((tto_buf & TXDB_SEL) == 0) {                        /* for console? */
    c = sim_tt_outcvt (tto_buf, TT_GET_MODE (uptr->flags));
    if (c >= 0) {
        if ((r = sim_putchar_s (c)) != SCPE_OK) {       /* output; error? */
            sim_activate (uptr, uptr->wait);            /* retry */
            return ((r == SCPE_STALL)? SCPE_OK: r);     /* !stall? report */
            }
        }
    uptr->pos = uptr->pos + 1;
    }
tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE)
    tto_int = 1;
return SCPE_OK;
}

/* Terminal output reset */

t_stat tto_reset (DEVICE *dptr)
{
tto_buf = 0;
tto_csr = CSR_DONE;
tto_int = 0;
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Programmable timer

   The architected VAX timer, which increments at 1Mhz, cannot be
   accurately simulated due to the overhead that would be required
   for 1M clock events per second.  Instead, a hidden calibrated
   100Hz timer is run (because that's what VMS expects), and a
   hack is used for the interval timer.

   When the timer is started, the timer interval is inspected.

   if the interval is >= 10msec, then the 100Hz timer drives the
        next interval
   if the interval is < 10mec, then count instructions

   If the interval register is read, then its value between events
   is interpolated using the current instruction count versus the
   count when the most recent event started, the result is scaled
   to the calibrated system clock, unless the interval being timed
   is less than a calibrated system clock tick (or the calibrated 
   clock is running very slowly) at which time the result will be 
   the elapsed instruction count.
*/

int32 iccs_rd (void)
{
return tmr_iccs & TMR_CSR_RD;
}

void iccs_wr (int32 val)
{
if ((val & TMR_CSR_RUN) == 0) {                         /* clearing run? */
    sim_cancel (&tmr_unit);                             /* cancel timer */
    tmr_use_100hz = 0;
    if (tmr_iccs & TMR_CSR_RUN)                         /* run 1 -> 0? */
        tmr_icr = icr_rd (TRUE);                        /* update itr */
    }
tmr_iccs = tmr_iccs & ~(val & TMR_CSR_W1C);             /* W1C csr */
tmr_iccs = (tmr_iccs & ~TMR_CSR_WR) |                   /* new r/w */
    (val & TMR_CSR_WR);
if (val & TMR_CSR_XFR) tmr_icr = tmr_nicr;              /* xfr set? */
if (val & TMR_CSR_RUN)  {                               /* run? */
    if (val & TMR_CSR_XFR)                              /* new tir? */
        sim_cancel (&tmr_unit);                         /* stop prev */
    if (!sim_is_active (&tmr_unit))                     /* not running? */
        tmr_sched ();                                   /* activate */
    }
else if (val & TMR_CSR_SGL) {                           /* single step? */
    tmr_incr (1);                                       /* incr tmr */
    if (tmr_icr == 0)                                   /* if ovflo, */
        tmr_icr = tmr_nicr;                             /* reload tir */
    }
if ((tmr_iccs & (TMR_CSR_DON | TMR_CSR_IE)) !=          /* update int */
    (TMR_CSR_DON | TMR_CSR_IE))
    tmr_int = 0;
return;
}

int32 icr_rd (t_bool interp)
{
uint32 delta;

if (interp || (tmr_iccs & TMR_CSR_RUN)) {               /* interp, running? */
    delta = sim_grtime () - tmr_sav;                    /* delta inst */
    if (tmr_use_100hz && (tmr_poll > TMR_INC))          /* scale large int */
        delta = (uint32) ((((double) delta) * TMR_INC) / tmr_poll);
    if (delta >= tmr_inc)
        delta = tmr_inc - 1;
    return tmr_icr + delta;
    }
return tmr_icr;
}

int32 nicr_rd ()
{
return tmr_nicr;
}

void nicr_wr (int32 val)
{
tmr_nicr = val;
}

/* 100Hz base clock unit service */

t_stat clk_svc (UNIT *uptr)
{
tmr_poll = sim_rtcn_calb (clk_tps, TMR_CLK);            /* calibrate clock */
sim_activate (&clk_unit, tmr_poll);                     /* reactivate unit */
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
todr_reg = todr_reg + 1;                                /* incr TODR */
if ((tmr_iccs & TMR_CSR_RUN) && tmr_use_100hz)          /* timer on, std intvl? */
    tmr_incr (TMR_INC);                                 /* do timer service */
return SCPE_OK;
}

/* Interval timer unit service */

t_stat tmr_svc (UNIT *uptr)
{
tmr_incr (tmr_inc);                                     /* incr timer */
return SCPE_OK;
}

/* Timer increment */

void tmr_incr (uint32 inc)
{
uint32 new_icr = (tmr_icr + inc) & LMASK;               /* add incr */

if (new_icr < tmr_icr) {                                /* ovflo? */
    tmr_icr = 0;                                        /* now 0 */
    if (tmr_iccs & TMR_CSR_DON)                         /* done? set err */
        tmr_iccs = tmr_iccs | TMR_CSR_ERR;
    else tmr_iccs = tmr_iccs | TMR_CSR_DON;             /* set done */
    if (tmr_iccs & TMR_CSR_RUN) {                       /* run? */
        tmr_icr = tmr_nicr;                             /* reload */
        tmr_sched ();                                   /* reactivate */
        }
    if (tmr_iccs & TMR_CSR_IE)                          /* ie? set int req */
        tmr_int = 1;
    else tmr_int = 0;
    }
else {
    tmr_icr = new_icr;                                  /* no, update icr */
    if (tmr_iccs & TMR_CSR_RUN)                         /* still running? */
        tmr_sched ();                                   /* reactivate */
    }
return;
}

/* Timer scheduling */

void tmr_sched (void)
{
tmr_sav = sim_grtime ();                                /* save intvl base */
tmr_inc = (~tmr_icr + 1);                               /* inc = interval */
if (tmr_inc == 0) tmr_inc = 1;
if (tmr_inc < TMR_INC) {                                /* 100Hz multiple? */
    sim_activate (&tmr_unit, tmr_inc);                  /* schedule timer */
    tmr_use_100hz = 0;
    }
else tmr_use_100hz = 1;                                 /* let clk handle */
return;
}

/* Clock coscheduling routine */

int32 clk_cosched (int32 wait)
{
int32 t;

t = sim_is_active (&clk_unit);
return (t? t - 1: wait);
}

/* 100Hz clock reset */

t_stat clk_reset (DEVICE *dptr)
{
tmr_poll = sim_rtcn_init (clk_unit.wait, TMR_CLK);      /* init 100Hz timer */
sim_activate_abs (&clk_unit, tmr_poll);                 /* activate 100Hz unit */
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
return SCPE_OK;
}

/* Interval timer reset */

t_stat tmr_reset (DEVICE *dptr)
{
tmr_iccs = 0;
tmr_icr = 0;
tmr_nicr = 0;
tmr_int = 0;
tmr_use_100hz = 1;
sim_cancel (&tmr_unit);                                 /* cancel timer */
todr_resync ();                                         /* resync TODR */
return SCPE_OK;
}

/* TODR routines */

int32 todr_rd (void)
{
return todr_reg;
}

void todr_wr (int32 data)
{
todr_reg = data;
return;
}

t_stat todr_resync (void)
{
uint32 base;
time_t curr;
struct tm *ctm;

curr = time (NULL);                                     /* get curr time */
if (curr == (time_t) -1)                                /* error? */
    return SCPE_NOFNC;
ctm = localtime (&curr);                                /* decompose */
if (ctm == NULL)                                        /* error? */
    return SCPE_NOFNC;
base = (((((ctm->tm_yday * 24) +                        /* sec since 1-Jan */
        ctm->tm_hour) * 60) +
        ctm->tm_min) * 60) +
        ctm->tm_sec;
todr_reg = (base * 100) + 0x10000000;                   /* cvt to VAX form */
return SCPE_OK;
}

/* Console write, txdb<11:8> != 0 (console unit) */

t_stat fl_wr_txdb (int32 data)
{
int32 sel = TXDB_GETSEL (data);                         /* get selection */

if (sel == TXDB_FCMD) {                                 /* floppy command? */
    fl_fnc = FL_GETFNC (data);                          /* get function */
    if (fl_state != FL_IDLE)                            /* cmd in prog? */
        switch (fl_fnc) {

        case FL_FNCCA:                                  /* cancel? */
            sim_cancel (&fl_unit);                      /* stop op */
            fl_state = FL_DONE;
            break;

        default:                                        /* all others */
            fl_protocol_error ();
            return SCPE_OK;
            }

        else switch (fl_fnc) {                          /* idle, case */

        case FL_FNCRS:                                  /* read status */
            fl_state = FL_READSTA;
            break;

        case FL_FNCCA:                                  /* cancel, nop */
            fl_state = FL_DONE;
            break;

        case FL_FNCRD: case FL_FNCWR:                   /* data xfer */
        case FL_FNCWD:
            fl_esr = 0;                                 /* clear errors */
            fl_ecode = 0;
            fl_bptr = 0;                                /* init buffer */
            fl_state = FL_RWDS;                         /* sector next */
            break;

        default:                                        /* all others */
            fl_protocol_error ();
            return SCPE_OK;
            }

    sim_activate (&fl_unit, fl_cwait);                  /* sched command */
    }                                                   /* end command */
else if (sel == TXDB_FDAT) {                            /* floppy data? */
    switch (fl_state) {                                 /* data */

        case FL_RWDS:                                   /* expecting sector */
            fl_sector = data & FL_M_SECTOR;
            fl_state = FL_RWDT;
            break;

        case FL_RWDT:                                   /* expecting track */
            fl_track = data & FL_M_TRACK;
            if (fl_fnc == FL_FNCRD)
                fl_state = FL_READ;
            else fl_state = FL_FILL;
            break;

        case FL_FILL:                                   /* expecting wr data */
            fl_buf[fl_bptr++] = data & BMASK;
            if (fl_bptr >= FL_NUMBY)
                fl_state = FL_WRITE;
            break;

        default:
            fl_protocol_error ();
            return SCPE_OK;
            }

    sim_activate (&fl_unit, fl_xwait);                  /* schedule xfer */
    }                                                   /* end else data */
else {
    sim_activate (&tto_unit, tto_unit.wait);            /* set up timeout */
    if (sel == TXDB_COMM) {                             /* read comm region? */
        data = data & COMM_MASK;                        /* byte to select */
        tti_buf = comm_region[data] | COMM_DATA;
        tti_csr = tti_csr | CSR_DONE;                   /* set input flag */
        if (tti_csr & CSR_IE)
            tti_int = 1;
        }
    else if (sel == TXDB_MISC) {                        /* misc function? */
        switch (data & MISC_MASK) {                     /* case on function */
        case MISC_CLWS:
            comm_region[COMM_WRMS] = 0;
        case MISC_CLCS:
            comm_region[COMM_CLDS] = 0;
            break;
        case MISC_SWDN:
            ABORT (STOP_SWDN);
            break;
        case MISC_BOOT:
            ABORT (STOP_BOOT);
            break;
            }
        }
    }
return SCPE_OK;
}

/* Unit service; the action to be taken depends on the transfer state:

   FL_IDLE              Should never get here
   FL_RWDS              Set TXCS<done> (driver sends sector, sets FL_RWDT)
   FL_RWDT              Set TXCS<done> (driver sends track, sets FL_READ/FL_FILL)
   FL_READ              Set TXCS<done>, schedule FL_READ1
   FL_READ1             Read sector, schedule FL_EMPTY
   FL_EMPTY             Copy data to RXDB, set RXCS<done>
                        if fl_bptr >= max, schedule completion, else continue
   FL_FILL              Set TXCS<done> (driver sends next byte, sets FL_WRITE)
   FL_WRITE             Set TXCS<done>, schedule FL_WRITE1
   FL_WRITE1            Write sector, schedule FL_DONE
   FL_DONE              Copy requested data to TXDB, set FL_IDLE
*/

t_stat fl_svc (UNIT *uptr)
{
int32 i, t;
uint32 da;
int8 *fbuf = uptr->filebuf;

switch (fl_state) {                                     /* case on state */

    case FL_IDLE:                                       /* idle */
        return SCPE_IERR;                               /* done */

    case FL_READ: case FL_WRITE:                        /* read, write */
        fl_state = fl_state + 1;                        /* set next state */
        t = abs (fl_track - uptr->TRACK);               /* # tracks to seek */
        if (t == 0)                                     /* minimum 1 */
            t = 1;
        sim_activate (uptr, fl_swait * t);              /* schedule seek */
                                                        /* fall thru, set flag */
    case FL_RWDS: case FL_RWDT: case FL_FILL:           /* rwds, rwdt, fill */
        tto_csr = tto_csr | CSR_DONE;                   /* set output done */
        if (tto_csr & CSR_IE)
            tto_int = 1;
        break;

    case FL_READ1:                                      /* read, seek done */
        if (fl_test_xfr (uptr, FALSE)) {                /* transfer ok? */
            da = CALC_DA (fl_track, fl_sector);         /* get disk address */
            for (i = 0; i < FL_NUMBY; i++)              /* copy sector to buf */
                fl_buf[i] = fbuf[da + i];
            tti_buf = fl_esr | FL_CDONE;                /* completion code */
            tti_csr = tti_csr | CSR_DONE;               /* set input flag */
            if (tti_csr & CSR_IE)
                tti_int = 1;      
            fl_state = FL_EMPTY;                        /* go empty */
            }
        else fl_state = FL_DONE;                        /* error? cmd done */
        sim_activate (uptr, fl_xwait);                  /* schedule next */
        break;

    case FL_EMPTY:                                      /* empty buffer */
        if ((tti_csr & CSR_DONE) == 0) {                /* prev data taken? */
            tti_buf = FL_CDATA | fl_buf[fl_bptr++];     /* get next byte */
            tti_csr = tti_csr | CSR_DONE;               /* set input flag */
            if (tti_csr & CSR_IE)
                tti_int = 1;
            if (fl_bptr >= FL_NUMBY) {                  /* buffer empty? */
                fl_state = FL_IDLE;                     /* cmd done */
                break;
                }
            }
        sim_activate (uptr, fl_xwait);                  /* schedule next */
        break;

    case FL_WRITE1:                                     /* write, seek done */
        if (fl_test_xfr (uptr, TRUE)) {                 /* transfer ok? */
            da = CALC_DA (fl_track, fl_sector);         /* get disk address */
            for (i = 0; i < FL_NUMBY; i++)              /* copy buf to sector */
                fbuf[da + i] = fl_buf[i];
            da = da + FL_NUMBY;
            if (da > uptr->hwmark)                      /* update hwmark */
                uptr->hwmark = da;
            }
        if (fl_fnc == FL_FNCWD)                         /* wrdel? set status */
            fl_esr |= FL_STADDA;
        fl_state = FL_DONE;                             /* command done */
        sim_activate (uptr, fl_xwait);                  /* schedule */
        break;

    case FL_DONE:                                       /* command done */
        if (tti_csr & CSR_DONE)                         /* input buf empty? */
            sim_activate (uptr, fl_xwait);              /* no, wait */
        else {                                          /* yes */
            tti_buf = fl_esr | FL_CDONE;                /* completion code */
            tti_csr = tti_csr | CSR_DONE;               /* set input flag */
            if (tti_csr & CSR_IE)
                tti_int = 1;
            fl_state = FL_IDLE;                         /* floppy idle */
            }
        break;    

    case FL_READSTA:                                    /* read status */
        if ((tti_csr & CSR_DONE) == 0) {                /* input buf empty? */
            tti_buf = fl_ecode;                         /* return err code */
            tti_csr = tti_csr | CSR_DONE;               /* set input flag */
            if (tti_csr & CSR_IE)
                tti_int = 1;
            fl_state = FL_DONE;                         /* command done */
            }
        sim_activate (uptr, fl_xwait);
        break;
        }
return SCPE_OK;
}

/* Test for data transfer okay */

t_bool fl_test_xfr (UNIT *uptr, t_bool wr)
{
if ((uptr->flags & UNIT_BUF) == 0)                      /* not buffered? */
    fl_ecode = 0110;
else if (fl_track >= FL_NUMTR)                          /* bad track? */
    fl_ecode = 0040;                                    /* done, error */
else if ((fl_sector == 0) || (fl_sector > FL_NUMSC))    /* bad sect? */
    fl_ecode = 0070;                                    /* done, error */
else if (wr && (uptr->flags & UNIT_WPRT))               /* write and locked? */
    fl_ecode = 0100;                                    /* done, error */
else {
    uptr->TRACK = fl_track;                             /* now on track */
    return TRUE;
    }
fl_esr = fl_esr | FL_STAERR;                            /* set error */
return FALSE;
}

/* Set protocol error */

void fl_protocol_error (void)
{
if ((tto_csr & CSR_DONE) == 0) {                        /* output busy? */
    tto_csr = tto_csr | CSR_DONE;                       /* set done */
    if (tto_csr & CSR_IE)
        tto_int = 1;
    }
if ((tti_csr & CSR_DONE) == 0) {                        /* input idle? */
    tti_csr = tti_csr | CSR_DONE;                       /* set done */
    if (tti_csr & CSR_IE)
        tti_int = 1;
    }
tti_buf = FL_CPROT;                                     /* status */
fl_state = FL_IDLE;                                     /* floppy idle */
return;
}

/* Reset */

t_stat fl_reset (DEVICE *dptr)
{
uint32 i;

fl_esr = FL_STAINC;
fl_ecode = 0;                                           /* clear error */
fl_sector = 0;                                          /* clear addr */
fl_track = 0;
fl_state = FL_IDLE;                                     /* ctrl idle */
fl_bptr = 0;
sim_cancel (&fl_unit);                                  /* cancel drive */
fl_unit.TRACK = 0;
for (i = 0; i < COMM_LNT; i++)
    comm_region[i] = 0;
comm_region[COMM_FPLV] = VER_FPLA;
comm_region[COMM_PCSV] = VER_PCS;
comm_region[COMM_WCSV] = VER_WCSP;
comm_region[COMM_WCSS] = VER_WCSS;
comm_region[COMM_GH] = 1;
return SCPE_OK;
}
