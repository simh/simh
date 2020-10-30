/* 3b2_400_stddev.h: AT&T 3B2 Model 400 System Devices implementation

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
   This file contains system-specific registers and devices for the
   following 3B2 devices:

   - timer       8253 interval timer
   - nvram       Non-Volatile RAM
   - csr         Control Status Registers
   - tod         MM58174A Real-Time-Clock
*/

#include "3b2_defs.h"
#include "3b2_400_stddev.h"

DEBTAB sys_deb_tab[] = {
    { "INIT",       INIT_MSG,       "Init"              },
    { "READ",       READ_MSG,       "Read activity"     },
    { "WRITE",      WRITE_MSG,      "Write activity"    },
    { "EXECUTE",    EXECUTE_MSG,    "Execute activity"  },
    { "IRQ",        IRQ_MSG,        "Interrupt activity"},
    { "TRACE",      TRACE_DBG,      "Detailed activity" },
    { NULL,         0                                   }
};

struct timer_ctr TIMERS[3];

uint32 *NVRAM = NULL;

int32 tmxr_poll = 16667;

/* CSR */

uint16 csr_data;

BITFIELD csr_bits[] = {
    BIT(IOF),
    BIT(DMA),
    BIT(DISK),
    BIT(UART),
    BIT(PIR9),
    BIT(PIR8),
    BIT(CLK),
    BIT(IFLT),
    BIT(ITIM),
    BIT(FLOP),
    BIT(NA),
    BIT(LED),
    BIT(ALGN),
    BIT(RRST),
    BIT(PARE),
    BIT(TIMO),
    ENDBITS
};

UNIT csr_unit = {
    UDATA(NULL, UNIT_FIX, CSRSIZE)
};

REG csr_reg[] = {
    { HRDATADF(DATA, csr_data, 16, "CSR Data", csr_bits) },
    { NULL }
};

DEVICE csr_dev = {
    "CSR", &csr_unit, csr_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &csr_ex, &csr_dep, &csr_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat csr_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_reset(DEVICE *dptr)
{
    csr_data = 0;
    return SCPE_OK;
}

uint32 csr_read(uint32 pa, size_t size)
{
    uint32 reg = pa - CSRBASE;

    sim_debug(READ_MSG, &csr_dev,
              "[%08x] CSR=%04x\n",
              R[NUM_PC], csr_data);

    switch (reg) {
    case 0x2:
        if (size == 8) {
            return (csr_data >> 8) & 0xff;
        } else {
            return csr_data;
        }
    case 0x3:
        return csr_data & 0xff;
    default:
        return 0;
    }
}

void csr_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg = pa - CSRBASE;

    switch (reg) {
    case 0x03:    /* Clear Bus Timeout Error */
        csr_data &= ~CSRTIMO;
        break;
    case 0x07:    /* Clear Memory Parity Error */
        csr_data &= ~CSRPARE;
        break;
    case 0x0b:    /* Set System Reset Request */
        full_reset();
        cpu_boot(0, &cpu_dev);
        break;
    case 0x0f:    /* Clear Memory Alignment Fault */
        csr_data &= ~CSRALGN;
        break;
    case 0x13:    /* Set Failure LED */
        csr_data |= CSRLED;
        break;
    case 0x17:    /* Clear Failure LED */
        csr_data &= ~CSRLED;
        break;
    case 0x1b:    /* Set Floppy Motor On */
        csr_data |= CSRFLOP;
        break;
    case 0x1f:    /* Clear Floppy Motor On */
        csr_data &= ~CSRFLOP;
        break;
    case 0x23:    /* Set Inhibit Timers */
        sim_debug(WRITE_MSG, &csr_dev,
                  "[%08x] SET INHIBIT TIMERS\n", R[NUM_PC]);
        csr_data |= CSRITIM;
        break;
    case 0x27:    /* Clear Inhibit Timers */
        sim_debug(WRITE_MSG, &csr_dev,
                  "[%08x] CLEAR INHIBIT TIMERS\n", R[NUM_PC]);

        /* A side effect of clearing the timer inhibit bit is to cause
         * a simulated "tick" of any active timers.  This is a hack to
         * make diagnostics pass. This is not 100% accurate, but it
         * makes SVR3 and DGMON tests happy.
         */

        if (TIMERS[0].gate && TIMERS[0].enabled) {
            TIMERS[0].val = TIMERS[0].divider - 1;
        }

        if (TIMERS[1].gate && TIMERS[1].enabled) {
            TIMERS[1].val = TIMERS[1].divider - 1;
        }

        if (TIMERS[2].gate && TIMERS[2].enabled) {
            TIMERS[2].val = TIMERS[2].divider - 1;
        }

        csr_data &= ~CSRITIM;
        break;
    case 0x2b:    /* Set Inhibit Faults */
        csr_data |= CSRIFLT;
        break;
    case 0x2f:    /* Clear Inhibit Faults */
        csr_data &= ~CSRIFLT;
        break;
    case 0x33:    /* Set PIR9 */
        csr_data |= CSRPIR9;
        break;
    case 0x37:    /* Clear PIR9 */
        csr_data &= ~CSRPIR9;
        break;
    case 0x3b:    /* Set PIR8 */
        csr_data |= CSRPIR8;
        break;
    case 0x3f:    /* Clear PIR8 */
        csr_data &= ~CSRPIR8;
        break;
    default:
        break;
    }
}

/* NVRAM */

UNIT nvram_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, NVRAMSIZE)
};

REG nvram_reg[] = {
    { NULL }
};

DEVICE nvram_dev = {
    "NVRAM", &nvram_unit, nvram_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &nvram_ex, &nvram_dep, &nvram_reset,
    NULL, &nvram_attach, &nvram_detach,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    &nvram_help, NULL, NULL,
    &nvram_description
};

t_stat nvram_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if ((vptr == NULL) || (addr & 03)) {
        return SCPE_ARG;
    }

    if (addr >= NVRAMSIZE) {
        return SCPE_NXM;
    }

    *vptr = NVRAM[addr >> 2];

    return SCPE_OK;
}

t_stat nvram_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if (addr & 03) {
        return SCPE_ARG;
    }

    if (addr >= NVRAMSIZE) {
        return SCPE_NXM;
    }

    NVRAM[addr >> 2] = (uint32) val;

    return SCPE_OK;
}

t_stat nvram_reset(DEVICE *dptr)
{
    if (NVRAM == NULL) {
        NVRAM = (uint32 *)calloc(NVRAMSIZE >> 2, sizeof(uint32));
        memset(NVRAM, 0, sizeof(uint32) * NVRAMSIZE >> 2);
        nvram_unit.filebuf = NVRAM;
    }

    if (NVRAM == NULL) {
        return SCPE_MEM;
    }

    return SCPE_OK;
}

const char *nvram_description(DEVICE *dptr)
{
    return "Non-volatile memory, used to store system state between boots.\n";
}

t_stat nvram_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st,
            "The NVRAM holds system state between boots. On initial startup,\n"
            "if no valid NVRAM file is attached, you will see the message:\n"
            "\n"
            "     FW ERROR 1-01: NVRAM SANITY FAILURE\n"
            "     DEFAULT VALUES ASSUMED\n"
            "     IF REPEATED, CHECK THE BATTERY\n"
            "\n"
            "To avoid this message on subsequent boots, attach a new NVRAM file\n"
            "with the SIMH command:\n"
            "\n"
            "     sim> ATTACH NVRAM <filename>\n");
    return SCPE_OK;
}

t_stat nvram_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    /* If we've been asked to attach, make sure the ATTABLE
       and BUFABLE flags are set on the unit */
    uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);

    r = attach_unit(uptr, cptr);

    if (r != SCPE_OK) {
        /* Unset the ATTABLE and BUFABLE flags if we failed. */
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    } else {
        uptr->hwmark = (uint32) uptr->capac;
    }

    return r;
}

t_stat nvram_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    }

    return r;
}


uint32 nvram_read(uint32 pa, size_t size)
{
    uint32 offset = pa - NVRAMBASE;
    uint32 data = 0;
    uint32 sc = (~(offset & 3) << 3) & 0x1f;

    switch(size) {
    case 8:
        data = (NVRAM[offset >> 2] >> sc) & BYTE_MASK;
        break;
    case 16:
        if (offset & 2) {
            data = NVRAM[offset >> 2] & HALF_MASK;
        } else {
            data = (NVRAM[offset >> 2] >> 16) & HALF_MASK;
        }
        break;
    case 32:
        data = NVRAM[offset >> 2];
        break;
    }

    return data;
}

void nvram_write(uint32 pa, uint32 val, size_t size)
{
    uint32 offset = pa - NVRAMBASE;
    uint32 index = offset >> 2;
    uint32 sc, mask;

    switch(size) {
    case 8:
        sc = (~(pa & 3) << 3) & 0x1f;
        mask = (uint32) (0xff << sc);
        NVRAM[index] = (NVRAM[index] & ~mask) | (val << sc);
        break;
    case 16:
        if (offset & 2) {
            NVRAM[index] = (NVRAM[index] & ~HALF_MASK) | val;
        } else {
            NVRAM[index] = (NVRAM[index] & HALF_MASK) | (val << 16);
        }
        break;
    case 32:
        NVRAM[index] = val;
        break;
    }
}

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

t_stat timer_set_shutdown(UNIT *uptr, int32 val, CONST char* cptr, void* desc)
{
    struct timer_ctr *sanity = (struct timer_ctr *)timer_unit[0].tmr;

    sim_debug(EXECUTE_MSG, &timer_dev,
              "[%08x] Setting sanity timer to 0 for shutdown.\n", R[NUM_PC]);

    sanity->val = 0;
    csr_data &= ~CSRCLK;
    csr_data |= CSRTIMO;

    return SCPE_OK;
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
        csr_data |= CSRCLK;
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
        csr_data &= ~CSRCLK;
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

/*
 * MM58174A Time Of Day Clock
 *
 * Despite its name, this device is not used by the 3B2 as a clock. It
 * is only used to store the current date and time between boots. It
 * is set when an operator changes the date and time. Is is read at
 * boot time. Therefore, we do not need to treat it as a clock or
 * timer device here.
 */

UNIT tod_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, sizeof(TOD_DATA))
};

DEVICE tod_dev = {
    "TOD", &tod_unit, NULL, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &tod_reset,
    NULL, &tod_attach, &tod_detach,
    NULL, 0, 0, sys_deb_tab, NULL, NULL,
    &tod_help, NULL, NULL,
    &tod_description
};

t_stat tod_reset(DEVICE *dptr)
{
    if (tod_unit.filebuf == NULL) {
        tod_unit.filebuf = calloc(sizeof(TOD_DATA), 1);
        if (tod_unit.filebuf == NULL) {
            return SCPE_MEM;
        }
    }

    return SCPE_OK;
}

t_stat tod_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);

    r = attach_unit(uptr, cptr);

    if (r != SCPE_OK) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    } else {
        uptr->hwmark = (uint32) uptr->capac;
    }

    return r;
}

t_stat tod_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->flags = uptr->flags & (uint32) ~(UNIT_ATTABLE | UNIT_BUFABLE);
    }

    return r;
}

/*
 * Re-set the tod_data registers based on the current simulated time.
 */
void tod_resync()
{
    struct timespec now;
    struct tm tm;
    time_t sec;
    TOD_DATA *td = (TOD_DATA *)tod_unit.filebuf;

    sim_rtcn_get_time(&now, TMR_CLK);
    sec = now.tv_sec - td->delta;

    /* Populate the tm struct based on current sim_time */
    tm = *gmtime(&sec);

    td->tsec = 0;
    td->unit_sec = tm.tm_sec % 10;
    td->ten_sec = tm.tm_sec / 10;
    td->unit_min = tm.tm_min % 10;
    td->ten_min = tm.tm_min / 10;
    td->unit_hour = tm.tm_hour % 10;
    td->ten_hour = tm.tm_hour / 10;
    /* tm struct stores as 0-11, tod struct as 1-12 */
    td->unit_mon = (tm.tm_mon + 1) % 10;
    td->ten_mon = (tm.tm_mon + 1) / 10;
    td->unit_day = tm.tm_mday % 10;
    td->ten_day = tm.tm_mday / 10;
    td->year = 1 << ((tm.tm_year - 1) % 4);
}

/*
 * Re-calculate the delta between real time and simulated time
 */
void tod_update_delta()
{
    struct timespec now;
    struct tm tm = {0};
    time_t ssec;
    TOD_DATA *td = (TOD_DATA *)tod_unit.filebuf;
    sim_rtcn_get_time(&now, TMR_CLK);

    /* Let the host decide if it is DST or not */
    tm.tm_isdst = -1;

    /* Compute the simulated seconds value */
    tm.tm_sec = (td->ten_sec * 10) + td->unit_sec;
    tm.tm_min = (td->ten_min * 10) + td->unit_min;
    tm.tm_hour = (td->ten_hour * 10) + td->unit_hour;
    /* tm struct stores as 0-11, tod struct as 1-12 */
    tm.tm_mon = ((td->ten_mon * 10) + td->unit_mon) - 1;
    tm.tm_mday = (td->ten_day * 10) + td->unit_day;

    /* We're forced to do this weird arithmetic because the TOD chip
     * used by the 3B2 does not store the year. It only stores the
     * offset from the nearest leap year. */
    switch(td->year) {
    case 1: /* Leap Year - 3 */
        tm.tm_year = 85;
        break;
    case 2: /* Leap Year - 2 */
        tm.tm_year = 86;
        break;
    case 4: /* Leap Year - 1 */
        tm.tm_year = 87;
        break;
    case 8: /* Leap Year */
        tm.tm_year = 88;
        break;
    default:
        break;
    }

    ssec = mktime(&tm);
    td->delta = (int32)(now.tv_sec - ssec);
}

uint32 tod_read(uint32 pa, size_t size)
{
    uint8 reg;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    tod_resync();

    reg = pa - TODBASE;

    switch(reg) {
    case 0x04:        /* 1/10 Sec    */
        return td->tsec;
    case 0x08:        /* 1 Sec       */
        return td->unit_sec;
    case 0x0c:        /* 10 Sec      */
        return td->ten_sec;
    case 0x10:        /* 1 Min       */
        return td->unit_min;
    case 0x14:        /* 10 Min      */
        return td->ten_min;
    case 0x18:        /* 1 Hour      */
        return td->unit_hour;
    case 0x1c:        /* 10 Hour     */
        return td->ten_hour;
    case 0x20:        /* 1 Day       */
        return td->unit_day;
    case 0x24:        /* 10 Day      */
        return td->ten_day;
    case 0x28:        /* Day of Week */
        return td->wday;
    case 0x2c:        /* 1 Month     */
        return td->unit_mon;
    case 0x30:        /* 10 Month    */
        return td->ten_mon;
    case 0x34:        /* Year        */
        return td->year;
    default:
        break;
    }

    return 0;
}

void tod_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    reg = pa - TODBASE;

    switch(reg) {
    case 0x04:        /* 1/10 Sec    */
        td->tsec = (uint8) val;
        break;
    case 0x08:        /* 1 Sec       */
        td->unit_sec = (uint8) val;
        break;
    case 0x0c:        /* 10 Sec      */
        td->ten_sec = (uint8) val;
        break;
    case 0x10:        /* 1 Min       */
        td->unit_min = (uint8) val;
        break;
    case 0x14:        /* 10 Min      */
        td->ten_min = (uint8) val;
        break;
    case 0x18:        /* 1 Hour      */
        td->unit_hour = (uint8) val;
        break;
    case 0x1c:        /* 10 Hour     */
        td->ten_hour = (uint8) val;
        break;
    case 0x20:        /* 1 Day       */
        td->unit_day = (uint8) val;
        break;
    case 0x24:        /* 10 Day      */
        td->ten_day = (uint8) val;
        break;
    case 0x28:        /* Day of Week */
        td->wday = (uint8) val;
        break;
    case 0x2c:        /* 1 Month     */
        td->unit_mon = (uint8) val;
        break;
    case 0x30:        /* 10 Month    */
        td->ten_mon = (uint8) val;
        break;
    case 0x34:        /* Year */
        td->year = (uint8) val;
        break;
    case 0x38:
        if (val & 1) {
            tod_update_delta();
        }
        break;
    default:
        break;
    }
}

const char *tod_description(DEVICE *dptr)
{
    return "Time-of-Day clock, used to store system time between boots.\n";
}

t_stat tod_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st,
            "The TOD is a battery-backed time-of-day clock that holds system\n"
            "time between boots. In order to store the time, a file must be\n"
            "attached to the TOD device with the SIMH command:\n"
            "\n"
            "     sim> ATTACH TOD <filename>\n"
            "\n"
            "On a newly installed System V Release 3 UNIX system, no system\n"
            "time will be stored in the TOD clock. In order to set the system\n"
            "time, run the following command from within UNIX (as root):\n"
            "\n"
            "     # sysadm datetime\n"
            "\n"
            "On subsequent boots, the correct system time will restored from\n"
            "from the TOD.\n");

    return SCPE_OK;
}
