/* sel32_clk.c: SEL 32 Class F IOP processor RTOM functions.

   Copyright (c) 2018-2021, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This module support the real-time clock and the interval timer.
   These are CD/TD class 3 devices.  The RTC can be programmed to
   50/100 HZ or 60/120 HZ rates and creates an interrupt at the
   requested rate.  The interval timer is a 32 bit register that is
   loaded with a value to be down counted.  An interrupt is generated
   when the count reaches zero,  The clock continues down counting
   until read/reset by the programmer.  The rate can be external or
   38.4 microseconds per count.
*/

#include "sel32_defs.h"

#if NUM_DEVS_RTOM > 0

#define UNIT_CLK UNIT_IDLE|UNIT_DISABLE

void rtc_setup (uint32 ss, uint32 level);
t_stat rtc_srv (UNIT *uptr);
t_stat rtc_reset (DEVICE *dptr);
t_stat rtc_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rtc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat rtc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char *rtc_desc(DEVICE *dptr);

extern int  irq_pend;                       /* go scan for pending int or I/O */
extern uint32 INTS[];                       /* interrupt control flags */
extern uint32 SPAD[];                       /* computer SPAD */
extern uint32 M[];                          /* system memory */
extern uint32  outbusy;                     /* output waiting on timeout */
extern uint32  inbusy;                      /* input waiting on timeout */

int32 rtc_pie = 0;                          /* rtc pulse ie */
int32 rtc_tps = 60;                         /* rtc ticks/sec */
int32 rtc_lvl = 0x18;                       /* rtc interrupt level */

/* Clock data structures
   rtc_dev      RTC device descriptor
   rtc_unit     RTC unit
   rtc_reg      RTC register list
*/

/* clock can be enabled / disabled */
/* default to 60 HZ RTC */
//718UNIT rtc_unit = { UDATA (&rtc_srv, UNIT_IDLE, 0), 16666, UNIT_ADDR(0x7F06)};
UNIT rtc_unit = { UDATA (&rtc_srv, UNIT_CLK, 0), 16666, UNIT_ADDR(0x7F06)};

REG rtc_reg[] = {
    { FLDATA (PIE, rtc_pie, 0) },
    { DRDATA (TIME, rtc_unit.wait, 32), REG_NZ + PV_LEFT },
    { DRDATA (TPS, rtc_tps, 8), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB rtc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 100, NULL, "100HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 120, NULL, "120HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &rtc_show_freq, NULL },
    { 0 }
    };

DEVICE rtc_dev = {
    "RTC", &rtc_unit, rtc_reg, rtc_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &rtc_reset,                 /* examine, deposit, reset */
    NULL, NULL, NULL,                       /* boot, attach, detach */
    /* dib, dev flags, debug flags, debug */
    NULL, DEV_DEBUG|DEV_DIS|DEV_DISABLE, 0, dev_debug,
    NULL, NULL, &rtc_help,                  /* ?, ?, help */
    NULL, NULL, &rtc_desc,                  /* ?, ?, description */
    };

/* The real time clock runs continuously; therefore, it only has
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock signal from simulator */
t_stat rtc_srv (UNIT *uptr)
{
#ifdef STOP_CLOCK_INTS_FOR_DEXP_TEST_DEBUGGING
    /* stop clock interrupts for dexp debugging */
    rtc_pie = 0;
#endif
    /* if clock disabled, do not do interrupts */
    if (((rtc_dev.flags & DEV_DIS) == 0) && rtc_pie) {
        int lev = 0x13;
        sim_debug(DEBUG_CMD, &rtc_dev,
            "RT Clock mfp INTS[%02x] %08x SPAD[%02x] %08x\n",
            lev, INTS[lev], lev+0x80, SPAD[lev+0x80]);
        sim_debug(DEBUG_CMD, &rtc_dev,
            "RT Clock int INTS[%02x] %08x SPAD[%02x] %08x\n",
            rtc_lvl, INTS[rtc_lvl], rtc_lvl+0x80, SPAD[rtc_lvl+0x80]);
        if (((INTS[rtc_lvl] & INTS_ENAB) ||     /* make sure enabled */
            (SPAD[rtc_lvl+0x80] & SINT_ENAB)) &&    /* in spad too */
            (((INTS[rtc_lvl] & INTS_ACT) == 0) ||   /* and not active */
            ((SPAD[rtc_lvl+0x80] & SINT_ACT) == 0))) { /* in spad too */
#if 0
            /* HACK for console I/O stopping */
            /* This reduces the number of console I/O stopping errors */
            /* need to find real cause of I/O stopping on clock interrupt */
            if ((outbusy==0) && (inbusy==0))    /* skip interrupt if con I/O in busy wait */
                INTS[rtc_lvl] |= INTS_REQ;      /* request the interrupt */
            else
            sim_debug(DEBUG_CMD, &rtc_dev,
                "RT Clock int console busy\n");
#else
            INTS[rtc_lvl] |= INTS_REQ;          /* request the interrupt */
#endif
            irq_pend = 1;                       /* make sure we scan for int */
        }
        sim_debug(DEBUG_CMD, &rtc_dev,
            "RT Clock int INTS[%02x] %08x SPAD[%02x] %08x\n",
            rtc_lvl, INTS[rtc_lvl], rtc_lvl+0x80, SPAD[rtc_lvl+0x80]);
    }
//  temp = sim_rtcn_calb(rtc_tps, TMR_RTC); /* timer 0 for RTC */
    sim_rtcn_calb(rtc_tps, TMR_RTC);        /* timer 0 for RTC */
    sim_activate_after(uptr, 1000000/rtc_tps);  /* reactivate 16666 tics / sec */
    return SCPE_OK;
}

/* Clock interrupt start/stop */
/* ss = 1 - starting clock */
/* ss = 0 - stopping clock */
/* level = interrupt level */
void rtc_setup(uint32 ss, uint32 level)
{
    uint32 addr = SPAD[0xf1] + (level<<2);  /* vector address in SPAD */

    rtc_lvl = level;                        /* save the interrupt level */
    addr = M[addr>>2];                      /* get the interrupt context block addr */
    if (ss == 1) {                          /* starting? */
        INTS[level] |= INTS_ENAB;           /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;      /* in spad too */
        sim_activate(&rtc_unit, 20);        /* start us off */
        sim_debug(DEBUG_CMD, &rtc_dev,
            "RT Clock setup enable int %02x rtc_pie %01x ss %01x\n",
            rtc_lvl, rtc_pie, ss);
    } else {
        INTS[level] &= ~INTS_ENAB;          /* make sure disabled */
        SPAD[level+0x80] &= ~SINT_ENAB;     /* in spad too */
        INTS[level] &= ~INTS_ACT;           /* make sure request not active */
        SPAD[level+0x80] &= ~SINT_ACT;      /* in spad too */
        sim_debug(DEBUG_CMD, &rtc_dev,
            "RT Clock setup disable int %02x rtc_pie %01x ss %01x\n",
            rtc_lvl, rtc_pie, ss);
    }
    rtc_pie = ss;                           /* set new state */
}

/* Clock reset */
t_stat rtc_reset(DEVICE *dptr)
{
    rtc_pie = 0;                            /* disable pulse */
    /* initialize clock calibration */
    sim_activate (&rtc_unit, rtc_unit.wait);    /* activate unit */
    return SCPE_OK;
}

/* Set frequency */
t_stat rtc_set_freq(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (cptr)                               /* if chars, bad */
        return SCPE_ARG;                    /* ARG error */
    if ((val != 50) && (val != 60) && (val != 100) && (val != 120))
        return SCPE_IERR;                   /* scope error */
    rtc_tps = val;                          /* set the new frequency */
    return SCPE_OK;                         /* we done */
}

/* Show frequency */
t_stat rtc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    /* print the current frequency setting */
    if (rtc_tps < 100)
        fprintf (st, (rtc_tps == 50)? "50Hz": "60Hz");
    else
        fprintf (st, (rtc_tps == 100)? "100Hz": "120Hz");
    return SCPE_OK;
}

/* sho help rtc */
t_stat rtc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
    fprintf(st, "SEL 32 IOP/MFP realtime clock at 0x7F06\r\n");
    fprintf(st, "Use:\r\n");
    fprintf(st, "    sim> SET RTC [50][60][100][120]\r\n");
    fprintf(st, "to set clock interrupt rate in HZ\r\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

/* device description */
const char *rtc_desc(DEVICE *dptr)
{
    return "SEL IOP/MFP realtime clock @ address 0x7F06";
}

/************************************************************************/

/* Interval Timer support */
int32 itm_src = 0;                          /* itm source freq 0=itm 1=rtc */
int32 itm_pie = 0;                          /* itm pulse enable */
int32 itm_run = 0;                          /* itm is running */
int32 itm_cmd = 0;                          /* itm last user cmd */
int32 itm_cnt = 0;                          /* itm reload pulse count */
int32 itm_tick_size_x_100 = 3840;           /* itm 26042 ticks/sec = 38.4 us per tic */
int32 itm_lvl = 0x5f;                       /* itm interrupt level */
int32 itm_strt = 0;                         /* clock start time in usec */
int32 itm_load = 0;                         /* clock loaded */
int32 itm_big = 26042 * 6000;               /* about 100 minutes */
t_stat itm_srv (UNIT *uptr);
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat itm_reset (DEVICE *dptr);
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat itm_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char *itm_desc(DEVICE *dptr);

/* Clock data structures
   itm_dev      Interval Timer ITM device descriptor
   itm_unit     Interval Timer ITM unit
   itm_reg      Interval Timer ITM register list
*/

/* Mark suggested I remove the UNIT_IDLE flag from ITM.  This causes SEL32 */
/* to use 100% of the CPU instead of waiting and running 10% cpu usage */
//BAD Mark UNIT itm_unit = { UDATA (&itm_srv, UNIT_IDLE, 0), 26042, UNIT_ADDR(0x7F04)};
UNIT itm_unit = { UDATA (&itm_srv, 0, 0), 26042, UNIT_ADDR(0x7F04)};

REG itm_reg[] = {
    { FLDATA (PIE, itm_pie, 0) },
    { FLDATA (CNT, itm_cnt, 0) },
    { FLDATA (CMD, itm_cmd, 0) },
    { DRDATA (TICK_SIZE, itm_tick_size_x_100, 32), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB itm_mod[] = {
    { MTAB_XTD|MTAB_VDV, 3840, NULL, "3840us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 7680, NULL, "7680us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "RESOLUTION", NULL,
      NULL, &itm_show_freq, NULL },
    { 0 }
    };

DEVICE itm_dev = {
    "ITM", &itm_unit, itm_reg, itm_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &itm_reset,                 /* examine, deposit, reset */
    NULL, NULL, NULL,                       /* boot, attach, detach */
    /* dib, dev flags, debug flags, debug */
//  NULL, DEV_DEBUG|DEV_DIS|DEV_DISABLE, 0, dev_debug,
    NULL, DEV_DEBUG, 0, dev_debug,          /* dib, dev flags, debug flags, debug */
    NULL, NULL, &itm_help,                  /* ?, ?, help */
    NULL, NULL, &itm_desc,                  /* ?, ?, description */
    };

/* The interval timer downcounts the value it is loaded with and
   runs continuously; therefore, it has a read/write routine,
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock expiration from simulator */
/* cause interrupt */
t_stat itm_srv (UNIT *uptr)
{
    if (itm_pie) {                          /* interrupt enabled? */
        time_t result = time(NULL);
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv Timer expired status %08x lev %02x cnt %x @ time %08x\n",
            INTS[itm_lvl], itm_lvl, itm_cnt, (uint32)result);
        if (((INTS[itm_lvl] & INTS_ENAB) || /* make sure enabled */
            (SPAD[itm_lvl+0x80] & SINT_ENAB)) &&    /* in spad too */
            (((INTS[itm_lvl] & INTS_ACT) == 0) ||   /* and not active */
            ((SPAD[itm_lvl+0x80] & SINT_ACT) == 0))) { /* in spad too */
            INTS[itm_lvl] |= INTS_REQ;      /* request the interrupt */
            irq_pend = 1;                   /* make sure we scan for int */
        }
        sim_cancel (&itm_unit);             /* cancel current timer */
        itm_run = 0;                        /* timer is no longer running */
        /* if cmd BIT29 is set, reload & restart */
        if ((INTS[itm_lvl] & INTS_ENAB) && (itm_cmd & 0x04) && (itm_cnt != 0)) {
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv Timer reload on expired int %02x value %08x src %x\n",
                itm_lvl, itm_cnt, itm_src);
            /* restart timer with value from user */
            if (itm_src)                    /* use specified src freq */
                sim_activate_after_abs_d(&itm_unit, ((double)itm_cnt*350000)/rtc_tps);
//DIAG          sim_activate_after_abs_d(&itm_unit, ((double)itm_cnt*400000)/rtc_tps);
//DIAG          sim_activate_after_abs_d(&itm_unit, ((double)itm_cnt*1000000)/rtc_tps);
            else
                sim_activate_after_abs_d(&itm_unit, ((double)itm_cnt*itm_tick_size_x_100)/100.0);
            itm_run = 1;                    /* show timer running */
            itm_load = itm_cnt;             /* save loaded value */
            itm_strt = 0;                   /* no negative start time */
        } else {
            int32 cnt = itm_big;            /* 0x65ba TRY 1,000,000/38.4 10 secs */
            itm_strt = cnt;                 /* get negative start time */
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv Timer reload for neg cnts on expired int %02x value %08x src %x\n",
                itm_lvl, cnt, itm_src);
            /* restart timer with large value for negative timer value simulation */
            if (itm_src)                    /* use specified src freq */
                sim_activate_after_abs_d(&itm_unit, ((double)cnt*1000000)/rtc_tps);
            else
                sim_activate_after_abs_d(&itm_unit, ((double)cnt*itm_tick_size_x_100) / 100.0);
            itm_run = 1;                    /* show timer running */
            itm_load = cnt;                 /* save loaded value */
        }
    }
    return SCPE_OK;
}

/* ITM read/load function called from CD command processing */
/* cmd bit assignments */
/* 0x40 = BIT25 = Read ITM value into R0 at anythime */
/* 0x20 = BIT26 = Program ITM and BIT27-BIT31 are valid */
/* 0x10 = BIT27 = =1 start timer, =0 stop timer */
/* 0x08 = BIT28 = =1 store R0 into ITM, =0 do not alter clock value */
/* 0x04 = BIT29 = =1 generate multiple ints on countdown to 0, reload start value */
/*                =0 generate single int on countdown to 0, continue counting negative */
/* 0x02 = BIT30 = BIT30 = 0 BIT31 = 0 = use jumpered clock frequency */
/* 0x01 = BIT31 = BIT30 = 0 BIT31 = 1 = use jumpered clock frequency */
/*              = BIT30 = 1 BIT31 = 0 = use RT clock frequency 50/60/100/120 HZ */
/*              = BIT30 = 1 BIT31 = 1 = use external clock frequency */
/* level = interrupt level */
/* cmd = 0x20 stop timer, do not transfer any value */
/*     = 0x39 load and enable interval timer, no return value */
/*     = 0x3d load and enable interval timer, countdown to zero, interrupt and reload */
/*     = 0x40 read timer value */
/*     = 0x60 read timer value and stop timer */
/*     = 0x79 read/reload and start timer */
/* cnt = value to write to timer */
/* ret = return value read from timer */
int32 itm_rdwr(uint32 cmd, int32 cnt, uint32 level)
{
    uint32  temp;

    cmd &= 0x7f;                            /* just need the cmd */
    itm_cmd = cmd;                          /* save last cmd */
    switch (cmd) {
    case 0x20:                              /* stop timer */
        /* stop the timer and save the curr value for later */
        temp = itm_load;                    /* use last loaded value */
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%2x kill value %08x (%08d) itm_load %08x\n",
            cmd, cnt, cnt, temp);
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x temp value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
        }
        sim_cancel (&itm_unit);             /* cancel itc */
        itm_run = 0;                        /* timer is not running */
        itm_cnt = 0;                        /* no count reset value */
        itm_load = temp;                    /* last loaded value */
        itm_strt = 0;                       /* not restarted neg */
        return 0;                           /* does not matter, no value returned  */
        break;

    case 0x29:                              /* load new value and start lo rate */
    case 0x28:                              /* load new value and start hi rate */
    case 0x2a:                              /* load new value and use RTC */
    case 0x2b:                              /* load new value and start hi rate */
    case 0x38:                              /* load new value and start hi rate */
    case 0x39:                              /* load new value and start lo rate */
    case 0x3a:                              /* load new value and start hi rate */
    case 0x3b:                              /* load new value and start lo rate */
        if (itm_run)                        /* if we were running stop timer */
            sim_cancel (&itm_unit);         /* cancel timer */
        itm_run = 0;                        /* stop timer running */
        if (cmd & 0x10) {                   /* clock to start? */
            /* start timer with value from user */
            /* if bits 30-31 == 20, use RTC freq */
            itm_src = (cmd>>1)&1;           /* set src */
            if (itm_src)                    /* use specified src freq */
                /* use clock frequency */
                sim_activate_after_abs_d(&itm_unit, ((double)cnt*1000000)/rtc_tps);
            else {
                /* use interval timer freq */
#ifdef MAYBE_CHANGE_FOR_MPX3X
                /* tsm does not run if fake time cnt is used */
///             if (cnt == 0)
///                 cnt = 0x52f0;
                /* this fixes an extra interrupt being generated on context switch */
                /* the value is load for the new task anyway */
                /* need to verify that UTX likes it too */
/*4MPX3X*/      sim_activate_after_abs_d(&itm_unit, ((double)(cnt+1)*itm_tick_size_x_100)/100.0);
#else
                sim_activate_after_abs_d(&itm_unit, ((double)cnt*itm_tick_size_x_100)/100.0);
#endif
            }
            itm_run = 1;                    /* set timer running */
        }
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x init value %08x (%08d)\n", cmd, cnt, cnt);
        itm_cnt = 0;                        /* no count reset value */
        itm_load = cnt;                     /* now loaded */
        itm_strt = 0;                       /* not restarted neg */
        return 0;                           /* does not matter, no value returned  */
        break;

    case 0x70:                              /* start timer with curr value*/
    case 0x71:                              /* start timer with curr value */
    case 0x72:                              /* start timer with RTC value*/
    case 0x74:                              /* start timer with curr value*/
    case 0x75:                              /* start timer with curr value */
    case 0x76:                              /* start timer with RTC value*/
    case 0x30:                              /* start timer with curr value*/
    case 0x31:                              /* start timer with curr value*/
    case 0x32:                              /* start timer with RTC value*/
    case 0x34:                              /* start timer with curr value*/
    case 0x35:                              /* start timer with curr value */
    case 0x36:                              /* start timer with RTC value*/
    case 0x37:                              /* start timer with curr value */
        temp = itm_load;                    /* get last loaded value */
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x temp value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
            sim_cancel (&itm_unit);         /* cancel timer */
        }
        /* start timer with current or user value, reload on zero time */
        cnt = temp;                         /* use current value */
        /* if bits 30-31 == 20, use RTC freq */
        itm_src = (cmd>>1)&1;               /* set src */
        if (itm_src)                        /* use specified src freq */
//DIAG      sim_activate_after_abs_d(&itm_unit, ((double)cnt*400000)/rtc_tps);
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*1000000)/rtc_tps);
        else
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*itm_tick_size_x_100)/100.0);
        itm_run = 1;                        /* set timer running */

        if (cmd & 0x04)                     /* do we reload on zero? */
            itm_cnt = cnt;                  /* count reset value */
        else
            itm_cnt = 0;                    /* no count reset value */
        itm_strt = 0;                       /* not restarted neg */
        itm_load = cnt;                     /* now loaded */
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x return value %08x (%08d)\n", cmd, temp, temp);
        return temp;                        /* return curr count */
        break;

    case 0x3c:                              /* load timer with new value and start */
    case 0x3d:                              /* load timer with new value and start */
    /* load timer with new value and start using RTC as source */
    case 0x3e:                              /* load timer with new value and start RTC*/
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%2x init value %08x (%d)\n", cmd, cnt, cnt);
        sim_cancel (&itm_unit);             /* cancel timer */
        /* if bits 30-31 == 20, use RTC freq */
        itm_src = (cmd>>1)&1;               /* set src */
        if (itm_src)                        /* use specified src freq */
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*700000)/rtc_tps);
        else
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*itm_tick_size_x_100)/100.0);
        itm_run = 1;                        /* set timer running */

        if (cmd & 0x04)                     /* do we reload on zero? */
            itm_cnt = cnt;                  /* count reset value */
        itm_strt = 0;                       /* not restarted neg */
        itm_load = cnt;                     /* now loaded */
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x return value %08x (%08d)\n", cmd, cnt, cnt);
        return 0;                           /* does not matter, no value returned  */
        break;

    case 0x40:                              /* read the current timer value */
        /* return current count value from timer */
        temp = itm_load;                    /* get last loaded value */
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x read value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
        }
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x40 return value %08x (%d)\n", temp, temp);
        return temp;
        break;

    case 0x60:                              /* read and stop timer */
        /* get timer value and stop timer */
        temp = itm_load;                    /* get last loaded value */
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x read value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
            sim_cancel (&itm_unit);         /* cancel timer */
        }
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%2x temp value %08x (%d)\n", cmd, temp, temp);
        itm_run = 0;                        /* stop timer running */
        itm_cnt = 0;                        /* no reload count value */
        itm_load = temp;                    /* current loaded value */
        itm_strt = 0;                       /* not restarted neg */
        return temp;                        /* return current count value */
        break;

    case 0x6a:                              /* read value & load new one */
    case 0x68:                              /* read value & load new one */
    case 0x69:                              /* read value & load new one */
        /* get timer value and load new value, do not start timer */
        temp = itm_load;                    /* get last loaded value */
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x read value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
            sim_cancel (&itm_unit);         /* cancel timer */
        }
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x temp value %08x (%08d)\n", cmd, temp, temp);
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x init value %08x (%08d)\n", cmd, cnt, cnt);
        itm_src = (cmd>>1)&1;               /* set src */
        itm_run = 0;                        /* stop timer running */
        itm_cnt = 0;                        /* no count reset value */
        itm_strt = 0;                       /* not restarted neg */
        itm_load = cnt;                     /* now loaded */
        return temp;                        /* return current count value */
        break;

    case 0x7d:                              /* read the current timer value */
    case 0x78:                              /* read the current timer value */
    case 0x79:                              /* read the current timer value */
    case 0x7a:                              /* read the current timer value */
    case 0x7b:                              /* read the current timer value */
    case 0x7c:                              /* read the current timer value */
    case 0x7e:                              /* read the current timer value */
    case 0x7f:                              /* read the current timer value */
        /* get timer value, load new value and start timer */
        temp = itm_load;                    /* get last loaded value */
        if (itm_run) {                      /* if we were running save curr cnt */
            /* read timer value */
            temp = (uint32)(100.0*sim_activate_time_usecs(&itm_unit)/itm_tick_size_x_100);
            sim_debug(DEBUG_CMD, &itm_dev,
                "Intv 0x%2x read value %08x (%d)\n", cmd, temp, temp);
            if (itm_strt) {                 /* see if running neg */
                /* we only get here if timer ran out and no reload value */
                /* get simulated negative start time in counts */
                temp = temp - itm_strt;     /* make into a negative number */
            } 
//extra     sim_cancel (&itm_unit);         /* cancel timer */
        }
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x temp value %08x (%08d)\n", cmd, temp, temp);
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv 0x%02x init value %08x (%08d)\n", cmd, cnt, cnt);
        sim_cancel (&itm_unit);             /* cancel timer */
        /* start timer to fire after cnt ticks */
        itm_src = (cmd>>1)&1;               /* set src */
        if (itm_src)                        /* use specified src freq */
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*1000000)/rtc_tps);
        else
            sim_activate_after_abs_d(&itm_unit, ((double)cnt*itm_tick_size_x_100)/100.0);
        itm_cnt = 0;                        /* no count reset value */
        if (cmd & 0x04)                     /* reload on int? */
            itm_cnt = cnt;                  /* set reload count value */
        itm_run = 1;                        /* set timer running */
        itm_strt = 0;                       /* not restarted neg */
        itm_load = cnt;                     /* now loaded */
        return temp;                        /* return current count value */
        break;
    default:
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv unknown cmd %02x level %02x\n", cmd, level);
        break;
    }
    return 0;                               /* does not matter, no value returned  */
}

/* Clock interrupt start/stop */
/* ss = 1 - clock interrupt enabled */
/* ss = 0 - clock interrupt disabled */
/* level = interrupt level */
void itm_setup(uint32 ss, uint32 level)
{
    itm_lvl = level;                        /* save the interrupt level */
    itm_load = 0;                           /* not loaded */
    itm_src = 0;                            /* use itm for freq */
    itm_strt = 0;                           /* not restarted neg */
    itm_run = 0;                            /* not running */
    itm_cnt = 0;                            /* no count reset value */
    sim_cancel (&itm_unit);                 /* not running yet */
    if (ss == 1) {                          /* starting? */
        INTS[level] |= INTS_ENAB;           /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;      /* in spad too */
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv Timer setup enable int %02x value %08x itm_pie %01x ss %01x\n",
            itm_lvl, itm_cnt, itm_pie, ss);
    } else {
        INTS[level] &= ~INTS_ENAB;          /* make sure disabled */
        SPAD[level+0x80] &= ~SINT_ENAB;     /* in spad too */
        sim_debug(DEBUG_CMD, &itm_dev,
            "Intv Timer setup disable int %02x value %08x itm_pie %01x ss %01x\n",
            itm_lvl, itm_cnt, itm_pie, ss);
    }
    itm_pie = ss;                           /* set new state */
}

/* Clock reset */
t_stat itm_reset (DEVICE *dptr)
{
    itm_pie = 0;                            /* disable pulse */
    itm_run = 0;                            /* not running */
    itm_load = 0;                           /* not loaded */
    itm_src = 0;                            /* use itm for freq */
    itm_strt = 0;                           /* not restarted neg */
    itm_cnt = 0;                            /* no count reset value */
    sim_cancel (&itm_unit);                 /* not running yet */
    return SCPE_OK;
}

/* Set frequency */
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (cptr)                               /* if chars, bad */
        return SCPE_ARG;                    /* ARG error */
    if ((val != 3840) && (val != 7680))
        return SCPE_IERR;                   /* scope error */
    itm_tick_size_x_100 = val;              /* set the new frequency */
    return SCPE_OK;                         /* we done */
}

/* Show frequency */
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    /* print the current interval count setting */
    fprintf (st, "%0.2fus", (itm_tick_size_x_100 / 100.0));
    return SCPE_OK;
}

/* sho help rtc */
t_stat itm_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
    fprintf(st, "SEL 32 IOP/MFP interval timer at 0x7F04\r\n");
    fprintf(st, "Use:\r\n");
    fprintf(st, "    sim> SET ITM [3840][7680]\r\n");
    fprintf(st, "to set interval timer clock rate in us x 100\r\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

/* device description */
const char *itm_desc(DEVICE *dptr)
{
    return "SEL IOP/MFP Interval Timer @ address 0x7F04";
}

#endif

