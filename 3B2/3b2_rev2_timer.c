/* 3b2_rev2_timer.c: 8253 Interval Timer

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
 * 8253 Timer.
 *
 * The 8253 Timer IC has three interval timers, which we treat here as
 * three units.
 *
 * Note that this simulation is very specific to the 3B2, and not
 * usable as a general purpose 8253 simulator.
 *
 */

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_defs.h"
#include "3b2_timer.h"

#define SET_INT do {            \
        CPU_SET_INT(INT_CLOCK); \
        SET_CSR(CSRCLK);        \
    } while (0)

#define CLR_INT do {            \
        CPU_CLR_INT(INT_CLOCK); \
        CLR_CSR(CSRCLK);        \
    } while (0)

struct timer_ctr TIMERS[3];

int32 tmxr_poll = 16667;

/*
 * The three timers, (A, B, C) run at different
 * programmatially controlled frequencies, so each must be
 * handled through a different service routine.
 */

UNIT timer_unit[] = {
    { UDATA(&timer0_svc, 0, 0) },
    { UDATA(&timer1_svc, UNIT_IDLE, 0) },
    { UDATA(&timer2_svc, 0, 0) },
    { NULL }
};

UNIT *timer_clk_unit = &timer_unit[1];

REG timer_reg[] = {
    { HRDATAD(DIVA,  TIMERS[0].divider, 16, "Divider A") },
    { HRDATAD(STA,   TIMERS[0].mode,    8,  "Mode A")   },
    { HRDATAD(DIVB,  TIMERS[1].divider, 16, "Divider B") },
    { HRDATAD(STB,   TIMERS[1].mode,    8,  "Mode B")   },
    { HRDATAD(DIVC,  TIMERS[2].divider, 16, "Divider C") },
    { HRDATAD(STC,   TIMERS[2].mode,    8,  "Mode C")   },
    { NULL }
};

MTAB timer_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, NULL, "SHUTDOWN",
      &timer_set_shutdown, NULL, NULL, "Soft Power Shutdown" },
    { 0 }
};

DEVICE timer_dev = {
    "TIMER", timer_unit, timer_reg, timer_mod,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat timer_reset(DEVICE *dptr) {
    int32 i, t;

    memset(&TIMERS, 0, sizeof(struct timer_ctr) * 3);

    for (i = 0; i < 3; i++) {
        timer_unit[i].tmrnum = i;
        timer_unit[i].tmr = (void *)&TIMERS[i];
    }

    /* Timer 1 gate is always active */
    TIMERS[1].gate = 1;

    if (!sim_is_running) {
        t = sim_rtcn_init_unit(timer_clk_unit, TPS_CLK, TMR_CLK);
        sim_activate_after(timer_clk_unit, 1000000 / t);
    }

    return SCPE_OK;
}

static void timer_activate(uint8 ctrnum)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];

    switch (ctrnum) {
    case TIMER_SANITY:
        break;
    case TIMER_INTERVAL:
        if ((csr_data & CSRITIM) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] INTERVAL TIMER: Activating after %d ms\n",
                      R[NUM_PC], ctr->val);
            sim_activate_after_abs(&timer_unit[ctrnum], ctr->val);
            ctr->val--;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] INTERVAL TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
        break;
    case TIMER_BUS:
        break;
    default:
        break;
    }
}

t_stat timer_set_shutdown(UNIT *uptr, int32 val, CONST char* cptr, void* desc)
{
    struct timer_ctr *sanity = (struct timer_ctr *)timer_unit[0].tmr;

    sim_debug(EXECUTE_MSG, &timer_dev,
              "[%08x] Setting sanity timer to 0 for shutdown.\n", R[NUM_PC]);

    sanity->val = 0;

    CLR_INT;

    CPU_SET_INT(INT_SERR);
    CSRBIT(CSRTIMO, TRUE);

    return SCPE_OK;
}

void timer_enable(uint8 ctrnum) {
    timer_activate(ctrnum);
}

void timer_disable(uint8 ctrnum)
{
    sim_debug(EXECUTE_MSG, &timer_dev,
              "[%08x] Disabling timer %d\n",
              R[NUM_PC], ctrnum);
    sim_cancel(&timer_unit[ctrnum]);
}

/*
 * Sanity Timer
 */
t_stat timer0_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 time;

    ctr = (struct timer_ctr *)uptr->tmr;

    time = ctr->divider * TIMER_STP_US;

    if (time == 0) {
        time = TIMER_STP_US;
    }

    sim_activate_after_abs(uptr, time);

    return SCPE_OK;
}

/*
 * Interval Timer
 */
t_stat timer1_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 t;

    ctr = (struct timer_ctr *)uptr->tmr;

    if (ctr->enabled && !(csr_data & CSRITIM)) {
        /* Fire the IPL 15 clock interrupt */
        SET_INT;
    }

    t = sim_rtcn_calb(TPS_CLK, TMR_CLK);
    sim_activate_after_abs(uptr, 1000000/TPS_CLK);
    tmxr_poll = t;

    return SCPE_OK;
}

/*
 * Bus Timeout Timer
 */
t_stat timer2_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 time;

    ctr = (struct timer_ctr *)uptr->tmr;

    time = ctr->divider * TIMER_STP_US;

    if (time == 0) {
        time = TIMER_STP_US;
    }

    sim_activate_after_abs(uptr, time);

    return SCPE_OK;
}

uint32 timer_read(uint32 pa, size_t size)
{
    uint32 reg;
    uint16 ctr_val;
    uint8 ctrnum;
    struct timer_ctr *ctr;

    reg = pa - TIMERBASE;
    ctrnum = (reg >> 2) & 0x3;
    ctr = &TIMERS[ctrnum];

    switch (reg) {
    case TIMER_REG_DIVA:
    case TIMER_REG_DIVB:
    case TIMER_REG_DIVC:
        ctr_val = ctr->val;

        if (ctr_val != ctr->divider) {
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] >>> ctr_val = %04x, ctr->divider = %04x\n",
                      R[NUM_PC], ctr_val, ctr->divider);
        }

        switch (ctr->mode & CLK_RW) {
        case CLK_LSB:
            return ctr_val & 0xff;
        case CLK_MSB:
            return (ctr_val & 0xff00) >> 8;
        case CLK_LMB:
            if (ctr->lmb) {
                ctr->lmb = FALSE;
                return (ctr_val & 0xff00) >> 8;
            } else {
                ctr->lmb = TRUE;
                return ctr_val & 0xff;
            }
        default:
            return 0;
        }
        break;
    case TIMER_REG_CTRL:
        return ctr->mode;
    case TIMER_CLR_LATCH:
        /* Clearing the timer latch has a side-effect
           of also clearing pending interrupts */
        CLR_INT;
        return 0;
    default:
        /* Unhandled */
        sim_debug(READ_MSG, &timer_dev,
                  "[%08x] UNHANDLED TIMER READ. ADDR=%08x\n",
                  R[NUM_PC], pa);
        return 0;
    }
}

void handle_timer_write(uint8 ctrnum, uint32 val)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];
    switch(ctr->mode & 0x30) {
    case 0x10:
        ctr->divider &= 0xff00;
        ctr->divider |= val & 0xff;
        ctr->val = ctr->divider;
        ctr->enabled = TRUE;
        ctr->stime = sim_gtime();
        sim_cancel(timer_clk_unit);
        sim_activate_after_abs(timer_clk_unit, ctr->divider * TIMER_STP_US);
        break;
    case 0x20:
        ctr->divider &= 0x00ff;
        ctr->divider |= (val & 0xff) << 8;
        ctr->val = ctr->divider;
        ctr->enabled = TRUE;
        ctr->stime = sim_gtime();
        /* Kick the timer to get the new divider value */
        sim_cancel(timer_clk_unit);
        sim_activate_after_abs(timer_clk_unit, ctr->divider * TIMER_STP_US);
        break;
    case 0x30:
        if (ctr->lmb) {
            ctr->lmb = FALSE;
            ctr->divider = (uint16) ((ctr->divider & 0x00ff) | ((val & 0xff) << 8));
            ctr->val = ctr->divider;
            ctr->enabled = TRUE;
            ctr->stime = sim_gtime();
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] Write timer %d val LMB (MSB): %02x\n",
                      R[NUM_PC], ctrnum, val & 0xff);
            /* Kick the timer to get the new divider value */
            sim_cancel(timer_clk_unit);
            sim_activate_after_abs(timer_clk_unit, ctr->divider * TIMER_STP_US);
        } else {
            ctr->lmb = TRUE;
            ctr->divider = (ctr->divider & 0xff00) | (val & 0xff);
            ctr->val = ctr->divider;
        }
        break;
    default:
        break;

    }
}

void timer_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg, ctrnum;
    struct timer_ctr *ctr;

    reg = (uint8) (pa - TIMERBASE);

    switch(reg) {
    case TIMER_REG_DIVA:
        handle_timer_write(0, val);
        break;
    case TIMER_REG_DIVB:
        handle_timer_write(1, val);
        break;
    case TIMER_REG_DIVC:
        handle_timer_write(2, val);
        break;
    case TIMER_REG_CTRL:
        /* The counter number is in bits 6 and 7 */
        ctrnum = (val >> 6) & 3;
        if (ctrnum > 2) {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] WARNING: Write to invalid counter: %d\n",
                      R[NUM_PC], ctrnum);
            return;
        }
        ctr = &TIMERS[ctrnum];
        ctr->mode = (uint8) val;
        ctr->enabled = FALSE;
        ctr->lmb = FALSE;
        break;
    case TIMER_CLR_LATCH:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] unexpected write to clear timer latch\n",
                  R[NUM_PC]);
        break;
    }
}

void timer_tick()
{
    if (TIMERS[0].gate && TIMERS[0].enabled) {
        TIMERS[0].val = TIMERS[0].divider - 1;
    }

    if (TIMERS[1].gate && TIMERS[1].enabled) {
        TIMERS[1].val = TIMERS[1].divider - 1;
    }

    if (TIMERS[2].gate && TIMERS[2].enabled) {
        TIMERS[2].val = TIMERS[2].divider - 1;
    }
}
