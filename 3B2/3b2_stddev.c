/* 3b2_stddev.c: AT&T 3B2 miscellaneous system board devices.

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

   - nvram       Non-Volatile RAM
   - tod         MM58174A Real-Time-Clock
   - flt         Fault Register
*/

#include "3b2_stddev.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"

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
 * MM58174A Time Of Day Clock
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

#if defined(REV3)

/*
 * Fault Register
 *
 * The Fault Register is composed of two 32-bit registers at addresses
 * 0x4C000 and 0x4D000. These latch state of the last address to cause
 * a CPU fault.
 *
 *   Bits 00-25: Physical memory address bits 00-25
 */

uint32 flt_1 = 0;
uint32 flt_2 = 0;

UNIT flt_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, 8)
};

REG flt_reg[] = {
    { NULL }
};

DEVICE flt_dev = {
    "FLT", &flt_unit, flt_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    NULL, NULL, NULL,
    NULL
};

uint32 flt_read(uint32 pa, size_t size)
{
    sim_debug(READ_MSG, &flt_dev,
              "[%08x] Read from FLT Register at %x\n",
              R[NUM_PC], pa);
    return 0;
}

void flt_write(uint32 pa, uint32 val, size_t size)
{
    sim_debug(WRITE_MSG, &flt_dev,
              "[%08x] Write to FLT Register at %x (val=%x)\n",
              R[NUM_PC], pa, val);
    return;
}

#endif
