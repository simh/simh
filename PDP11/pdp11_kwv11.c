/* pdp11_kwv11.c: KWV11-A/C programmable real-time clock simulator

   Copyright (c) 2026, Kyle Duren

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   KWV11-A/C Q-bus programmable real-time clock

   The model keeps the live counter private.  A read of BPR returns the preset
   or the value most recently captured by ST2.  The scheduled timer is a
   terminal event, not a simulated callback for every oscillator tick; the
   current counter is derived from the remaining simulator time when needed.

   The device is disabled by default.  Use SET KWV11 ENABLE on a Q-bus CPU;
   autoconfiguration assigns CSR 170420 (physical address 17770420) and
   vectors 0440 and 0444.  SET KWV11 50HZ or SET KWV11 60HZ selects the line
   frequency used by rate 7.

   Reference:
   - DEC ADV11-A, KWV11-A, AAV11-A, DRV11 User's Manual, EK-ADV11-OP-002,
     3rd printing (revised April 1977) (Bitsavers):
     https://bitsavers.org/pdf/dec/qbus/ADV11/EK-ADV11-OP-002_ADV11-A_KWV11-A_AAV11-A_DRV11_Users_Manual_Apr77.pdf
   - DEC KWV11A Diagnostic, MAINDEC-11-CNKWA-A, CNKWAA0, AH-T452A-MC
     (May 1983) (Bitsavers):
     https://www.bitsavers.org/pdf/dec/pdp11/microfiche/Diagnostic_Program_Listings/Listings/CNKWAA0__KWV11__KWV11A_DIAGNOSTIC__AH-T452A-MC__MAY_1983_gray.pdf
*/

#include "pdp11_defs.h"

/* KWV11-A/C register layout.  CSR and BPR occupy the four-byte device
   window; the counter itself is internal to the model and is not a bus
   register.  The manual describes the device and counter on printed pp. 3-1
   and 3-5 (PDF pp. 30 and 34). */

#define UNIT_V_LINE50HZ (UNIT_V_UF + 0)
#define UNIT_LINE50HZ   (1 << UNIT_V_LINE50HZ)

#define CSR_V_GO         0
#define CSR_V_MODE       1
#define CSR_V_RATE       3
#define CSR_V_INTOV      6
#define CSR_V_OVFLO      7
#define CSR_V_MAINT_ST1  8
#define CSR_V_MAINT_ST2  9
#define CSR_V_MAINT_OSC  10
#define CSR_V_DIO        11
#define CSR_V_FOR        12
#define CSR_V_ST2GOE     13
#define CSR_V_INT2       14
#define CSR_V_ST2FLG     15

#define CSR_GO           (1u << CSR_V_GO)
#define CSR_MODE         (03u << CSR_V_MODE)
#define CSR_RATE         (07u << CSR_V_RATE)
#define CSR_INTOV        (1u << CSR_V_INTOV)
#define CSR_OVFLO        (1u << CSR_V_OVFLO)
#define CSR_MAINT_ST1    (1u << CSR_V_MAINT_ST1)
#define CSR_MAINT_ST2    (1u << CSR_V_MAINT_ST2)
#define CSR_MAINT_OSC   (1u << CSR_V_MAINT_OSC)
#define CSR_DIO          (1u << CSR_V_DIO)
#define CSR_FOR          (1u << CSR_V_FOR)
#define CSR_ST2GOE       (1u << CSR_V_ST2GOE)
#define CSR_INT2         (1u << CSR_V_INT2)
#define CSR_ST2FLG       (1u << CSR_V_ST2FLG)

#define CSR_GETMODE(x)   (((x) >> CSR_V_MODE) & 03)
#define CSR_GETRATE(x)   (((x) >> CSR_V_RATE) & 07)

/* Maintenance inputs are write-only pulses.  The status and maintenance
   bits are consequently not included in the writable CSR mask.  See Table
   3-1, printed pp. 3-11--3-12 (PDF pp. 40--41). */
#define CSR_WRMASK (CSR_GO | CSR_MODE | CSR_RATE | CSR_INTOV | CSR_DIO | \
                    CSR_ST2GOE | CSR_INT2)
#define CSR_RDMASK (0xFFFFu & ~(CSR_MAINT_ST1 | CSR_MAINT_ST2 | CSR_MAINT_OSC))

/* Rate 6 is the external ST1 input.  Its rate is zero here because it is
   advanced by the maintenance pulse path rather than by the timer unit.
   The complete rate and mode encoding is in Table 3-1, printed p. 3-12
   (PDF p. 41). */
static const char *kwv11_rates[] = {
    "Stop", "1MHz", "100kHz", "10kHz", "1kHz", "100Hz", "ST1 ext", "Line"
};

static const char *kwv11_modes[] = {
    "Single", "Repeat", "ExtEvent", "ExtEventZero"
};

static uint32 kwv11_rate_tps[8] = { 0, 1000000, 100000, 10000, 1000, 100, 0, 50 };
static uint32 kwv11_rate_usec[8] = { 0, 1, 10, 100, 1000, 10000, 0, 20000 };

static BITFIELD kwv11_csr_bits[] = {
    BIT(GO), BITFNAM(MODE, 2, kwv11_modes), BITFNAM(RATE, 3, kwv11_rates),
    BIT(INTOV), BIT(OVFLO), BITNC, BITNC, BITNC, BIT(DIO),
    BIT(FOR), BIT(ST2GOE), BIT(INT2), BIT(ST2FLG), ENDBITS
};

static uint32 kwv11_csr;
static uint32 kwv11_bpr;
static uint32 kwv11_ctr;
static uint32 kwv11_run_ctr;
static uint32 kwv11_run_ticks;
static uint32 kwv11_tick_usec;
static double kwv11_run_usec;

static void kwv11_update_ints (void);
static void kwv11_schedule (void);
static void kwv11_update_ctr (void);
static void kwv11_overflow (void);
static void kwv11_st1_pulse (void);
static void kwv11_st2_pulse (void);

/* Device entry points and interrupt acknowledge callbacks. */
t_stat kwv11_rd (int32 *data, int32 PA, int32 access);
t_stat kwv11_wr (int32 data, int32 PA, int32 access);
t_stat kwv11_svc (UNIT *uptr);
t_stat kwv11_reset (DEVICE *dptr);
t_stat kwv11_set_line (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat kwv11_show_rate (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *kwv11_description (DEVICE *dptr);

static int32 kwv11_ovf_inta (void);
static int32 kwv11_st2_inta (void);

#define IOLN_KWV11 004

DIB kwv11_dib = {
    IOBA_AUTO, IOLN_KWV11, &kwv11_rd, &kwv11_wr,
    /* The first vector is overflow; the second is ST2 capture.  The manual
       specifies the vector pair on printed pp. 3-3 and 3-8 (PDF pp. 32 and
       37). */
    2, IVCL(KWV11), VEC_AUTO, { &kwv11_ovf_inta, &kwv11_st2_inta }
};

UNIT kwv11_unit = { UDATA (&kwv11_svc, UNIT_IDLE, 0) };

REG kwv11_reg[] = {
    { ORDATADF(CSR, kwv11_csr, 16, "control/status register", kwv11_csr_bits) },
    { ORDATAD(BPR, kwv11_bpr, 16, "buffer/preset register") },
    { FLDATA(OVFLO, kwv11_csr, CSR_V_OVFLO) },
    { FLDATA(ST2FLG, kwv11_csr, CSR_V_ST2FLG) },
    { FLDATA(INTOV, kwv11_csr, CSR_V_INTOV) },
    { FLDATA(INT2, kwv11_csr, CSR_V_INT2) },
    { FLDATA(GO, kwv11_csr, CSR_V_GO) },
    { BRDATA(TPS, kwv11_rate_tps, 10, 32, 8), REG_NZ + PV_LEFT },
    { ORDATA(DEVADDR, kwv11_dib.ba, 32), REG_HRO },
    { ORDATA(DEVVEC, kwv11_dib.vec, 16), REG_HRO },
    { NULL }
};

MTAB kwv11_mod[] = {
    { UNIT_LINE50HZ, UNIT_LINE50HZ, "50 Hz line frequency", "50HZ", &kwv11_set_line },
    { UNIT_LINE50HZ, 0, "60 Hz line frequency", "60HZ", &kwv11_set_line },
    { MTAB_XTD|MTAB_VDV, 0, "RATE", NULL, NULL, &kwv11_show_rate, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL, NULL, NULL, &show_addr },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR", &set_vec, &show_vec, NULL },
    { 0 }
};

#define DBG_REG   0x01
#define DBG_TICK  0x02
#define DBG_SCHED 0x04
#define DBG_INT   0x08

DEBTAB kwv11_deb[] = {
    { "REG", DBG_REG, "Register access" },
    { "TICK", DBG_TICK, "Counter terminal events" },
    { "SCHED", DBG_SCHED, "Timer scheduling" },
    { "INT", DBG_INT, "Interrupts" },
    { NULL, 0 }
};

DEVICE kwv11_dev = {
    "KWV11", &kwv11_unit, kwv11_reg, kwv11_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &kwv11_reset,
    NULL, NULL, NULL, &kwv11_dib,
    DEV_DEBUG | DEV_DISABLE | DEV_DIS | DEV_QBUS,
    0, kwv11_deb, NULL, NULL, NULL, NULL, NULL, &kwv11_description
};

/* Both interrupt requests are level-derived from the CSR.  Keeping this in
   one place ensures that register reads, maintenance pulses, and timer
   events all apply the same interrupt-enable and status-bit rules. */
static void kwv11_update_ints (void)
{
    if ((kwv11_csr & CSR_OVFLO) && (kwv11_csr & CSR_INTOV))
        SET_INT(KWV11);
    else
        CLR_INT(KWV11);

    if ((kwv11_csr & CSR_ST2FLG) && (kwv11_csr & CSR_INT2))
        SET_INT(KWV11_ST2);
    else
        CLR_INT(KWV11_ST2);
}

static void kwv11_update_ctr (void)
{
    double remain, elapsed;
    uint32 ticks;

    /* The timer is scheduled only for the terminal count event.  Reconstruct
       the intervening ticks when software examines or changes a register. */
    if (!sim_is_active (&kwv11_unit) || kwv11_tick_usec == 0)
        return;

    remain = sim_activate_time_usecs (&kwv11_unit);
    elapsed = kwv11_run_usec - remain;
    if (elapsed <= 0)
        return;
    ticks = (uint32)(elapsed / kwv11_tick_usec);
    if (ticks > kwv11_run_ticks)
        ticks = kwv11_run_ticks;
    kwv11_ctr = (kwv11_run_ctr + ticks) & 0xFFFF;
}

static void kwv11_schedule (void)
{
    uint32 rate, ticks;

    /* Cancel before recomputing so rate, mode, DIO, and counter changes cannot
       leave an obsolete terminal event queued. */
    sim_cancel (&kwv11_unit);
    kwv11_tick_usec = 0;
    kwv11_run_usec = 0;
    kwv11_run_ticks = 0;

    if ((kwv11_csr & CSR_GO) == 0)
        return;

    rate = CSR_GETRATE (kwv11_csr);
    if (rate == 0 || rate == 6 || (rate < 6 && (kwv11_csr & CSR_DIO)))
        return;

    kwv11_tick_usec = kwv11_rate_usec[rate];
    if (kwv11_tick_usec == 0)
        return;

    ticks = 0x10000 - (kwv11_ctr & 0xFFFF);
    if (ticks == 0)
        ticks = 0x10000;
    kwv11_run_ctr = kwv11_ctr;
    kwv11_run_ticks = ticks;
    kwv11_run_usec = (double)ticks * kwv11_tick_usec;
    sim_debug (DBG_SCHED, &kwv11_dev, "schedule %u ticks, %.0f usecs\n",
               ticks, kwv11_run_usec);
    sim_activate_after_d (&kwv11_unit, kwv11_run_usec);
}

static void kwv11_overflow (void)
{
    uint32 mode = CSR_GETMODE (kwv11_csr);

    /* A second overflow before software clears OVFLO sets FOR (failure).
       This is the flag-overrun behavior described on printed p. 3-5 (PDF
       p. 34). */
    kwv11_ctr = 0;
    if (kwv11_csr & CSR_OVFLO)
        kwv11_csr |= CSR_FOR;
    kwv11_csr |= CSR_OVFLO;

    if (mode == 1)
        kwv11_ctr = kwv11_bpr;
    else
        kwv11_csr &= ~CSR_GO;

    kwv11_update_ints ();
}

static void kwv11_st1_pulse (void)
{
    /* ST1 is both the external clock source and the maintenance single-step
       input.  It is ignored unless the counter is running.  See the rate
       and maintenance descriptions on printed pp. 3-5 and 3-12 (PDF pp. 34
       and 41). */
    if ((kwv11_csr & CSR_GO) == 0)
        return;
    kwv11_update_ctr ();
    kwv11_ctr = (kwv11_ctr + 1) & 0xFFFF;
    if (kwv11_ctr == 0)
        kwv11_overflow ();
    kwv11_schedule ();
}

static void kwv11_st2_pulse (void)
{
    uint32 mode;

    /* ST2 captures the current counter into BPR.  In mode 3 it also starts
       the next interval from zero; ST2GOE defers starting the counter until
       the first ST2 event.  The mode behavior is specified on printed pp.
       3-5 and 3-13 (PDF pp. 34 and 42). */
    if ((kwv11_csr & (CSR_GO | CSR_ST2GOE)) == 0)
        return;
    kwv11_update_ctr ();
    if (kwv11_csr & CSR_ST2FLG)
        kwv11_csr |= CSR_FOR;
    kwv11_bpr = kwv11_ctr;
    kwv11_csr |= CSR_ST2FLG;
    mode = CSR_GETMODE (kwv11_csr);
    if (mode == 3)
        kwv11_ctr = 0;
    if (kwv11_csr & CSR_ST2GOE) {
        kwv11_csr |= CSR_GO;
        kwv11_csr &= ~CSR_ST2GOE;
    }
    kwv11_update_ints ();
    kwv11_schedule ();
}

/* Register access.  Reading CSR acknowledges/clears the latched status;
   reading BPR returns the preset or the most recent ST2 capture.  Register
   access and BPR restrictions are described on printed pp. 3-5 and 3-9
   (PDF pp. 34 and 38). */
t_stat kwv11_rd (int32 *data, int32 PA, int32 access)
{
    kwv11_update_ctr ();
    if (((PA >> 1) & 1) == 0) {
        *data = kwv11_csr & CSR_RDMASK;
        kwv11_csr &= ~(CSR_ST2FLG | CSR_OVFLO | CSR_FOR);
        kwv11_update_ints ();
    } else {
        *data = kwv11_bpr & 0xFFFF;
    }
    sim_debug (DBG_REG, &kwv11_dev, "read %06o -> %06o\n", PA, *data);
    return SCPE_OK;
}

/* CSR writes merge byte accesses before applying the writable mask.  BPR is
   different: the manual specifies it as a 16-bit, word-oriented register and
   says that an attempted byte write results in a whole word being written
   (printed p. 3-9, PDF p. 38).  The DEC KWV11 diagnostics agree: their BPR
   pattern tests use word MOV instructions (CVKWAC0, printed pp. 2-3;
   CNKWAA0, printed pp. 58-59), while their explicit MOVB tests exercise CSR
   byte-lane isolation (CVKWAC0, printed p. 5; CNKWAA0, printed pp. 60-61).
   Model the BPR byte access as a whole-word write,
   placing the supplied byte on the addressed bus lane; do not merge it with
   the previous BPR value. */
t_stat kwv11_wr (int32 data, int32 PA, int32 access)
{
    uint32 old_csr = kwv11_csr;
    uint32 value;

    kwv11_update_ctr ();
    if (((PA >> 1) & 1) == 0) {
        if (access == WRITEB) {
            uint32 mask = (PA & 1) ? 0xFF00 : 0x00FF;
            value = (uint32)data << ((PA & 1) ? 8 : 0);
            data = (int32)((old_csr & ~mask) | (value & mask));
        }
        kwv11_csr = (old_csr & ~CSR_WRMASK) | ((uint32)data & CSR_WRMASK);
        if (data & CSR_ST2FLG)
            kwv11_csr &= ~CSR_ST2FLG;
        if (data & CSR_OVFLO)
            kwv11_csr &= ~CSR_OVFLO;
        if (data & CSR_FOR)
            kwv11_csr &= ~CSR_FOR;

        if (data & CSR_MAINT_ST1)
            kwv11_st1_pulse ();
        if (data & CSR_MAINT_ST2)
            kwv11_st2_pulse ();
        if ((data & CSR_MAINT_OSC) && !(kwv11_csr & CSR_DIO))
            kwv11_st1_pulse ();

        if ((old_csr & CSR_GO) == 0 && (kwv11_csr & CSR_GO)) {
            if (CSR_GETMODE (kwv11_csr) >= 2)
                kwv11_ctr = 0;
            else
                kwv11_ctr = kwv11_bpr;
        }
        if ((kwv11_csr & CSR_GO) == 0 ||
            CSR_GETRATE (old_csr) != CSR_GETRATE (kwv11_csr) ||
            CSR_GETMODE (old_csr) != CSR_GETMODE (kwv11_csr) ||
            ((old_csr ^ kwv11_csr) & CSR_DIO))
            kwv11_schedule ();
        else
            kwv11_update_ints ();
    } else {
        if (access == WRITEB) {
            data = (PA & 1) ? data << 8 : data;
        }
        kwv11_bpr = (uint32)data & 0xFFFF;
        if ((kwv11_csr & CSR_GO) == 0 && CSR_GETMODE (kwv11_csr) < 2)
            kwv11_ctr = kwv11_bpr;
        if (kwv11_csr & CSR_GO)
            kwv11_schedule ();
    }
    return SCPE_OK;
}

/* The service callback represents a counter terminal event, not an
   individual oscillator tick. */
t_stat kwv11_svc (UNIT *uptr)
{
    sim_debug (DBG_TICK, &kwv11_dev, "terminal counter event at %06o\n", kwv11_ctr);
    if ((kwv11_csr & CSR_GO) == 0)
        return SCPE_OK;
    kwv11_overflow ();
    kwv11_schedule ();
    return SCPE_OK;
}

/* Interrupt acknowledge callbacks select the vector for the pending source.
   The request is dropped here; the CSR status remains latched until CSR is
   read or explicitly cleared by software. */
static int32 kwv11_ovf_inta (void)
{
    if ((kwv11_csr & CSR_OVFLO) && (kwv11_csr & CSR_INTOV)) {
        CLR_INT (KWV11);
        return kwv11_dib.vec;
    }
    return 0;
}

static int32 kwv11_st2_inta (void)
{
    if ((kwv11_csr & CSR_ST2FLG) && (kwv11_csr & CSR_INT2)) {
        CLR_INT (KWV11_ST2);
        return kwv11_dib.vec + 4;
    }
    return 0;
}

/* Reset clears device state and lets Q-bus autoconfiguration assign the
   fixed KWV11 CSR and vector pair.  The recommended CSR and vector settings
   appear on printed pp. 3-7--3-8 (PDF pp. 36--37). */
t_stat kwv11_reset (DEVICE *dptr)
{
    kwv11_csr = 0;
    kwv11_bpr = 0;
    kwv11_ctr = 0;
    kwv11_run_ctr = 0;
    kwv11_run_ticks = 0;
    kwv11_tick_usec = 0;
    kwv11_run_usec = 0;
    sim_cancel (&kwv11_unit);
    CLR_INT (KWV11);
    CLR_INT (KWV11_ST2);
    return auto_config (0, 0);
}

/* SET KWV11 50HZ/60HZ changes only the line-frequency rate entry. */
t_stat kwv11_set_line (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (val == UNIT_LINE50HZ) {
        kwv11_rate_tps[7] = 50;
        kwv11_rate_usec[7] = 20000;
    } else {
        kwv11_rate_tps[7] = 60;
        kwv11_rate_usec[7] = 16667;
    }
    if (CSR_GETRATE (kwv11_csr) == 7)
        kwv11_schedule ();
    return SCPE_OK;
}

/* SHOW KWV11 RATE reports the selected source and its effective frequency. */
t_stat kwv11_show_rate (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 rate = CSR_GETRATE (kwv11_csr);
    fprintf (st, "%s", kwv11_rates[rate]);
    if (rate == 7)
        fprintf (st, " (%uHz)", kwv11_rate_tps[7]);
    return SCPE_OK;
}

const char *kwv11_description (DEVICE *dptr)
{
    return "KWV11-A/C programmable real-time clock";
}
