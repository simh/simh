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
#include "sim_tmxr.h"
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
#define  MISC_NOOP0      0x0                            /* no operation */
#define  MISC_NOOP1      0x1                            /* no operation */
#define  MISC_BOOT       0x2                            /* reboot */
#define  MISC_CLWS       0x3                            /* clear warm start */
#define  MISC_CLCS       0x4                            /* clear cold start */
#define  MISC_SWDN       0x5                            /* software done */
#define  MISC_LEDS0      0x8                            /* LEDs 000 (all on) */
#define  MISC_LEDS1      0x9                            /* LEDs 001 (on,  on,  off) */
#define  MISC_LEDS2      0xA                            /* LEDs 010 (on, off,   on)*/
#define  MISC_LEDS3      0xB                            /* LEDs 011 (on, off,  off)*/
#define  MISC_LEDS4      0xC                            /* LEDs 100 (off, on,   on)*/
#define  MISC_LEDS5      0xD                            /* LEDs 101 (off, on,  off)*/
#define  MISC_LEDS6      0xE                            /* LEDs 110 (off, off,  on)*/
#define  MISC_LEDS7      0xF                            /* LEDs 111 (all off)*/
#define TXDB_SEL        (TXDB_M_SEL << TXDB_V_SEL)      /* non-terminal */
#define TXDB_GETSEL(x)  (((x) >> TXDB_V_SEL) & TXDB_M_SEL)
#define CLKCSR_IMP      (CSR_IE)                        /* real-time clock */
#define CLKCSR_RW       (CSR_IE)
#define CLK_DELAY       5000                            /* 100 Hz */
#define TMXR_MULT       1                               /* 100 Hz */

extern int32 int_req[IPL_HLVL];
extern int32 hlt_pin;
extern jmp_buf save_env;
extern int32 p1;

int32 tti_csr = 0;                                      /* control/status */
uint32 tti_buftime;                                     /* time input character arrived */
int32 tto_csr = 0;                                      /* control/status */
int32 tto_leds = 0;                                     /* processor board LEDs */
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
char *tti_description (DEVICE *dptr);
char *tto_description (DEVICE *dptr);
char *clk_description (DEVICE *dptr);
t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
void txdb_func (int32 data);

extern int32 sysd_hlt_enb (void);
extern int32 con_halt (int32 code, int32 cc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

DIB tti_dib = { 0, 0, NULL, NULL, 1, IVCL (TTI), SCB_TTI, { NULL } };

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), SERIAL_IN_WAIT };

REG tti_reg[] = {
    { HRDATAD (BUF,     tti_unit.buf,         16, "last data item processed") },
    { HRDATAD (CSR,          tti_csr,         16, "control/status register") },
    { FLDATAD (INT, int_req[IPL_TTI],  INT_V_TTI, "interrupt pending flag") },
    { FLDATAD (ERR,          tti_csr,  CSR_V_ERR, "error flag (CSR<15>)") },
    { FLDATAD (DONE,         tti_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,           tti_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,     tti_unit.pos,   T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME,   tti_unit.wait,         24, "input polling interval"), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE,  TT_MODE_7B, "7b", "7B",     NULL, NULL,      NULL, "Set 7 bit mode" },
    { TT_MODE,  TT_MODE_8B, "8b", "8B",     NULL, NULL,      NULL, "Set 8 bit mode" },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL, NULL, &show_vec, NULL, "Display interrupt vector" },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, 0, 0, NULL, NULL, NULL, &tti_help, NULL, NULL, 
    &tti_description
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = { 0, 0, NULL, NULL, 1, IVCL (TTO), SCB_TTO, { NULL } };

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { HRDATAD (BUF,     tto_unit.buf,          8, "last data item processed") },
    { HRDATAD (CSR,          tto_csr,         16, "control/status register") },
    { FLDATAD (INT, int_req[IPL_TTO],  INT_V_TTO, "interrupt pending flag") },
    { FLDATAD (ERR,          tto_csr,  CSR_V_ERR, "error flag (CSR<15>)") },
    { FLDATAD (DONE,         tto_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,           tto_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,     tto_unit.pos,   T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME,   tto_unit.wait,         24, "time from I/O initiation to interrupt"), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE,  TT_MODE_7B, "7b", "7B",     NULL, NULL,      NULL, "Set 7 bit mode" },
    { TT_MODE,  TT_MODE_8B, "8b", "8B",     NULL, NULL,      NULL, "Set 8 bit mode" },
    { TT_MODE,  TT_MODE_7P, "7p", "7P",     NULL, NULL,      NULL, "Set 7 bit mode (suppress non printing)" },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL, NULL, &show_vec, NULL, "Display interrupt vector" },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, 0, 0, NULL, NULL, NULL, &tto_help, NULL, NULL, 
    &tto_description
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

DIB clk_dib = { 0, 0, NULL, NULL, 1, IVCL (CLK), SCB_INTTIM, { NULL } };

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), CLK_DELAY };

REG clk_reg[] = {
    { HRDATAD (CSR,          clk_csr,        16, "control/status register") },
    { FLDATAD (INT, int_req[IPL_CLK], INT_V_CLK, "interrupt pending flag") },
    { FLDATAD (IE,           clk_csr,  CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (TIME,   clk_unit.wait,        24, "initial poll interval"), REG_NZ + PV_LEFT },
    { DRDATAD (POLL,        tmr_poll,        24, "calibrated poll interval"), REG_NZ + PV_LEFT + REG_HRO },
    { DRDATAD (TPS,          clk_tps,         8, "ticks per second (100)"), REG_NZ + PV_LEFT },
#if defined (SIM_ASYNCH_IO)
    { DRDATAD (ASYNCH,            sim_asynch_enabled,         1, "asynch I/O enabled flag"), PV_LEFT },
    { DRDATAD (LATENCY,           sim_asynch_latency,        32, "desired asynch interrupt latency"), PV_LEFT },
    { DRDATAD (INST_LATENCY, sim_asynch_inst_latency,        32, "calibrated instruction latency"), PV_LEFT },
#endif
    { NULL }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &clk_description
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

if (tti_csr & CSR_DONE) {                               /* Input pending ? */
    tti_csr = tti_csr & ~CSR_DONE;                      /* clr done */
    tti_unit.buf = tti_unit.buf & 0377;                 /* clr errors */
    CLR_INT (TTI);
    sim_activate_abs (&tti_unit, tti_unit.wait);        /* check soon for more input */
    }
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
    case MISC_LEDS0: case MISC_LEDS1: case MISC_LEDS2: case MISC_LEDS3:
    case MISC_LEDS4: case MISC_LEDS5: case MISC_LEDS6: case MISC_LEDS7:
        tto_leds = 0x7 & (~((data & MISC_MASK)-MISC_LEDS0));
        sim_putchar ('.');
        sim_putchar ('0' + tto_leds);
        sim_putchar ('.');
        break;
        }
    }
else
    if (sel != 0)
        RSVD_OPND_FAULT;

}

t_stat cpu_show_leds (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "leds=%d(%s,%s,%s)", tto_leds, tto_leds&4 ? "ON" : "OFF", 
                                            tto_leds&2 ? "ON" : "OFF", 
                                            tto_leds&1 ? "ON" : "OFF");
return SCPE_OK;
}

/* Terminal input routines

   tti_svc      process event (character ready)
   tti_reset    process reset
*/

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule (uptr, KBD_WAIT (uptr->wait, tmr_poll));
                                                        /* continue poll */
if ((tti_csr & CSR_DONE) &&                             /* input still pending and < 500ms? */
    ((sim_os_msec () - tti_buftime) < 500))
     return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK) {                                   /* break? */
    if (sysd_hlt_enb ())                                /* if enabled, halt */
        hlt_pin = 1;
    tti_unit.buf = TTIBUF_ERR | TTIBUF_FRM | TTIBUF_RBR;
    }
else tti_unit.buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
tti_buftime = sim_os_msec ();
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    SET_INT (TTI);
return SCPE_OK;
}

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_unit.buf = 0;
tti_csr = 0;
CLR_INT (TTI);
sim_activate_abs (&tti_unit, KBD_WAIT (tti_unit.wait, tmr_poll));
return SCPE_OK;
}

t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Console Terminal Input (TTI)\n\n");
fprintf (st, "The terminal input (TTI) polls the console keyboard for input.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *tti_description (DEVICE *dptr)
{
return "console terminal input";
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

t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Console Terminal Output (TTO)\n\n");
fprintf (st, "The terminal output (TTO) writes to the simulator console.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *tto_description (DEVICE *dptr)
{
return "console terminal output";
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
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
AIO_SET_INTERRUPT_LATENCY(tmr_poll*clk_tps);            /* set interrrupt latency */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
int32 t;

sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
clk_csr = 0;
CLR_INT (CLK);
t = sim_rtcn_init (clk_unit.wait, TMR_CLK);             /* init timer */
sim_activate_abs (&clk_unit, t);                        /* activate unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
return SCPE_OK;
}

char *clk_description (DEVICE *dptr)
{
return "100hz clock tick";
}

