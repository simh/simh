/* pdp11_stddev.c: PDP-11 standard I/O devices simulator

   Copyright (c) 1993-2015, Robert M Supnik

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

   tti,tto      DL11 terminal input/output
   clk          KW11L (and other) line frequency clock

   30-Dec-15    RMS     Added NOBEVENT support
   18-Apr-12    RMS     Modified to use clock coscheduling
   20-May-08    RMS     Standardized clock delay at 1mips
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input, clock
   29-Oct-06    RMS     Synced keyboard and clock
                        Added clock coscheduling support
   05-Jul-06    RMS     Added UC only support for early DOS/RSTS
   22-Nov-05    RMS     Revised for new terminal processing routines
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   07-Jul-05    RMS     Removed extraneous externs
   11-Oct-04    RMS     Added clock model dependencies
   28-May-04    RMS     Removed SET TTI CTRL-C
   29-Dec-03    RMS     Added console backpressure support
   25-Apr-03    RMS     Revised for extended file support
   01-Mar-03    RMS     Added SET/SHOW CLOCK FREQ, SET TTI CTRL-C
   22-Nov-02    RMS     Changed terminal default to 7B for UNIX
   01-Nov-02    RMS     Added 7B/8B support to terminal
   29-Sep-02    RMS     Added vector display support
                        Split out paper tape
                        Split DL11 dibs
   30-May-02    RMS     Widened POS to 32b
   26-Jan-02    RMS     Revised for multiple timers
   09-Jan-02    RMS     Fixed bugs in KW11L (John Dundas)
   06-Jan-02    RMS     Split I/O address routines, revised enable/disable support
   29-Nov-01    RMS     Added read only unit support
   09-Nov-01    RMS     Added RQDX3 support
   07-Oct-01    RMS     Upgraded clock to full KW11L for RSTS/E autoconfigure
   07-Sep-01    RMS     Moved function prototypes, revised interrupt mechanism
   17-Jul-01    RMS     Moved function prototype
   04-Jul-01    RMS     Added DZ11 support
   05-Mar-01    RMS     Added clock calibration support
   30-Oct-00    RMS     Standardized register order
   25-Jun-98    RMS     Fixed bugs in paper tape error handling
*/

#include "pdp11_defs.h"
#include "sim_tmxr.h"

#define TTICSR_IMP      (CSR_DONE + CSR_IE)             /* terminal input */
#define TTICSR_RW       (CSR_IE)
#define TTOCSR_IMP      (CSR_DONE + CSR_IE)             /* terminal output */
#define TTOCSR_RW       (CSR_IE)
#define CLKCSR_IMP      (CSR_DONE + CSR_IE)             /* real-time clock */
#define CLKCSR_RW       (CSR_IE)
#define CLK_DELAY       16667

int32 tti_csr = 0;                                      /* control/status */
uint32 tti_buftime;                                     /* time input character arrived */
int32 tto_csr = 0;                                      /* control/status */
int32 clk_csr = 0;                                      /* control/status */
int32 clk_tps = 60;                                     /* ticks/second */
int32 clk_default = 60;                                 /* default ticks/second */
int32 clk_fie = 0;                                      /* force IE = 1 */
int32 clk_fnxm = 0;                                     /* force NXM on reg */
int32 tmxr_poll = CLK_DELAY;                            /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* timer poll */

t_stat tti_rd (int32 *data, int32 PA, int32 access);
t_stat tti_wr (int32 data, int32 PA, int32 access);
t_stat tti_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_rd (int32 *data, int32 PA, int32 access);
t_stat tto_wr (int32 data, int32 PA, int32 access);
t_stat tto_svc (UNIT *uptr);
t_stat tto_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat clk_rd (int32 *data, int32 PA, int32 access);
t_stat clk_wr (int32 data, int32 PA, int32 access);
t_stat clk_svc (UNIT *uptr);
int32 clk_inta (void);
t_stat clk_reset (DEVICE *dptr);
t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

DIB tti_dib = {
    IOBA_TTI, IOLN_TTI, &tti_rd, &tti_wr,
    1, IVCL (TTI), VEC_TTI, { NULL }
    };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE, 0), TMLN_SPD_9600_BPS };

REG tti_reg[] = {
    { HRDATAD (BUF,       tti_unit.buf,          8, "last data item processed") },
    { HRDATAD (CSR,            tti_csr,         16, "control/status register") },
    { FLDATAD (INT,         IREQ (TTI),  INT_V_TTI, "interrupt pending flag") },
    { FLDATAD (DONE,           tti_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (ERR,            tti_csr,  CSR_V_ERR, "device error flag (CSR<15>)") },
    { FLDATAD (IE,             tti_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,       tti_unit.pos,   T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME,     tti_unit.wait,         24, "input polling interval"), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7b", NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, DEV_UBUS | DEV_QBUS
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = {
    IOBA_TTO, IOLN_TTO, &tto_rd, &tto_wr,
    1, IVCL (TTO), VEC_TTO, { NULL }
    };

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_7P, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, 8) },
    { ORDATA (CSR, tto_csr, 16) },
    { FLDATA (INT, IREQ (TTO), INT_V_TTO) },
    { FLDATA (ERR, tto_csr, CSR_V_ERR) },
    { FLDATA (DONE, tto_csr, CSR_V_DONE) },
    { FLDATA (IE, tto_csr, CSR_V_IE) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &tty_set_mode },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, DEV_UBUS | DEV_QBUS
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

#define IOLN_CLK        002

DIB clk_dib = {
    IOBA_AUTO, IOLN_CLK, &clk_rd, &clk_wr,
    1, IVCL (CLK), VEC_AUTO, { &clk_inta }
    };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), CLK_DELAY };

BITFIELD clk_bits[] = {
    BITNCF(6),                              /* MBZ */
    BIT(IE),                                /* Interrupt Enable */
    BIT(DONE),                              /* Done */
    ENDBITS
};

REG clk_reg[] = {
    { ORDATADF (CSR, clk_csr, 16, "Control Status Register", clk_bits) },
    { FLDATAD (INT, IREQ (CLK), INT_V_CLK, "Processor Interrupt Pending") },
    { FLDATAD (DONE, clk_csr, CSR_V_DONE, "Tick Interval Complete") },
    { FLDATAD (IE, clk_csr, CSR_V_IE, "Interrupt Enabled") },
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 16), PV_LEFT + REG_HRO },
    { DRDATA (DEFTPS, clk_default, 16), PV_LEFT + REG_HRO },
    { FLDATA (FIE, clk_fie, 0), REG_HIDDEN },
    { FLDATA (FNXM, clk_fnxm, 0), REG_HIDDEN },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &clk_show_freq, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

#define DBG_RREG     1   /* register read access */
#define DBG_WREG     2   /* register write access */
#define DBG_INT      4   /* interrupt activity */
#define DBG_INTA     8   /* interrupt activity */

DEBTAB clk_debug[] = {
  {"RREG",  DBG_RREG,   "register read access"},
  {"WREG",  DBG_WREG,   "register write access"},
  {"INT",   DBG_INT,    "interrupt activity"},
  {"INTA",  DBG_INTA,   "interrupt acknowledgement"},
  {0}
};

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, DEV_DEBUG | DEV_UBUS | DEV_QBUS, 0,
    clk_debug
    };

/* Terminal input address routines */

t_stat tti_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 00:                                            /* tti csr */
        *data = tti_csr & TTICSR_IMP;
        return SCPE_OK;

    case 01:                                            /* tti buf */
        tti_csr = tti_csr & ~CSR_DONE;
        CLR_INT (TTI);
        *data = tti_unit.buf & 0377;
        sim_activate_after_abs (&tti_unit, tti_unit.wait);  /* check soon for more input */
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

t_stat tti_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 00:                                            /* tti csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            CLR_INT (TTI);
        else if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
            SET_INT (TTI);
        tti_csr = (tti_csr & ~TTICSR_RW) | (data & TTICSR_RW);
        return SCPE_OK;

    case 01:                                            /* tti buf */
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal input service */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */

if ((tti_csr & CSR_DONE) &&                             /* input still pending and < 500ms? */
    ((sim_os_msec () - tti_buftime) < 500))
     return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    uptr->buf = 0;
else uptr->buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
tti_buftime = sim_os_msec ();
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    SET_INT (TTI);
return SCPE_OK;
}

/* Terminal input reset */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_unit.buf = 0;
tti_csr = 0;
CLR_INT (TTI);
sim_activate (&tti_unit, tmr_poll);
return SCPE_OK;
}

/* Terminal output address routines */

t_stat tto_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 00:                                            /* tto csr */
        *data = tto_csr & TTOCSR_IMP;
        return SCPE_OK;

    case 01:                                            /* tto buf */
        *data = tto_unit.buf;
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

t_stat tto_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 00:                                            /* tto csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            CLR_INT (TTO);
        else if ((tto_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
            SET_INT (TTO);
        tto_csr = (tto_csr & ~TTOCSR_RW) | (data & TTOCSR_RW);
        return SCPE_OK;

    case 01:                                            /* tto buf */
        if ((PA & 1) == 0)
            tto_unit.buf = data & 0377;
        tto_csr = tto_csr & ~CSR_DONE;
        CLR_INT (TTO);
        sim_activate (&tto_unit, tto_unit.wait);
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal output service */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags));
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* try again */
        return ((r == SCPE_STALL)? SCPE_OK: r);         /* !stall? report */
        }
    }
tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE)
    SET_INT (TTO);
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* Terminal output reset */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
tto_csr = CSR_DONE;
CLR_INT (TTO);
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~TT_MODE) | val;
tto_unit.flags = (tto_unit.flags & ~TT_MODE) | val;
return SCPE_OK;
}

/* The line time clock has a few twists and turns through the history of 11's

   LSI-11               no CSR
   LSI-11/23 (KDF11A)   no CSR
   PDP-11/23+ (KDF11B)  no monitor bit
   PDP-11/24 (KDF11U)   monitor bit clears on IAK
*/

/* Clock I/O address routines */

t_stat clk_rd (int32 *data, int32 PA, int32 access)
{
int32 orig_csr = clk_csr;

if (clk_fnxm)                                           /* not there??? */
    return SCPE_NXM;
if (CPUT (HAS_LTCM))                                    /* monitor bit? */
    *data = clk_csr & CLKCSR_IMP;
else *data = clk_csr & (CLKCSR_IMP & ~CSR_DONE);        /* no, just IE */
sim_debug_bits(DBG_RREG, &clk_dev, clk_bits, orig_csr, *data, 1);
return SCPE_OK;
}

t_stat clk_wr (int32 data, int32 PA, int32 access)
{
int32 orig_csr = clk_csr;

if (clk_fnxm)                                           /* not there??? */
    return SCPE_NXM;
if (PA & 1)
    return SCPE_OK;
clk_csr = (clk_csr & ~CLKCSR_RW) | (data & CLKCSR_RW);
if (CPUT (HAS_LTCM) && ((data & CSR_DONE) == 0))        /* monitor bit? */
    clk_csr = clk_csr & ~CSR_DONE;                      /* clr if zero */
if ((((clk_csr & CSR_IE) == 0) && !clk_fie) ||          /* unless IE+DONE */
    ((clk_csr & CSR_DONE) == 0)) {                      /* clr intr */
    CLR_INT (CLK);
    sim_debug (DBG_INT, &clk_dev, "CLR_INT(CLK)\n");
    }
sim_debug_bits(DBG_WREG, &clk_dev, clk_bits, orig_csr, clk_csr, 1);
return SCPE_OK;
}

/* Clock service */

t_stat clk_svc (UNIT *uptr)
{
int32 t;

clk_csr = clk_csr | CSR_DONE;                           /* set done */
if ((clk_csr & CSR_IE) || clk_fie) {
    SET_INT (CLK);
    sim_debug (DBG_INT, &clk_dev, "SET_INT(CLK)\n");
    }
t = sim_rtcn_calb (clk_tps, TMR_CLK);                   /* calibrate clock */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
tmr_poll = t;                                           /* set timer poll */
tmxr_poll = t;                                          /* set mux poll */
return SCPE_OK;
}

/* Clock interrupt acknowledge */

int32 clk_inta (void)
{
if (CPUT (CPUT_24))
    clk_csr = clk_csr & ~CSR_DONE;
sim_debug (DBG_INTA, &clk_dev, "clk_inta() returning vector 0%o\n", clk_dib.vec);
return clk_dib.vec;
}

/* Clock reset */

t_stat clk_reset (DEVICE *dptr)
{
if (CPUT (HAS_LTCR))                                    /* reg there? */
    clk_fie = clk_fnxm = 0;
else {
    clk_fnxm = 1;                                       /* no LTCR, set nxm */
    clk_fie = CPUO (OPT_BVT);                           /* ie = 1 unless no BEVENT */
    }
clk_tps = clk_default;                                  /* set default tps */
clk_csr = CSR_DONE;                                     /* set done */
CLR_INT (CLK);
tmr_poll = sim_rtcn_init_unit (&clk_unit, clk_unit.wait, TMR_CLK);/* init line clock */
sim_activate_after (&clk_unit, 1000000/clk_tps);        /* activate unit */
tmr_poll = clk_unit.wait;                               /* set timer poll */
tmxr_poll = clk_unit.wait;                              /* set mux poll */
return SCPE_OK;
}

/* Set frequency */

t_stat clk_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 50) && (val != 60))
    return SCPE_IERR;
clk_tps = clk_default = val;
return SCPE_OK;
}

/* Show frequency */

t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%dHz", clk_tps);
return SCPE_OK;
}
