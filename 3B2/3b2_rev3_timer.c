/* 3b2_rev3_timer.c: 82C54 Interval Timer.

   Copyright (c) 2021, Seth J. Morabito

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
 * 82C54 Timer.
 *
 * 82C54 (Rev3) Timer IC has three interval timers, which we treat
 * here as three units.
 *
 * In the 3B2, the three timers are assigned specific purposes:
 *
 *  - Timer 0: SYSTEM SANITY TIMER. This timer is normally loaded with
 *             a short timeout and allowed to run. If it times out, it
 *             will generate an interrupt and cause a system
 *             error. Software resets the timer regularly to ensure
 *             that it does not time out.  It is fed by a 10 kHz
 *             clock, so each single counting step of this timer is
 *             100 microseconds.
 *
 *  - Timer 1: UNIX INTERVAL TIMER. This is the main timer that drives
 *             process switching in Unix. It operates at a fixed rate,
 *             and the counter is set up by Unix to generate an
 *             interrupt once every 10 milliseconds. The timer is fed
 *             by a 100 kHz clock, so each single counting step of
 *             this timer is 10 microseconds.
 *
 *  - Timer 2: BUS TIMEOUT TIMER. This timer is reset every time the
 *             IO bus is accessed, and then stopped when the IO bus
 *             responds. It is mainly used to determine when the IO
 *             bus is hung (e.g., no card is installed in a given
 *             slot, so nothing can respond). When it times out, it
 *             generates an interrupt. It is fed by a 500 kHz clock,
 *             so each single counting step of this timer is 2
 *             microseconds.
 *
 *
 * Implementaiton Notes
 * ====================
 *
 * In general, no attempt has been made to create an accurate
 * emulation of the 82C54 timer. This implementation is truly built
 * for the 3B2, and even more specifically for System V Unix, which is
 * the only operating system ever to have been ported to the 3B2.
 *
 *  - The Bus Timeout Timer is not implemented other than a stub that
 *    is designed to pass hardware diagnostics. The simulator IO
 *    subsystem always sets the correct interrupt directly if the bus
 *    will not respond.
 *
 *  - The System Sanity Timer is also not implemented other than a
 *    stub to pass diagnostics.
 *
 *  - The main Unix Interval Timer is implemented as a true SIMH clock
 *    when set up for the correct mode. In other modes, it likewise
 *    implements a stub designed to pass diagnostics.
 */

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_defs.h"
#include "3b2_timer.h"

struct timer_ctr TIMERS[3];

int32 tmxr_poll = 16667;

UNIT timer_unit[] = {
    { UDATA(&timer0_svc, 0, 0) },
    { UDATA(&timer1_svc, UNIT_IDLE, 0) },
    { UDATA(&timer2_svc, 0, 0) },
    { NULL }
};

UNIT *timer_clk_unit = &timer_unit[1];

REG timer_reg[] = {
    { HRDATAD(DIV0,   TIMERS[0].divider, 16, "Divider (0)") },
    { HRDATAD(COUNT0, TIMERS[0].val,     16, "Count (0)")   },
    { HRDATAD(CTRL0,  TIMERS[0].ctrl,    8,  "Control (0)") },
    { HRDATAD(DIV1,   TIMERS[1].divider, 16, "Divider (1)") },
    { HRDATAD(COUNT1, TIMERS[1].val,     16, "Count (1)")   },
    { HRDATAD(CTRL1,  TIMERS[1].ctrl,    8,  "Control (1)") },
    { HRDATAD(DIV2,   TIMERS[2].divider, 16, "Divider (2)") },
    { HRDATAD(COUNT2, TIMERS[2].val,     16, "Count (2)")   },
    { HRDATAD(CTRL2,  TIMERS[2].ctrl,    8,  "Control (2)") },
    { NULL }
};

DEVICE timer_dev = {
    "TIMER", timer_unit, timer_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat timer_reset(DEVICE *dptr) {
    int32 i;

    memset(&TIMERS, 0, sizeof(struct timer_ctr) * 3);

    for (i = 0; i < 3; i++) {
        timer_unit[i].tmrnum = i;
        timer_unit[i].tmr = (void *)&TIMERS[i];
    }

    /* TODO: I don't think this is right. Verify. */
    /*
    if (!sim_is_running) {
        t = sim_rtcn_init_unit(timer_clk_unit, TPS_CLK, TMR_CLK);
        sim_activate_after_abs(timer_clk_unit, 1000000 / t);
    }
    */

    return SCPE_OK;
}

static void timer_activate(uint8 ctrnum)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];

    switch (ctrnum) {
    case TIMER_SANITY:
        if ((csr_data & CSRISTIM) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] SANITY TIMER: Activating after %d steps\n",
                      R[NUM_PC], ctr->val);
            sim_activate_abs(&timer_unit[ctrnum], ctr->val);
            ctr->val--;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] SANITY TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
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
        if ((csr_data & CSRITIMO) == 0) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] BUS TIMER: Activating after %d steps\n",
                      R[NUM_PC], ctr->val);
            sim_activate_abs(&timer_unit[ctrnum], (ctr->val - 2));
            ctr->val -= 2;
        } else {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] BUS TIMER: Currently disabled, not starting\n",
                      R[NUM_PC]);
        }
        break;
    default:
        break;
    }
}

void timer_enable(uint8 ctrnum)
{
    sim_debug(EXECUTE_MSG, &timer_dev,
              "[%08x] Enabling timer %d\n",
              R[NUM_PC], ctrnum);
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

    ctr = (struct timer_ctr *)uptr->tmr;
    
    if (ctr->enabled) {
        sim_debug(EXECUTE_MSG, &timer_dev,
                  "[%08x] TIMER 0 COMPLETION.\n",
                  R[NUM_PC]);
        if (!(csr_data & CSRISTIM)) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] TIMER 0 NMI IRQ.\n",
                      R[NUM_PC]);
            ctr->val = 0xffff;
            cpu_nmi = TRUE;
            CSRBIT(CSRSTIMO, TRUE);
            CPU_SET_INT(INT_BUS_TMO);
        }
    }

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
        CSRBIT(CSRCLK, TRUE);
        CPU_SET_INT(INT_CLOCK);
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

    ctr = (struct timer_ctr *)uptr->tmr;

    if (ctr->enabled && TIMER_RW(ctr) == CLK_LSB) {
        sim_debug(EXECUTE_MSG, &timer_dev,
                  "[%08x] TIMER 2 COMPLETION.\n",
                  R[NUM_PC]);
        if (!(csr_data & CSRITIMO)) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[%08x] TIMER 2 IRQ.\n",
                      R[NUM_PC]);
            ctr->val = 0xffff;
            CSRBIT(CSRTIMO, TRUE);
            CPU_SET_INT(INT_BUS_TMO);
            /* Also trigger a bus abort */
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        }
    }

    return SCPE_OK;
}

uint32 timer_read(uint32 pa, size_t size)
{
    uint32 reg;
    uint16 ctr_val;
    uint8 ctrnum, retval;
    struct timer_ctr *ctr;

    reg = pa - TIMERBASE;
    ctrnum = (reg >> 2) & 0x3;
    ctr = &TIMERS[ctrnum];

    switch (reg) {
    case TIMER_REG_DIVA:
    case TIMER_REG_DIVB:
    case TIMER_REG_DIVC:
        ctr_val = ctr->val;

        switch (TIMER_RW(ctr)) {
        case CLK_LSB:
            retval = ctr_val & 0xff;
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] [%d] [LSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, retval, retval);
            break;
        case CLK_MSB:
            retval = (ctr_val & 0xff00) >> 8;
            sim_debug(READ_MSG, &timer_dev,
                      "[%08x] [%d] [MSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, retval, retval);
            break;
        case CLK_LMB:
            if (ctr->r_ctrl_latch) {
                ctr->r_ctrl_latch = FALSE;
                retval = ctr->ctrl_latch;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LATCH CTRL] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            } else if (ctr->r_cnt_latch) {
                if (ctr->r_lmb) {
                    ctr->r_lmb = FALSE;
                    retval = (ctr->cnt_latch & 0xff00) >> 8;
                    ctr->r_cnt_latch = FALSE;
                    sim_debug(READ_MSG, &timer_dev,
                              "[%08x] [%d] [LATCH DATA MSB] val=%d (0x%x)\n",
                              R[NUM_PC], ctrnum, retval, retval);
                } else {
                    ctr->r_lmb = TRUE;
                    retval = ctr->cnt_latch & 0xff;
                    sim_debug(READ_MSG, &timer_dev,
                              "[%08x] [%d] [LATCH DATA LSB] val=%d (0x%x)\n",
                              R[NUM_PC], ctrnum, retval, retval);
                }
            } else if (ctr->r_lmb) {
                ctr->r_lmb = FALSE;
                retval = (ctr_val & 0xff00) >> 8;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LMB - MSB] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            } else {
                ctr->r_lmb = TRUE;
                retval = ctr_val & 0xff;
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] [%d] [LMB - LSB] val=%d (0x%x)\n",
                          R[NUM_PC], ctrnum, retval, retval);
            }
            break;
        default:
            retval = 0;
        }

        return retval;
    case TIMER_REG_CTRL:
        return ctr->ctrl;
    case TIMER_CLR_LATCH:
        /* Clearing the timer latch has a side-effect
           of also clearing pending interrupts */
        CSRBIT(CSRCLK, FALSE);
        CPU_CLR_INT(INT_CLOCK);
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
    UNIT *unit = &timer_unit[ctrnum];

    ctr = &TIMERS[ctrnum];
    ctr->enabled = TRUE;

    switch(TIMER_RW(ctr)) {
    case CLK_LSB:
        ctr->divider = val & 0xff;
        ctr->val = ctr->divider;
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [%d] [LSB] val=%d (0x%x)\n",
                  R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_MSB:
        ctr->divider = (val & 0xff) << 8;
        ctr->val = ctr->divider;
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [%d] [MSB] val=%d (0x%x)\n",
                  R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_LMB:
        if (ctr->w_lmb) {
            ctr->w_lmb = FALSE;
            ctr->divider = (uint16) ((ctr->divider & 0x00ff) | ((val & 0xff) << 8));
            ctr->val = ctr->divider;
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] [%d] [LMB - MSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
            timer_activate(ctrnum);
        } else {
            ctr->w_lmb = TRUE;
            ctr->divider = (ctr->divider & 0xff00) | (val & 0xff);
            ctr->val = ctr->divider;
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] [%d] [LMB - LSB] val=%d (0x%x)\n",
                      R[NUM_PC], ctrnum, val & 0xff, val & 0xff);
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
        ctrnum = (val >> 6) & 3;
        if (ctrnum == 3) {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] READ BACK COMMAND. DATA=%02x\n",
                      R[NUM_PC], val);
            if (val & 2) {
                ctr = &TIMERS[0];
                if ((val & 0x20) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
            if (val & 4) {
                ctr = &TIMERS[1];
                if ((val & 0x10) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
            if (val & 8) {
                ctr = &TIMERS[2];
                if ((val & 0x10) == 0) {
                    ctr->ctrl_latch = (uint16) TIMERS[2].ctrl;
                    ctr->r_ctrl_latch = TRUE;
                }
                if ((val & 0x20) == 0) {
                    ctr->cnt_latch = ctr->val;
                    ctr->r_cnt_latch = TRUE;
                }
            }
        } else {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] Timer Control Write: timer %d => %02x\n",
                      R[NUM_PC], ctrnum, val & 0xff);
            ctr = &TIMERS[ctrnum];
            ctr->ctrl = (uint8) val;
            ctr->enabled = FALSE;
            ctr->w_lmb = FALSE;
            ctr->r_lmb = FALSE;
            ctr->val = 0xffff;
            ctr->divider = 0xffff;
        }
        break;
    case TIMER_CLR_LATCH:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] unexpected write to clear timer latch\n",
                  R[NUM_PC]);
        break;
    default:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] unknown timer register: %d\n",
                  R[NUM_PC], reg);
    }
}
