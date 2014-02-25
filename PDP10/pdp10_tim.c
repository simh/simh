/* pdp10_tim.c: PDP-10 tim subsystem simulator

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

   tim          timer subsystem

   18-Apr-12    RMS     Removed absolute scheduling on reset
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
#include <math.h>
#include <time.h>

/* The KS timer works off a 4.100 MHz (243.9024 nsec) oscillator that
 * is independent of all other system timing.
 *
 * Two pieces of timekeeping hardware are exposed to the OS.
 *  o The interval timer, which can interrupt at a programmed interval.
 *  o The timebase, which records time (71 bits).
 *
 * The clock is architecturally readable in units of 243.9024 nsec via
 * the timebase.  The implementation is somewhat different.
 *
 * The instructions that update the clocks specify time in these units.
 *
 * However, both timekeepers are incremented by the microcode when
 * a 12 bit counter overflows; e.g. at a period of 999.0244 usec.
 * Thus, the granularity of timer interrupts is approximately 1 msec.
 *
 * The OS programs the interval timer to interrupt as though the
 * the 12 least significant bits mattered.  Thus, for a (roughly)
 * 1 msec interval, it would program 1 * 4096 into the interval timer.
 * The sign bit is not used, so 35-12 = 23 bits for the maximum interval,
 * which is 139.674 minutes.  If any of the least significant bits
 * are non-zero, the interval is extended by 1 * 4096 counts.
 *
 * The timer merely sets the INTERVAL DONE flag in the APR flags.
 * Whether that actually causes an interrupt is controlled by the
 * APR interrupt enable for the flag and by the  PI system.
 * 
 * The flag is readable as an APR condition by RDAPR, and CONSO/Z APR,.
 * The flag is cleared by WRAPR 1b22!1b30 (clear, count done).
 *
 * The timebase is maintained with the 12 LSB zero in a workspace
 * register.  When read by the OS, the actual value of the 10 MSB of 
 * the hardware counter is inserted into those bits, providing increased
 * resolution.  Although the system reference manual says otherwise, the
 * two LSB of the counter are read as zero by the microcode (DPM2), so
 * bits <70:71> of the timebase are also read as zero by software.
 *
 * When the OS sets the timebase, the 12 LSB that it supplies are ignored.
 *
 * The timebase is typically used for accurate time of day and CPU runtime
 * accounting.  The simulator adjusts the equivalent of the 12-bit counter,
 * so CPU time will reflect simulator wall clock, not simulated machine cycles.
 * Since time of day must be accurate, this may result in the OS reporting
 * CPU times that are unrealistically faster - or slower - than on the
 * real hardware.
 *
 * This module also implements the TCU, a battery backed-up TOY clock
 * that was supported by TOPS-10, but not sold by DEC.
 */

/* Invariants */

#define TIM_HW_FREQ     4100000                         /* 4.1Mhz */
#define TIM_HWRE_MASK   07777                           /* Timer field of timebase */
#define TIM_BASE_RAZ    03                              /* Timer bits read as zero by ucode */
#define UNIT_V_Y2K      (UNIT_V_UF + 0)                 /* Y2K compliant OS */
#define UNIT_Y2K        (1u << UNIT_V_Y2K)

#define TIM_TMXR_FREQ  60                               /* Target frequency (HZ) for tmxr polls */

 /* Estimate of simulator instructions/sec for initialization and fixed timing.
  * This came from prior magic constant of 8000 at 60 tics/sec.
  * The machine was marketed as ~ 300KIPs, which would imply 3 usec/instr.
  * So 8,000 instructions should take ~24 msec.  This would indicate that
  * the simulator from which this came was ~1.4 x the speed of the real
  * hardware.  Current milage will vary.
  */
#define TIM_WAIT_IPS   480000

/* Clock mode TOPS-10/ITS */

#define TIM_TPS_T10     60                              /* Initial frequency guess for TOPS-10 (close, not exact) */
#define TIM_ITS_QUANT   (TIM_HW_FREQ / TIM_TPS_T10)     /* ITS PC sampling and user runtime interval */

/* Clock mode TOPS-20/KLAD */

#define TIM_TPS_T20     1000                            /* Initial estimate for TOPS-20 - 1msec seems fast? */

/* Probability function for TOPS-20 idlelock */

#define PROB(x)         (((rand() * 100) / RAND_MAX) >= (x))

static d10 tim_base[2] = { 0, 0 };                      /* 71b timebase */
static d10 tim_interval = 0;                            /* value programmed into the clock */
static d10 tim_period = 0;                              /* period in HW ticks adjusted for non-zero LSBs */
static d10 tim_new_period = 0;                          /* period for the next interval */
static int32 tim_mult;                                  /* Multiple of interval timer period at which tmxr is polled */

d10 quant = 0;                                          /* ITS quantum */
static int32 tim_t20_prob = 33;                         /* TOPS-20 prob */

/* Exported variables - initialized by set CPU model and reset */

int32 clk_tps;                                          /* Interval clock ticks/sec */
int32 tmr_poll;                                         /* SimH instructions/clock service */
int32 tmxr_poll;                                        /* SimH instructions/term mux poll */

extern int32 apr_flg, pi_act;
extern UNIT cpu_unit;
extern d10 pcst;
extern a10 pager_PC;
extern int32 t20_idlelock;

DEVICE tim_dev;
static t_stat tcu_rd (int32 *data, int32 PA, int32 access);
static t_stat tim_svc (UNIT *uptr);
static t_stat tim_reset (DEVICE *dptr);
static t_bool update_interval (d10 new_interval);
static void tim_incr_base (d10 *base, d10 incr);

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

static UNIT tim_unit = { UDATA (&tim_svc, UNIT_IDLE, 0), 0 };

static REG tim_reg[] = {
    { BRDATA (TIMEBASE, tim_base, 8, 36, 2) },
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

static MTAB tim_mod[] = {
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

/* Timebase - the timer is always running at less than hardware frequency,
 * need to interpolate the value by calculating how much of the current
 * clock tick has elapsed, and what that equates to in sysfreq units.
 */

t_bool rdtim (a10 ea, int32 prv)
{
double fract;                     /* Fraction of current interval completed */
d10 tempbase[2];                  /* Local copy of tempbase to interpolate */
int32 used;                       /* Used part of curr intv, in hw ticks   */
d10 incr;                         /* Interpolated increment for timebase   */

tempbase[0] = tim_base[0];                              /* copy time base */
tempbase[1] = tim_base[1];

used = tmr_poll - (sim_activate_time (&tim_unit) - 1);
fract = ((double)used) / ((double)tmr_poll);

/*
 * incr is approximate number of HW ticks to add to the timebase
 * value returned.  This does NOT update the timebase.
 */
incr = (d10)(fract * (double)tim_period);
tim_incr_base (tempbase, incr);

/* Although the two LSB of the counter contribute carry to the
 * value, they are read as zero by microcode, and thus cleared here.
 *
 * The reason that these bits are forced to zero in the hardware is
 * that the counter is in a different clock domain from the microcode.
 * To make the domain crossing, the microcode reads the counter
 * until two consecutive values match.
 * 
 * Since the microcode cycle time is 300 nsec, the LSBs of the 
 * counter run too fast (244 nsec) for the strategy to work.  
 * Ignoring the two LSB ensures that the value can't change any 
 * faster than ~976 nsec, which guarantees a stable value can be
 * obtained in at most three attempts.
 */
tempbase[1] &= ~((d10) TIM_BASE_RAZ);

/* If the destination is arranged so that the first word is OK, but
 * the second pagefaults, the value will be half-written.  As we
 * expect the PFH to restart the instruction, the both halves will
 * be written the second time.  We could read and write back both
 * halves to avoid this, but the hardware doesn't seem to either.
 */
Write (ea, tempbase[0], prv);
Write (INCA(ea), tempbase[1], prv);
return FALSE;
}

t_bool wrtim (a10 ea, int32 prv)
{
tim_base[0] = Read (ea, prv);
tim_base[1] = CLRS (Read (INCA (ea), prv) & ~((d10) TIM_HWRE_MASK));
return FALSE;
}

t_bool rdint (a10 ea, int32 prv)
{
Write (ea, tim_interval, prv);
return FALSE;
}

/* write a new interval timer period (in timer ticks).
 * This does not clear the harware counter, so the first
 * completion can come up to ~1 msc later than the new
 * period.
 */

t_bool wrint (a10 ea, int32 prv)
{
tim_interval = CLRS (Read (ea, prv));
return update_interval (tim_interval);
}

static t_bool update_interval (d10 new_interval)
{
/*
 * The value provided is in hardware clicks. For a frequency of 4.1 
 * MHz, that means that dividing by 4096 (shifting 12 to the right) we get
 * the aproximate value in millisenconds. If any of rhe rightmost bits is
 * one, we add one unit (4096 ticks ). Reference:
 * AA-H391A-TK_DECsystem-10_DECSYSTEM-20_Processor_Reference_Jun1982.pdf
 * (page 4-37)
 */
tim_new_period = new_interval & ~TIM_HWRE_MASK;
if (new_interval & TIM_HWRE_MASK) tim_new_period += 010000;
    
/* clk_tps is the new number of clocks ticks per second */
clk_tps = (int32) ceil((double)TIM_HW_FREQ /(double)tim_new_period);
   
/* tmxr is polled every tim_mult clks.  Compute the divisor matching the target. */
tim_mult = (clk_tps <= TIM_TMXR_FREQ) ? 1 : (clk_tps / TIM_TMXR_FREQ) ;
   
/* Estimate instructions/tick for fixed timing - just for KLAD */
tim_unit.wait = TIM_WAIT_IPS / clk_tps;
tmxr_poll = tim_unit.wait * tim_mult;

/* The next tim_svc will update the activation time.
 * 
 */
return FALSE;
}

/* Timer service - the timer is only serviced when the interval
 * programmed in tim_period by wrint expires.  If the interval
 * changes, the timebase update is based on the previous interval.
 * The interval calibration is based on what the new interval will be.
 */

static t_stat tim_svc (UNIT *uptr)
{
if (cpu_unit.flags & UNIT_KLAD)                         /* diags? */
    tmr_poll = uptr->wait;                              /* fixed clock */
else tmr_poll = sim_rtc_calb (clk_tps);                 /* else calibrate */
    
sim_activate (uptr, tmr_poll);                          /* reactivate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
tim_incr_base (tim_base, tim_period);                   /* incr time base based on period of expired interval */
tim_period = tim_new_period;                            /* If interval has changed, update period */
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

static void tim_incr_base (d10 *base, d10 incr)
{
base[1] = base[1] + incr;                               /* add on incr */
base[0] = base[0] + (base[1] >> 35);                    /* carry to high */
base[0] = base[0] & DMASK;                              /* mask high */
base[1] = base[1] & MMASK;                              /* mask low */
return;
}

/* Timer reset */

static t_stat tim_reset (DEVICE *dptr)
{
sim_register_clock_unit (&tim_unit);                    /* declare clock unit */

tim_base[0] = tim_base[1] = 0;                          /* clear timebase (HW does) */
/* HW does not initialize the interval timer, so the rate at which the timer flag
 * sets is random.  No sensible user would enable interrupts or check the flag without
 * setting an interval.  The timebase is intialized to zero by microcode intialization.
 * It increments based on the overflow, so it would be reasonable for a user to just
 * read it twice and subtract the values to determine elapsed time.
 *
 * Simply to keep the simulator overhead down until the interval timer is initialized
 * by the OS or diagnostic, we will set the internal interval to ~17 msec here.
 * This allows the service routine to increment the timebase, and gives RDTIME an
 * baseline for its interpolation.
 */
tim_interval = 0;
clk_tps = 60;
update_interval(17*4096);

apr_flg = apr_flg & ~APRF_TIM;                          /* clear interrupt */

tmr_poll = sim_rtc_init (tim_unit.wait);                /* init timer */
sim_activate (&tim_unit, tmr_poll);                     /* activate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
return SCPE_OK;
}

/* Set timer parameters from CPU model */

t_stat tim_set_mod (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val & (UNIT_T20|UNIT_KLAD)) {
    clk_tps = TIM_TPS_T20;
    update_interval(((d10)(1000*4096))/clk_tps); 
    tmr_poll = tim_unit.wait;
    uptr->flags = uptr->flags | UNIT_Y2K;
    }
else {
    clk_tps = TIM_TPS_T10;
    update_interval (((d10)(1000*4096))/clk_tps);
    tmr_poll = tim_unit.wait;
    if (Q_ITS)
        uptr->flags = uptr->flags | UNIT_Y2K;
    else uptr->flags = uptr->flags & ~UNIT_Y2K;
    }
return SCPE_OK;
}

/* Time of year clock
 *
 * The hardware clock was never sold by DEC, but support for it exists
 * in TOPS-10.  Code was also available for RSX20F to read and report the
 * to the OS via its20F's SETSPD task.  This implements only the read functions.
 *
 * The manufacturer's manual can be found at
 * http://bitsavers.trailing-edge.com/pdf/digitalPathways/tcu-150.pdf
 */

static t_stat tcu_rd (int32 *data, int32 PA, int32 access)
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
