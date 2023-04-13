/* 3b2_timer.c: 8253/82C54 Interval Timer

   Copyright (c) 2021-2022, Seth J. Morabito

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
 * The 8253/82C54 Timer IC has three interval timers, which we treat
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
 * In general, no attempt has been made to create a truly accurate
 * simulation of the 8253/82C54 timer. This implementation is built
 * for the 3B2, and even more specifically to pass System V timer
 * "Sanity/Interval Timer" diagnostics.
 *
 *  - The Bus Timeout Timer is not implemented other than a stub that
 *    is designed to pass hardware diagnostics. The simulator IO
 *    subsystem always sets the correct interrupt directly if the bus
 *    will not respond.
 *
 *  - The System Sanity Timer is also not implemented other than a
 *    stub to pass diagnostics.
 *
 *  - The main Unix Interval Timer is more fully implemented, because
 *    it drives system interrupts in System V UNIX.
 */

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_defs.h"
#include "3b2_timer.h"

#define MIN_DIVIDER 50

#if defined (REV3)
#define QUICK_DELAY 10
#else
#define QUICK_DELAY 100
#endif

#define MIN_US 100

#define CALC_US(C,N) ((TIMER_MODE(C) == 3) ?      \
                      (TIME_BASE[(N)] * (C)->divider) / 2 : \
                      TIME_BASE[(N)] * (C)->divider)

#define DELAY_US(C,N) (MAX(MIN_US, CALC_US((C),(N))))

#if defined(REV3)
/* Microseconds per step (Version 3 system board):
 *
 * Timer 0: 10KHz time base
 * Timer 1: 100KHz time base
 * Timer 2: 500KHz time base
 */
static uint32 TIME_BASE[3] = {
    100, 10, 1
};
#else
/* Microseconds per step (Version 2 system board):
 *
 * Timer 0: 100Khz time base
 * Timer 1: 100Khz time base
 * Timer 2: 500Khz time base
 */
static uint32 TIME_BASE[3] = {
    10, 10, 2
};
#endif

struct timer_ctr TIMERS[3];

UNIT timer_unit[] = {
    { UDATA(&tmr_svc, UNIT_IDLE, 0) },
    { UDATA(&tmr_svc, UNIT_IDLE, 0) },
    { UDATA(&tmr_svc, UNIT_IDLE, 0) },
    { NULL }
};

UNIT *tmr_int_unit = &timer_unit[3];

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
    "TMR",
    timer_unit,
    timer_reg,
    NULL,
    3,
    16,
    8,
    4,
    16,
    32,
    NULL,
    NULL,
    &timer_reset,
    NULL,
    NULL,
    NULL,
    NULL,
    DEV_DEBUG,
    0,
    sys_deb_tab,
    NULL,
    NULL,
    &tmr_help,
    NULL,
    NULL,
    &tmr_description
};

t_stat timer_reset(DEVICE *dptr) {
    int32 i;

    memset(&TIMERS, 0, sizeof(struct timer_ctr) * 3);

    /* Store the timer/counter number in the UNIT */
    for (i = 0; i < 3; i++) {
        timer_unit[i].u3 = i;
    }

    return SCPE_OK;
}

/*
 * Inhibit or allow a timer externally.
 */
void timer_gate(uint8 ctrnum, t_bool inhibit)
{
    struct timer_ctr *ctr = &TIMERS[ctrnum];

    if (inhibit) {
        ctr->gate = FALSE;
        sim_cancel(&timer_unit[ctrnum]);
    } else {
        ctr->gate = TRUE;
        if (ctr->enabled && !sim_is_active(&timer_unit[ctrnum])) {
            sim_activate_after(&timer_unit[ctrnum], DELAY_US(ctr, ctrnum));
            ctr->val--;
        }
    }
}

static void timer_activate(uint8 ctrnum)
{
    struct timer_ctr *ctr = &TIMERS[ctrnum];

    if (ctr->enabled && ctr->gate) {
        if (ctr->divider < MIN_DIVIDER) {
            /* If the timer delay is too short, we need to force a
               very quick activation */
            sim_activate_abs(&timer_unit[ctrnum], QUICK_DELAY);
        } else {
            /* Otherwise, use a computed time in microseconds */
            sim_activate_after_abs(&timer_unit[ctrnum], DELAY_US(ctr, ctrnum));
        }
    }
}

/*
 * Sanity, Non-calibrated Interval, and Bus Timeout Timer service routine
 */
t_stat tmr_svc(UNIT *uptr)
{
    int32 ctr_num = uptr->u3;
    uint32 usec_delay;
    struct timer_ctr *ctr = &TIMERS[ctr_num];

    if (ctr == NULL) {
        return SCPE_SUB;
    }

    /* If the timer isn't enabled, do nothing. */
    if (!ctr->enabled) {
        return SCPE_OK;
    }

    sim_debug(EXECUTE_MSG, &timer_dev,
              "[tmr_svc] Handling timeout for ctr number %d\n",
              ctr_num);

    switch (ctr_num) {
    case TMR_SANITY:
#if defined (REV3)
        if (!CSR(CSRISTIM) && TIMER_MODE(ctr) != 4) {
            cpu_nmi = TRUE;
            CSRBIT(CSRSTIMO, TRUE);
            CPU_SET_INT(INT_BUS_TMO);
            ctr->val = 0xffff;
        }
#endif
        break;
    case TMR_INT:
        if (!CSR(CSRITIM)) {
            CSRBIT(CSRCLK, TRUE);
            CPU_SET_INT(INT_CLOCK);
            if (ctr->enabled && ctr->gate) {
                usec_delay = DELAY_US(ctr, TMR_INT);
                sim_debug(EXECUTE_MSG, &timer_dev,
                          "[tmr_svc] Re-triggering TMR_INT in %d usec\n", usec_delay);
                sim_activate_after(uptr, usec_delay);
            }
            ctr->val = 0xffff;
        }
        break;
    case TMR_BUS:
#if defined (REV3)
        /* Only used during diagnostics */
        if (TIMER_RW(ctr) == CLK_LSB) {
            sim_debug(EXECUTE_MSG, &timer_dev,
                      "[tmr_svc] BUS TIMER FIRING. Setting memory fault and interrupt\n");
            CSRBIT(CSRTIMO, TRUE);
            CPU_SET_INT(INT_BUS_TMO);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
            ctr->val = 0xffff;
        }
#endif
        break;
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

    sim_debug(EXECUTE_MSG, &timer_dev,
              "timer_read: reg=%x\n", reg);

    switch (reg) {
    case TIMER_REG_DIVA:
    case TIMER_REG_DIVB:
    case TIMER_REG_DIVC:
        ctr_val = ctr->val;

        switch (TIMER_RW(ctr)) {
        case CLK_LSB:
            retval = ctr_val & 0xff;
            break;
        case CLK_MSB:
            retval = (ctr_val & 0xff00) >> 8;
            break;
        case CLK_LMB:
            if (ctr->r_ctrl_latch) {
                ctr->r_ctrl_latch = FALSE;
                retval = ctr->ctrl_latch;
            } else if (ctr->r_cnt_latch) {
                if (ctr->r_lmb) {
                    ctr->r_lmb = FALSE;
                    retval = (ctr->cnt_latch & 0xff00) >> 8;
                    ctr->r_cnt_latch = FALSE;
                } else {
                    ctr->r_lmb = TRUE;
                    retval = ctr->cnt_latch & 0xff;
                }
            } else if (ctr->r_lmb) {
                ctr->r_lmb = FALSE;
                retval = (ctr_val & 0xff00) >> 8;
            } else {
                ctr->r_lmb = TRUE;
                retval = ctr_val & 0xff;
            }
            break;
        default:
            retval = 0;
        }

        break;
    case TIMER_REG_CTRL:
        retval = ctr->ctrl;
        break;
    case TIMER_CLR_LATCH:
        /* Clearing the timer latch has a side-effect
           of also clearing pending interrupts */
        CSRBIT(CSRCLK, FALSE);
        CPU_CLR_INT(INT_CLOCK);
        retval = 0;
        break;
    default:
        /* Unhandled */
        retval = 0;
        break;
    }

    return retval;
}

void handle_timer_write(uint8 ctrnum, uint32 val)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];
    ctr->enabled = TRUE;

    switch(TIMER_RW(ctr)) {
    case CLK_LSB:
        ctr->divider = val & 0xff;
        ctr->val = ctr->divider;
        sim_debug(EXECUTE_MSG, &timer_dev, "TIMER_WRITE: CTR=%d LSB=%02x\n", ctrnum, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_MSB:
        ctr->divider = (val & 0xff) << 8;
        ctr->val = ctr->divider;
        sim_debug(EXECUTE_MSG, &timer_dev, "TIMER_WRITE: CTR=%d MSB=%02x\n", ctrnum, val & 0xff);
        timer_activate(ctrnum);
        break;
    case CLK_LMB:
        if (ctr->w_lmb) {
            ctr->w_lmb = FALSE;
            ctr->divider = (uint16) ((ctr->divider & 0x00ff) | ((val & 0xff) << 8));
            ctr->val = ctr->divider;
            sim_debug(EXECUTE_MSG, &timer_dev, "TIMER_WRITE: CTR=%d (L/M) MSB=%02x\n", ctrnum, val & 0xff);
            timer_activate(ctrnum);
        } else {
            ctr->w_lmb = TRUE;
            ctr->divider = (ctr->divider & 0xff00) | (val & 0xff);
            ctr->val = ctr->divider;
            sim_debug(EXECUTE_MSG, &timer_dev, "TIMER_WRITE: CTR=%d (L/M) LSB=%02x\n", ctrnum, val & 0xff);
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

    sim_debug(EXECUTE_MSG, &timer_dev,
              "timer_write: reg=%x val=%x\n", reg, val);

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
                  "unexpected write to clear timer latch\n");
        break;
    default:
        sim_debug(WRITE_MSG, &timer_dev,
                  "unknown timer register: %d\n",
                  reg);
    }
}

CONST char *tmr_description(DEVICE *dptr)
{
#if defined (REV3)
    return "82C54 Programmable Interval Timer";
#else
    return "8253 Programmable Interval Timer";
#endif
}

t_stat tmr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
#if defined (REV3)
    fprintf(st, "82C54 Programmable Interval Timer (TMR)\n\n");
    fprintf(st, "The TMR device implements three programmable timers used by the 3B2/700\n");
#else
    fprintf(st, "8253 Programmable Interval Timer (TMR)\n\n");
    fprintf(st, "The TMR device implements three programmable timers used by the 3B2/400\n");
#endif
    fprintf(st, "to perform periodic tasks and sanity checks.\n\n");
    fprintf(st, "- TMR0: Used as a system sanity timer.\n");
    fprintf(st, "- TMR1: Used as a periodic 10 millisecond interval timer.\n");
    fprintf(st, "- TMR2: Used as a bus timeout timer.\n");

    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);

    return SCPE_OK;
}
