/* pdp10_tim.c: PDP-10 tim subsystem simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   tim          timer subsystem

   18-Jun-07    RMS     Added UNIT_IDLE flag
   03-Nov-06    RMS     Rewritten to support idling
   29-Oct-06    RMS     Added clock coscheduling function
   02-Feb-04    RMS     Exported variables needed by Ethernet simulator
   29-Jan-02    RMS     New data structures
   06-Jan-02    RMS     Added enable/disable support
   02-Dec-01    RMS     Fixed bug in ITS PC sampling (found by Dave Conroy)
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   17-Jul-01    RMS     Moved function prototype
   04-Jul-01    RMS     Added DZ11 support
*/

#include "pdp10_defs.h"
#include <time.h>

/* Invariants */

#define TIM_HW_FREQ     4100000                         /* 4.1Mhz */
#define TIM_HWRE_MASK   07777
#define UNIT_V_Y2K      (UNIT_V_UF + 0)                 /* Y2K compliant OS */
#define UNIT_Y2K        (1u << UNIT_V_Y2K)

/* Clock mode TOPS-10/ITS */

#define TIM_TPS_T10     60
#define TIM_WAIT_T10    8000
#define TIM_MULT_T10    1
#define TIM_ITS_QUANT   (TIM_HW_FREQ / TIM_TPS_T10)

/* Clock mode TOPS-20/KLAD */

#define TIM_TPS_T20     1001
#define TIM_WAIT_T20    500
#define TIM_MULT_T20    16

/* Probability function for TOPS-20 idlelock */

#define PROB(x)         (((rand() * 100) / RAND_MAX) >= (x))

d10 tim_base[2] = { 0, 0 };                             /* 71b timebase */
d10 tim_ttg = 0;                                        /* time to go */
d10 tim_period = 0;                                     /* period */
d10 quant = 0;                                          /* ITS quantum */
int32 tim_mult = TIM_MULT_T10;                          /* tmxr poll mult */
int32 tim_t20_prob = 33;                                /* TOPS-20 prob */

/* Exported variables */

int32 clk_tps = TIM_TPS_T10;                            /* clock ticks/sec */
int32 tmr_poll = TIM_WAIT_T10;                          /* clock poll */
int32 tmxr_poll = TIM_WAIT_T10 * TIM_MULT_T10;          /* term mux poll */

extern int32 apr_flg, pi_act;
extern UNIT cpu_unit;
extern d10 pcst;
extern a10 pager_PC;
extern int32 t20_idlelock;

DEVICE tim_dev;
t_stat tcu_rd (int32 *data, int32 PA, int32 access);
t_stat tim_svc (UNIT *uptr);
t_stat tim_reset (DEVICE *dptr);
void tim_incr_base (d10 *base, d10 incr);

extern d10 Read (a10 ea, int32 prv);
extern d10 ReadM (a10 ea, int32 prv);
extern void Write (a10 ea, d10 val, int32 prv);
extern void WriteP (a10 ea, d10 val);
extern int32 pi_eval (void);
extern t_stat wr_nop (int32 data, int32 PA, int32 access);

/* TIM data structures

   tim_dev      TIM device descriptor
   tim_unit     TIM unit descriptor
   tim_reg      TIM register list
*/

DIB tcu_dib = { IOBA_TCU, IOLN_TCU, &tcu_rd, &wr_nop, 0 };

UNIT tim_unit = { UDATA (&tim_svc, UNIT_IDLE, 0), TIM_WAIT_T10 };

REG tim_reg[] = {
    { BRDATA (TIMEBASE, tim_base, 8, 36, 2) },
    { ORDATA (TTG, tim_ttg, 36) },
    { ORDATA (PERIOD, tim_period, 36) },
    { ORDATA (QUANT, quant, 36) },
    { DRDATA (TIME, tim_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (PROB, tim_t20_prob, 6), REG_NZ + PV_LEFT + REG_HIDDEN },
    { DRDATA (POLL, tmr_poll, 32), REG_HRO + PV_LEFT },
    { DRDATA (MUXPOLL, tmxr_poll, 32), REG_HRO + PV_LEFT },
    { DRDATA (MULT, tim_mult, 6), REG_HRO + PV_LEFT },
    { DRDATA (TPS, clk_tps, 12), REG_HRO + PV_LEFT },
    { NULL }
    };

MTAB tim_mod[] = {
    { UNIT_Y2K, 0, "non Y2K OS", "NOY2K", NULL },
    { UNIT_Y2K, UNIT_Y2K, "Y2K OS", "Y2K", NULL },
    { MTAB_XTD|MTAB_VDV, 000, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { 0 }
    };

DEVICE tim_dev = {
    "TIM", &tim_unit, tim_reg, tim_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &tim_reset,
    NULL, NULL, NULL,
    &tcu_dib, DEV_UBUS
    };

/* Timer instructions */

/* Timer - if the timer is running at less than hardware frequency,
   need to interpolate the value by calculating how much of the current
   clock tick has elapsed, and what that equates to in msec. */

t_bool rdtim (a10 ea, int32 prv)
{
d10 tempbase[2];

ReadM (INCA (ea), prv);                                 /* check 2nd word */
tempbase[0] = tim_base[0];                              /* copy time base */
tempbase[1] = tim_base[1];
if (tim_mult != TIM_MULT_T20) {                         /* interpolate? */
    int32 used;
    d10 incr;
    used = tmr_poll - (sim_is_active (&tim_unit) - 1);
    incr = (d10) (((double) used * TIM_HW_FREQ) /
        ((double) tmr_poll * (double) clk_tps));
    tim_incr_base (tempbase, incr);
    }
tempbase[0] = tempbase[0] & ~((d10) TIM_HWRE_MASK);     /* clear low 12b */
Write (ea, tempbase[0], prv);
Write (INCA(ea), tempbase[1], prv);
return FALSE;
}

t_bool wrtim (a10 ea, int32 prv)
{
tim_base[0] = Read (ea, prv);
tim_base[1] = CLRS (Read (INCA (ea), prv));
return FALSE;
}

t_bool rdint (a10 ea, int32 prv)
{
Write (ea, tim_period, prv);
return FALSE;
}

t_bool wrint (a10 ea, int32 prv)
{
tim_period = Read (ea, prv);
tim_ttg = tim_period;
return FALSE;
}

/* Timer service - the timer is only serviced when the 'ttg' register
   has reached 0 based on the expected frequency of clock interrupts. */

t_stat tim_svc (UNIT *uptr)
{
if (cpu_unit.flags & UNIT_KLAD)                         /* diags? */
    tmr_poll = uptr->wait;                              /* fixed clock */
else tmr_poll = sim_rtc_calb (clk_tps);                 /* else calibrate */
sim_activate (uptr, tmr_poll);                          /* reactivate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
tim_incr_base (tim_base, tim_period);                   /* incr time base */
tim_ttg = tim_period;                                   /* reload */
apr_flg = apr_flg | APRF_TIM;                           /* request interrupt */
if (Q_ITS) {                                            /* ITS? */
    if (pi_act == 0)
	    quant = (quant + TIM_ITS_QUANT) & DMASK;
    if (TSTS (pcst)) {                                  /* PC sampling? */
        WriteP ((a10) pcst & AMASK, pager_PC);          /* store sample */
        pcst = AOB (pcst);                              /* add 1,,1 */
        }
    }                                                   /* end ITS */
else if (t20_idlelock && PROB (100 - tim_t20_prob))
    t20_idlelock = 0;
return SCPE_OK;
}

/* Clock coscheduling routine */

int32 clk_cosched (int32 wait)
{
int32 t;

if (tim_mult == TIM_MULT_T20)
    return wait;
t = sim_is_active (&tim_unit);
return (t? t - 1: wait);
}

void tim_incr_base (d10 *base, d10 incr)
{
base[1] = base[1] + incr;                               /* add on incr */
base[0] = base[0] + (base[1] >> 35);                    /* carry to high */
base[0] = base[0] & DMASK;                              /* mask high */
base[1] = base[1] & MMASK;                              /* mask low */
return;
}

/* Timer reset */

t_stat tim_reset (DEVICE *dptr)
{
tim_period = 0;                                         /* clear timer */
tim_ttg = 0;
apr_flg = apr_flg & ~APRF_TIM;                          /* clear interrupt */
tmr_poll = sim_rtc_init (tim_unit.wait);                /* init timer */
sim_activate_abs (&tim_unit, tmr_poll);                 /* activate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
return SCPE_OK;
}

/* Set timer parameters from CPU model */

t_stat tim_set_mod (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val & (UNIT_T20|UNIT_KLAD)) {
    clk_tps = TIM_TPS_T20;
    uptr->wait = TIM_WAIT_T20;
    tmr_poll = TIM_WAIT_T20;
    tim_mult = TIM_MULT_T20;
    uptr->flags = uptr->flags | UNIT_Y2K;
    }
else {
    clk_tps = TIM_TPS_T10;
    uptr->wait = TIM_WAIT_T10;
    tmr_poll = TIM_WAIT_T10;
    tim_mult = TIM_MULT_T10;
    if (Q_ITS)
        uptr->flags = uptr->flags | UNIT_Y2K;
    else uptr->flags = uptr->flags & ~UNIT_Y2K;
    }
tmxr_poll = tmr_poll * tim_mult;
return SCPE_OK;
}

/* Time of year clock */

t_stat tcu_rd (int32 *data, int32 PA, int32 access)
{
time_t curtim;
struct tm *tptr;

curtim = time (NULL);                                   /* get time */
tptr = localtime (&curtim);                             /* decompose */
if (tptr == NULL)
    return SCPE_NXM; 
if ((tptr->tm_year > 99) && !(tim_unit.flags & UNIT_Y2K))
    tptr->tm_year = 99;                                 /* Y2K prob? */

switch ((PA >> 1) & 03) {                               /* decode PA<3:1> */

    case 0:                                             /* year/month/day */
        *data = (((tptr->tm_year) & 0177) << 9) |
                (((tptr->tm_mon + 1) & 017) << 5) |
                ((tptr->tm_mday) & 037);
        return SCPE_OK;

    case 1:                                             /* hour/minute */
        *data = (((tptr->tm_hour) & 037) << 8) |
                ((tptr->tm_min) & 077);
        return SCPE_OK;

    case 2:                                             /* second */
        *data = (tptr->tm_sec) & 077;
        return SCPE_OK;

    case 3:                                             /* status */
        *data = CSR_DONE;
        return SCPE_OK;
        }

return SCPE_NXM;                                        /* can't get here */
}
