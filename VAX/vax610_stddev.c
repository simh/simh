/* vax610_stddev.c: MicroVAX I standard I/O devices

   Copyright (c) 2011-2012, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   tti          terminal input
   tto          terminal output
   clk          100Hz clock

   15-Feb-2012  MB      First Version
*/

#include "vax_defs.h"
#include <time.h>

#define TTICSR_IMP      (CSR_DONE + CSR_IE)             /* terminal input */
#define TTICSR_RW       (CSR_IE)
#define TTIBUF_ERR      0x8000                          /* error */
#define TTIBUF_OVR      0x4000                          /* overrun */
#define TTIBUF_FRM      0x2000                          /* framing error */
#define TTIBUF_RBR      0x0400                          /* receive break */
#define TTOCSR_IMP      (CSR_DONE + CSR_IE)             /* terminal output */
#define TTOCSR_RW       (CSR_IE)
#define TXDB_V_SEL      8                               /* unit select */
#define TXDB_M_SEL      0xF
#define  TXDB_MISC      0xF                             /* console misc */
#define MISC_MASK        0xFF                           /* console data mask */
#define  MISC_BOOT       0x2                            /* reboot */
#define  MISC_CLWS       0x3                            /* clear warm start */
#define  MISC_CLCS       0x4                            /* clear cold start */
#define  MISC_SWDN       0x5                            /* software done */
#define TXDB_SEL        (TXDB_M_SEL << TXDB_V_SEL)      /* non-terminal */
#define TXDB_GETSEL(x)  (((x) >> TXDB_V_SEL) & TXDB_M_SEL)
#define CLKCSR_IMP      (CSR_IE)                        /* real-time clock */
#define CLKCSR_RW       (CSR_IE)
#define CLK_DELAY       5000                            /* 100 Hz */
#define TMXR_MULT       1                               /* 100 Hz */

extern int32 int_req[IPL_HLVL];
extern int32 hlt_pin;
extern int32 sim_switches;
extern jmp_buf save_env;
extern int32 p1;

int32 tti_csr = 0;                                      /* control/status */
int32 tto_csr = 0;                                      /* control/status */
int32 clk_csr = 0;                                      /* control/status */
int32 clk_tps = 100;                                    /* ticks/second */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;                /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* pgm timer poll */

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
void txdb_func (int32 data);

extern int32 sysd_hlt_enb (void);
extern int32 con_halt (int32 code, int32 cc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

DIB tti_dib = { 0, 0, NULL, NULL, 1, IVCL (TTI), SCB_TTI, { NULL } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), 0 };

REG tti_reg[] = {
    { HRDATA (BUF, tti_unit.buf, 16) },
    { HRDATA (CSR, tti_csr, 16) },
    { FLDATA (INT, int_req[IPL_TTI], INT_V_TTI) },
    { FLDATA (DONE, tti_csr, CSR_V_DONE) },
    { FLDATA (IE, tti_csr, CSR_V_IE) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, 0
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = { 0, 0, NULL, NULL, 1, IVCL (TTO), SCB_TTO, { NULL } };

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { HRDATA (BUF, tto_unit.buf, 8) },
    { HRDATA (CSR, tto_csr, 16) },
    { FLDATA (INT, int_req[IPL_TTO], INT_V_TTO) },
    { FLDATA (DONE, tto_csr, CSR_V_DONE) },
    { FLDATA (IE, tto_csr, CSR_V_IE) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,     NULL, &show_vec },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, 0
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

DIB clk_dib = { 0, 0, NULL, NULL, 1, IVCL (CLK), SCB_INTTIM, { NULL } };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), CLK_DELAY };

REG clk_reg[] = {
    { HRDATA (CSR, clk_csr, 16) },
    { FLDATA (INT, int_req[IPL_CLK], INT_V_CLK) },
    { FLDATA (IE, clk_csr, CSR_V_IE) },
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 8), REG_NZ + PV_LEFT },
    { NULL }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, 0
    };

/* Clock and terminal MxPR routines

   iccs_rd/wr   interval timer
   rxcs_rd/wr   input control/status
   rxdb_rd      input buffer
   txcs_rd/wr   output control/status
   txdb_wr      output buffer
*/

int32 iccs_rd (void)
{
return (clk_csr & CLKCSR_IMP);
}

int32 rxcs_rd (void)
{
return (tti_csr & TTICSR_IMP);
}

int32 rxdb_rd (void)
{
int32 t = tti_unit.buf;                                 /* char + error */

tti_csr = tti_csr & ~CSR_DONE;                          /* clr done */
tti_unit.buf = tti_unit.buf & 0377;                     /* clr errors */
CLR_INT (TTI);
return t;
}

int32 txcs_rd (void)
{
return (tto_csr & TTOCSR_IMP);
}

void iccs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    CLR_INT (CLK);
clk_csr = (clk_csr & ~CLKCSR_RW) | (data & CLKCSR_RW);
return;
}

void rxcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    CLR_INT (TTI);
else if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    SET_INT (TTI);
tti_csr = (tti_csr & ~TTICSR_RW) | (data & TTICSR_RW);
return;
}

void txcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    CLR_INT (TTO);
else if ((tto_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    SET_INT (TTO);
tto_csr = (tto_csr & ~TTOCSR_RW) | (data & TTOCSR_RW);
return;
}

void txdb_wr (int32 data)
{
if (data & TXDB_SEL) {                                  /* internal function? */
    txdb_func (data);
    return;
    }
tto_unit.buf = data & 0377;
tto_csr = tto_csr & ~CSR_DONE;
CLR_INT (TTO);
sim_activate (&tto_unit, tto_unit.wait);
return;
}

void txdb_func (int32 data)
{
int32 sel = TXDB_GETSEL (data);                         /* get selection */

if (sel == TXDB_MISC) {                                 /* misc function? */
    switch (data & MISC_MASK) {                         /* case on function */

    case MISC_SWDN:
        ABORT (STOP_SWDN);
        break;

    case MISC_BOOT:
        con_halt (0, 0);                                /* set up reboot */
        break;
        }
    }
}

/* Terminal input routines

   tti_svc      process event (character ready)
   tti_reset    process reset
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_activate (uptr, KBD_WAIT (uptr->wait, tmr_poll));   /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK) {                                   /* break? */
    if (sysd_hlt_enb ())                                /* if enabled, halt */
        hlt_pin = 1;
    tti_unit.buf = TTIBUF_ERR | TTIBUF_FRM | TTIBUF_RBR;
    }
else tti_unit.buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    SET_INT (TTI);
return SCPE_OK;
}

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;
tti_csr = 0;
CLR_INT (TTI);
sim_activate_abs (&tti_unit, KBD_WAIT (tti_unit.wait, tmr_poll));
return SCPE_OK;
}

/* Terminal output routines

   tto_svc      process event (character typed)
   tto_reset    process reset
*/

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = sim_tt_outcvt (tto_unit.buf, TT_GET_MODE (uptr->flags));
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* retry */
        return ((r == SCPE_STALL)? SCPE_OK: r);         /* !stall? report */
        }
    }
tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE)
    SET_INT (TTO);
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
tto_csr = CSR_DONE;
CLR_INT (TTO);
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Clock routines

   clk_svc      process event (clock tick)
   clk_reset    process reset
*/

t_stat clk_svc (UNIT *uptr)
{
int32 t;

if (clk_csr & CSR_IE)
    SET_INT (CLK);
t = sim_rtcn_calb (clk_tps, TMR_CLK);                   /* calibrate clock */
sim_activate (&clk_unit, t);                            /* reactivate unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
return SCPE_OK;
}

/* Clock coscheduling routine */

int32 clk_cosched (int32 wait)
{
int32 t;

t = sim_is_active (&clk_unit);
return (t? t - 1: wait);
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
int32 t;

clk_csr = 0;
CLR_INT (CLK);
t = sim_rtcn_init (clk_unit.wait, TMR_CLK);             /* init timer */
sim_activate_abs (&clk_unit, t);                        /* activate unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
return SCPE_OK;
}

