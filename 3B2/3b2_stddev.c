/* 3b2_stddev.c: Miscellaneous System Board Devices

   Copyright (c) 2017-2022, Seth J. Morabito

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

   - nvram       Non-Volatile RAM
   - tod         MM58174A and MM58274C Real-Time-Clock
   - flt         Fault Register (Rev 3 only)
*/

#include "3b2_stddev.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_timer.h"

DEBTAB sys_deb_tab[] = {
    { "INIT",       INIT_MSG,       "Init"              },
    { "READ",       READ_MSG,       "Read activity"     },
    { "WRITE",      WRITE_MSG,      "Write activity"    },
    { "EXECUTE",    EXECUTE_MSG,    "Execute activity"  },
    { "IRQ",        IRQ_MSG,        "Interrupt activity"},
    { "TRACE",      TRACE_DBG,      "Detailed activity" },
    { NULL,         0                                   }
};

uint32 *NVRAM = NULL;

/* NVRAM */
UNIT nvram_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, NVRSIZE)
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

    if (addr >= NVRSIZE) {
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

    if (addr >= NVRSIZE) {
        return SCPE_NXM;
    }

    NVRAM[addr >> 2] = (uint32) val;

    return SCPE_OK;
}

t_stat nvram_reset(DEVICE *dptr)
{
    if (NVRAM == NULL) {
        NVRAM = (uint32 *)calloc(NVRSIZE >> 2, sizeof(uint32));
        memset(NVRAM, 0, sizeof(uint32) * NVRSIZE >> 2);
        nvram_unit.filebuf = NVRAM;
    }

    if (NVRAM == NULL) {
        return SCPE_MEM;
    }

    return SCPE_OK;
}

const char *nvram_description(DEVICE *dptr)
{
    return "Non-Volatile RAM.\n";
}

t_stat nvram_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "Non-Volatile RAM\n\n");
    fprintf(st, "The %s device is a small battery-backed, non-volatile RAM\n", dptr->name);
    fprintf(st, "used by the 3B2 to hold system configuration and diagnostic data.\n\n");
    fprintf(st, "In order for the simulator to keep track of this data while not\n");
    fprintf(st, "running, the %s device may be attached to a file, e.g.\n\n", dptr->name);
    fprintf(st, "    sim> ATTACH NVRAM <filename>\n");
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);
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
    uint32 offset = pa - NVRBASE;
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
    uint32 offset = pa - NVRBASE;
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
 * MM58174A and MM58274C Time Of Day Clock.
 *
 * In addition to keeping track of time of day in tenths of seconds,
 * this device is also used as the simulator's primary calibrated
 * real-time clock. It operates at the speed of 100Hz, with every
 * tenth step incrementing the time-of-day counter.
 */

#define TOD_12H(TD)       (((TD)->clkset & 1) == 0)
#define TOD_BCDH(V)       (((V) / 10) & 0xf)
#define TOD_BCDL(V)       (((V) % 10) & 0xf)
#define TOD_LYEAR(TD)     ((TD)->lyear == 0)
#define TOD_LYEAR_INC(TD)                          \
    do {                                           \
        (TD)->lyear = (((TD)->lyear + 1) & 0x3);   \
        (TD)->clkset &= 3;                         \
        (TD)->clkset |= (TD)->lyear << 2;          \
    } while(0)

#define CTRL_DISABLE      0x4
#define CLKSET_PM         0x2
#define FLAG_DATA_CHANGED 0x4
#define FLAG_INTERRUPT    0x1
#define MIN_DIFF          5l
#define MAX_DIFF          157680000l

#define CLK_DELAY 10000   /* 10 milliseconds per tick */
#define CLK_TPS   100l    /* 100 ticks per second */

static void tod_resync(UNIT *uptr);
static void tod_tick(UNIT *uptr);
static t_stat tod_svc(UNIT *uptr);
static t_bool tod_enabled;

int32 tmr_poll = CLK_DELAY;
int32 tmxr_poll = CLK_DELAY;

UNIT tod_unit = {
    UDATA(&tod_svc, UNIT_FIX|UNIT_BINK|UNIT_IDLE, sizeof(TOD_DATA)), CLK_DELAY
};

REG tod_reg[] = {
    { DRDATAD(POLL, tmr_poll, 24, "Calibrated poll interval") },
    { 0 }
};

DEVICE tod_dev = {
    "TOD", &tod_unit, tod_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &tod_reset,
    NULL, &tod_attach, &tod_detach,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    &tod_help, NULL, NULL,
    &tod_description
};

/*
 * Attempt to re-sync the TOD by catching up (if lagging) and updating
 * the current time stored in the TOD state.
 *
 * Because this process may be expensive when catching up following a
 * very long time without the simulator running, the process will
 * short-circuit if the delta is longer than 5 years, or if no
 * previous time was recorded.
 */
static void tod_resync(UNIT *uptr)
{
    TOD_DATA *td;
    time_t delta;
    uint32 catchup_ticks;

    if (!(uptr->flags & UNIT_ATT) || uptr->filebuf == NULL) {
        return;
    }

    td = (TOD_DATA *)uptr->filebuf;

    if (td->time > 0) {
        delta = time(NULL) - td->time;
        if (delta > MIN_DIFF && delta < MAX_DIFF) {
            catchup_ticks = (uint32) delta * CLK_TPS;
            sim_debug(EXECUTE_MSG, &tod_dev,
                      "Catching up with a delta of %ld seconds (%d ticks).\n",
                      delta, catchup_ticks);
            while (catchup_ticks-- > 0) {
                tod_tick(&tod_unit);
            }
        }
    }

    td->time = time(NULL);
}

t_stat tod_reset(DEVICE *dptr)
{
    int32 t;

    if (tod_unit.filebuf == NULL) {
        tod_unit.filebuf = calloc(sizeof(TOD_DATA), 1);
        if (tod_unit.filebuf == NULL) {
            return SCPE_MEM;
        }
    }

    /* We start in a running state */
    tod_enabled = TRUE;

    t = sim_rtcn_init_unit(&tod_unit, tod_unit.wait, TMR_CLK);
    sim_activate_after(&tod_unit, 1000000/CLK_TPS);
    tmr_poll = t;
    tmxr_poll = t;

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

static t_stat tod_svc(UNIT *uptr)
{
    TOD_DATA *td = (TOD_DATA *)uptr->filebuf;
    int32 t;

    /* Re-sync the recorded system time once every second */
    if (tod_enabled) {
        tod_tick(uptr);

        if (td->tsec == 0) {
            tod_resync(uptr);
        }
    }

    t = sim_rtcn_calb(CLK_TPS, TMR_CLK);
    sim_activate_after(uptr, 1000000/CLK_TPS);
    tmr_poll = t;
    tmxr_poll = t;
    return SCPE_OK;
}

/*
 * The MM58174 and MM58274 consist of a set of failry "dumb" roll-over
 * counters. In an ideal world, we'd just look at the real system time
 * and translate that into whatever read the host needs.
 * Unfortunately, since the Day-of-Week and Leap Year registers are
 * totally independent of whatever the "real" date and time should be,
 * this doesn't map very well, and DGMON hardware diagnostics fail.
 *
 * Instead, we model the behavior of the chip accurately here. Each
 * rollover is cascaded to the next highest register, using the same
 * logic the chip uses.
 */
static void tod_tick(UNIT *uptr)
{
    TOD_DATA *td = (TOD_DATA *)uptr->filebuf;

    if (++td->tsec > 99) {
        td->tsec = 0;
        td->flags |= FLAG_DATA_CHANGED;
        if (++td->sec > 59) {
            td->sec = 0;
            if (++td->min > 59) {
                td->min = 0;
                td->hour++;

                /* 12-hour clock cycles from 1-12, 24-hour clock cycles from 00-23 */
                if (TOD_12H(td)) {
                    if (td->hour == 12) {
                        td->clkset ^= CLKSET_PM;
                    }
                    if (td->hour > 12) {
                        td->hour = 1;
                    }
                } else if (td->hour > 23) {
                    td->hour = 0;
                }

                if ((TOD_12H(td) && td->hour == 12) || (!TOD_12H(td) && td->hour == 0)) {
                    /* Manage day-of-week */
                    td->wday++;
                    if (td->wday > 7) {
                        td->wday = 1;
                    }
                    td->day++;
                    switch(td->mon) {
                    case 2: /* FEB */
                        if (TOD_LYEAR(td)) {
                            if (td->day > 29) {
                                td->day = 1;
                            }
                        } else {
                            if (td->day > 28) {
                                td->day = 1;
                            }
                        }
                        break;
                    case 4:  /* APR */
                    case 6:  /* JUN */
                    case 9:  /* SEP */
                    case 11: /* NOV */
                        if (td->day > 30) {
                            td->day = 1;
                        }
                        break;
                    case 1:  /* JAN */
                    case 3:  /* MAR */
                    case 5:  /* MAY */
                    case 7:  /* JUL */
                    case 8:  /* AUG */
                    case 10: /* OCT */
                    case 12: /* DEC */
                        if (td->day > 31) {
                            td->day = 1;
                        }
                        break;
                    }
                    if (td->day == 1) {
                        if (++td->mon > 12) {
                            td->mon = 1;
                            TOD_LYEAR_INC(td);
                            if (++td->year > 99) {
                                td->year = 0;
                            }
                        }
                    }
                }
            }
        }
    }
}


uint32 tod_read(uint32 pa, size_t size)
{
    uint8 reg, val;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    reg = pa & 0xfc;

    switch(reg) {
#if defined(REV3)
    case TOD_CTRL:
        val = td->flags;
        td->flags &= ~(FLAG_DATA_CHANGED);
        break;
#endif
    case TOD_TSEC:
        val = TOD_BCDH(td->tsec);
        break;
    case TOD_1SEC:
        val = TOD_BCDL(td->sec);
        break;
    case TOD_10SEC:
        val = TOD_BCDH(td->sec);
        break;
    case TOD_1MIN:
        val = TOD_BCDL(td->min);
        break;
    case TOD_10MIN:
        val = TOD_BCDH(td->min);
        break;
    case TOD_1HOUR:
        val = TOD_BCDL(td->hour);
        break;
    case TOD_10HOUR:
        val = TOD_BCDH(td->hour);
        break;
    case TOD_1DAY:
        val = TOD_BCDL(td->day);
        break;
    case TOD_10DAY:
        val = TOD_BCDH(td->day);
        break;
    case TOD_1MON:
        val = TOD_BCDL(td->mon);
        break;
    case TOD_10MON:
        val = TOD_BCDH(td->mon);
        break;
    case TOD_WDAY:
        val = td->wday;
        break;
    case TOD_1YEAR:
#if defined(REV3)
        val = TOD_BCDL(td->year);
#else
        val = td->lyear;
#endif
        break;
#if defined(REV3)
    case TOD_10YEAR:
        val = TOD_BCDH(td->year);
        break;
    case TOD_SET_INT:
        val = td->clkset;
        break;
#endif
    default:
        val = 0;
        break;
    }

    return val;
}

void tod_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg;
    TOD_DATA *td = (TOD_DATA *)(tod_unit.filebuf);

    /* reg = pa - TODBASE; */
    reg = pa & 0xfc;

    switch(reg) {
#if defined(REV3)
    case TOD_CTRL:
        td->ctrl = (uint8) val;
        if (val & CTRL_DISABLE) {
            tod_enabled = FALSE;
            td->tsec = 0;
        } else {
            tod_enabled = TRUE;
        }
#else
    case TOD_TEST:
        /* test mode */
#endif
        break;
    case TOD_TSEC:
        td->tsec = (uint8) val * 10;
        break;
    case TOD_1SEC:
        td->sec = ((td->sec / 10) * 10) + (uint8) val;
        break;
    case TOD_10SEC:
        td->sec = ((uint8) val * 10) + (td->sec % 10);
        break;
    case TOD_1MIN:
        td->min = ((td->min / 10) * 10) + (uint8) val;
        break;
    case TOD_10MIN:
        td->min = ((uint8) val * 10) + (td->min % 10);
        break;
    case TOD_1HOUR:
        td->hour = ((td->hour / 10) * 10) + (uint8) val;
        break;
    case TOD_10HOUR:
        td->hour = ((uint8) val * 10) + (td->hour % 10);
        break;
    case TOD_1DAY:
        td->day = ((td->day / 10) * 10) + (uint8) val;
        break;
    case TOD_10DAY:
        td->day = ((uint8) val * 10) + (td->day % 10);
        break;
    case TOD_1MON:
        td->mon = ((td->mon / 10) * 10) + (uint8) val;
        break;
    case TOD_10MON:
        td->mon = ((uint8) val * 10) + (td->mon % 10);
        break;
    case TOD_1YEAR:
#if defined(REV3)
        td->year = ((td->year / 10) * 10) + (uint8) val;
#else
        td->lyear = (uint8) val;
#endif
        break;
#if defined(REV3)
    case TOD_10YEAR:
        td->year = ((uint8) val * 10) + (td->year % 10);
        break;
    case TOD_SET_INT:
        td->clkset = (uint8) val;
        if (!TOD_12H(td)) {
            /* The AM/PM indicator is always 0 if not in 12H mode */
            td->clkset &= ~(CLKSET_PM);
        }
        td->lyear = (val >> 2) & 3;
        break;
#else
    case TOD_STARTSTOP:
        tod_enabled = val & 1;
        break;
#endif
    case TOD_WDAY:
        td->wday = (uint8)val & 0x7;
        break;
    default:
        break;
    }
}

const char *tod_description(DEVICE *dptr)
{
#if defined(REV3)
    return("MM58274C real time clock");
#else
    return("MM58174A real time clock");
#endif
}

t_stat tod_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    char dname[10];

#if defined(REV3)
    snprintf(dname, 10, "MM58274C");
#else
    snprintf(dname, 10, "MM58174A");
#endif

    fprintf(st, "%s Time-Of-Day Clock (%s)\n\n", dname, dptr->name);
    fprintf(st, "The %s controller simulates a National Semiconductor %s\n", dptr->name, dname);
    fprintf(st, "real time clock. This clock keeps track of the current system time\n");
    fprintf(st, "and date.\n\n");
    fprintf(st, "In order to preserve simulated calendar time between simulator runs,\n");
    fprintf(st, "the %s clock may be attached to a file which stores its state while\n", dptr->name);
    fprintf(st, "the simulator is not running, e.g.:\n\n");
    fprintf(st, "    sim> ATTACH TOD <filename>\n");
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);
    return SCPE_OK;
}

#if defined(REV3)

/*
 * Fault Register
 *
 * The Fault Register is composed of two 32-bit registers at addresses
 * 0x4C000 and 0x4D000. These latch state of the last address to cause
 * a CPU fault.
 *
 *   Bits 00-25: Physical memory address bits
 *
 * Fault Register 2 does double duty. It actually consists of four
 * words, each of which maps to a memory slot on the system board. If
 * occupied, it records the size of memory equipped in the slot, as
 * well as information about any memory faults.
 *
 *
 *     Bits    Active    Purpose
 *    ----------------------------------------------------
 *     31-26     H       Upper Halfword ECC Syndrome Bits
 *     25-20     H       Lower Halfword ECC Syndeome Bits
 *     19        L       I/O Bus or BUB master on Fault
 *     18        H       Invert low addr bit 2 (DWORD1)
 *     17        H       Decrement low addr by 4
 *     16        L       I/O Bus Master on Fault
 *     15        L       CPU accessing I/O Peripheral
 *     14        L       BUB Slot 0 master on fault
 *     13        L       CPU Accessing BUB Peripheral
 *     12-11     H       BUB peripheral accessed by CPU
 *     10        L       BUB Slot 1 master on fault
 *      9        L       BUB Slot 2 master on fault
 *      8        L       BUB Slot 3 master on fault
 *      7-3      N/A     Not Used
 *      2        L       Memory Equipped
 *      1-0      H       Equipped Memory Size
 *
 */

uint32 flt[2] = {0, 0};

UNIT flt_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, 64)
};

REG flt_reg[] = {
    { HRDATAD(FLT1, flt[0], 32, "Fault Register 1") },
    { HRDATAD(FLT2, flt[1], 32, "Fault Register 2") },
    { NULL }
};

DEVICE flt_dev = {
    "FLT", &flt_unit, flt_reg, NULL,
    1, 16, 32, 1, 16, 32,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    &flt_help, NULL, NULL,
    &flt_description
};

/*
 * Return the configured memory size for a given backplane location.
 */
static uint32 mem_size(uint8 slot) {
    switch(MEM_SIZE) {
    case MSIZ_8M:
        if (slot <= 1) {
            return MEM_EQP|MEM_4M;
        } else {
            return 0;
        }
    case MSIZ_16M:
        return MEM_EQP|MEM_4M;
    case MSIZ_32M:
        if (slot <= 1) {
            return MEM_EQP|MEM_16M;
        } else {
            return 0;
        }
    case MSIZ_64M:
        return MEM_EQP|MEM_16M;
    default:
        return 0;
    }
}

uint32 flt_read(uint32 pa, size_t size)
{
    sim_debug(EXECUTE_MSG, &flt_dev,
              "Read from FLT Register at %x\n",
              pa);

    switch(pa) {
    case FLTLBASE:
        return flt[0];
    case FLTHBASE:
        return (flt[1] & FLT_MSK) | mem_size(0);
    case FLTHBASE + 4:
        return (flt[1] & FLT_MSK) | mem_size(1);
    case FLTHBASE + 8:
        return (flt[1] & FLT_MSK) | mem_size(2);
    case FLTHBASE + 12:
        return (flt[1] & FLT_MSK) | mem_size(3);
    default:
        sim_debug(EXECUTE_MSG, &flt_dev,
                  "Read from FLT Register at %x: FAILURE, NO DATA!!!!\n",
                  pa);
        return 0;
    }
}

void flt_write(uint32 pa, uint32 val, size_t size)
{
    sim_debug(EXECUTE_MSG, &flt_dev,
              "Write to FLT Register at %x (val=%x)\n",
              pa, val);

    return;
}

t_stat flt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "Fault Register\n\n");
    fprintf(st, "The %s device is a pair of 32-bit registers that hold information about\n", dptr->name);
    fprintf(st, "system memory faults.\n");
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);
    return SCPE_OK;
}

const char *flt_description(DEVICE *dptr)
{
    return "Fault Register";
}

#endif
