/* vax_watch.c: VAX watch chip

   Copyright (c) 2011-2012, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   wtc          Watch chip
   
   08-Nov-2012  MB      First version

   This file covers the watch chip (MC146818) which is used by several VAX
   models including the KA620, KA630, KA410, KA420 and KA820.
*/

#include "vax_defs.h"
#include <time.h>

/* control/status registers */

#define WTC_CSRA_RS     0x0F                            /* Rate Select Bits (Not Used by VMS) */
#define WTC_CSRA_V_DV   4
#define WTC_CSRA_M_DV   0x7
#define WTC_CSRA_DV     (WTC_CSRA_M_DV << WTC_CSRA_V_DV)
#define WTC_CSRA_UIP    0x80                            /* update in progess (BUSY) */
#define WTC_CSRA_WR     (WTC_CSRA_RS | WTC_CSRA_DV)
const char *wtc_dv_modes[] = {"4.194304MHz", "1.048576MHz", "32.768KHz", "Any", "Any", "Test-Only", "Test-Only", "Test-Only"};
BITFIELD wtc_csra_bits[] = {
    BITNCF(4),                              /* Rate Select - unused MBZ for VMS */
    BITFNAM(DV,3,wtc_dv_modes),             /* Divider Select */
    BIT(UIP),                               /* Update In Progress */
    ENDBITS
};

#define WTC_CSRB_DSE    0x01                            /* daylight saving en */
#define WTC_CSRB_2412   0x02                            /* 24/12hr select (1 -> 24 hr) */
#define WTC_CSRB_DM     0x04                            /* data mode (1 -> binary, 0 -> BCD) */
#define WTC_CSRB_SET    0x80                            /* set time */
#define WTC_CSRB_PIE    0x40                            /* periodic interrupt enable (Not Used by VMS) */
#define WTC_CSRB_AIE    0x20                            /* alarm interrupt enable (Not Used by VMS) */
#define WTC_CSRB_UIE    0x10                            /* update ended interrupt enable (Not Used by VMS) */
#define WTC_CSRB_SQWE   0x08                            /* square wave enable (Not Used by VMS) */
#define WTC_CSRB_WR     (WTC_CSRB_DSE | WTC_CSRB_2412 | WTC_CSRB_DM | WTC_CSRB_SET)
const char *wtc_dse_modes[] = {"Disabled", "Enabled"};
const char *wtc_hr_modes[] = {"12Hr", "24Hr"};
const char *wtc_data_modes[] = {"BCD", "Binary"};
BITFIELD wtc_csrb_bits[] = {
    BITFNAM(DST,1,wtc_dse_modes),           /* Daylight Savings Time Enable */
    BITFNAM(24HR,1,wtc_hr_modes),           /* 24/12 Hour Mode */
    BITFNAM(DM,1,wtc_data_modes),           /* Data Mode */
    BITNCF(4),                              /* Unused SQWE, UIE, AIE, PIE */
    BIT(SET),                               /* Set In Progress */
    ENDBITS
};

BITFIELD wtc_csrc_bits[] = {
    BITF(VALUE,8),                          /* Should be unused */
    ENDBITS
};
#define WTC_CSRD_VRT    0x80                            /* valid time */
#define WTC_CSRD_RD     (WTC_CSRD_VRT)
#define WTC_CSRD_WR     (WTC_CSRD_VRT)
BITFIELD wtc_csrd_bits[] = {
    BITNCF(7),
    BIT(VALID),                             /* Valid RAM and Time (VRT) */
    ENDBITS
};

BITFIELD wtc_value_bits[] = {
    BITFFMT(VALUE,8,%d),                    /* Decimal Value */
    ENDBITS
};
BITFIELD* wtc_bitdefs[] = {wtc_value_bits, wtc_value_bits, wtc_value_bits, wtc_value_bits,
                           wtc_value_bits, wtc_value_bits, wtc_value_bits, wtc_value_bits, 
                           wtc_value_bits, wtc_value_bits, wtc_csra_bits,  wtc_csrb_bits, 
                           wtc_csrc_bits,  wtc_csrd_bits,  wtc_value_bits, wtc_value_bits};

#define WTC_MODE_STD    0
#define WTC_MODE_VMS    1
const char *wtc_modes[] = {"Std", "VMS"};
BITFIELD wtc_mode_bits[] = {
    BITFNAM(MODE,1,wtc_modes),              /* Watch Date/Time mode */
    ENDBITS
};

int32 wtc_csra = 0;
int32 wtc_csrb = 0;
int32 wtc_csrc = 0;
int32 wtc_csrd = 0;
int32 wtc_mode = WTC_MODE_VMS;

t_stat wtc_set (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat wtc_show (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat wtc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *wtc_description (DEVICE *dptr);
t_stat wtc_reset (DEVICE *dptr);
void wtc_set_valid (void);
void wtc_set_invalid (void);

UNIT wtc_unit = { UDATA (NULL, 0, 0) };

REG wtc_reg[] = {
    { HRDATADF (CSRA, wtc_csra, 8, "CSRA", wtc_csra_bits) },
    { HRDATADF (CSRB, wtc_csrb, 8, "CSRB", wtc_csrb_bits) },
    { HRDATADF (CSRC, wtc_csrc, 8, "CSRC", wtc_csrc_bits) },
    { HRDATADF (CSRD, wtc_csrd, 8, "CSRD", wtc_csrd_bits) },
    { HRDATADF (MODE, wtc_mode, 8, "Watch Mode", wtc_mode_bits) },
    { NULL }
    };

MTAB wtc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "TIME", "TIME={VMS|STD}", &wtc_set, &wtc_show, NULL, "Display watch time mode" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* trace read/write registers */

DEBTAB wtc_debug[] = {
  {"REG",    DBG_REG},
  {0}
};

DEVICE wtc_dev = {
    "WTC", &wtc_unit, wtc_reg, wtc_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &wtc_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, wtc_debug, NULL, NULL, &wtc_help, NULL, NULL,
    &wtc_description
    };

/* Register names for Debug tracing */
static const char *wtc_regs[] =
    {"SEC ", "SECA", "MIN ", "MINA", 
     "HR  ", "HRA ", "DOW ", "DOM ", 
     "MON ", "YEAR", "CSRA", "CSRB", 
     "CSRC", "CSRD" };



int32 wtc_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0xF;
int32 val = 0;
time_t curr;
struct timespec now;
static int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
struct tm *ctm = NULL;

if (rg < 10) {                                          /* time reg? */
    sim_rtcn_get_time (&now, TMR_CLK);
    curr = now.tv_sec;                                  /* get curr time */
    if (curr == (time_t) -1)                            /* error? */
        return 0;
    ctm = localtime (&curr);                            /* decompose */
    if (ctm == NULL)                                    /* error? */
        return 0;
    if ((wtc_mode == WTC_MODE_VMS) &&
        ((ctm->tm_year % 4) == 0)) {                    /* Leap Year? */
        if (ctm->tm_mon > 1) {                          /* Past February? */
            ++ctm->tm_mday;                             /* Adjust for Leap Day */
            if (ctm->tm_mday > mdays[ctm->tm_mon]) {    /* wrap to last day of prior month */
                ++ctm->tm_mon;
                ctm->tm_mday = 1;
                }
            }
        else
            if ((ctm->tm_mon == 1) &&                   /* February 29th? */
                (ctm->tm_mday == 29)) {
                ctm->tm_mon = 2;                        /* Is March 1 in 1982 */
                ctm->tm_mday = 1;
                }
        }
    }

switch(rg) {

    case 0:                                             /* seconds */
        val = ctm->tm_sec;
        break;

    case 2:                                             /* minutes */
        val = ctm->tm_min;
        break;

    case 4:                                             /* hours */
        val = ctm->tm_hour;
        break;

    case 6:                                             /* day of week */
        val = ctm->tm_wday;
        break;

    case 7:                                             /* day of month */
        val = ctm->tm_mday;
        break;

    case 8:                                             /* month */
        val = ctm->tm_mon + 1;
        break;

    case 9:                                             /* year */
        if (wtc_mode == WTC_MODE_VMS)
            val = 82;                                   /* always 1982 for VMS */
        else
            val = (int32)(ctm->tm_year % 100);
        break;

    case 10:                                            /* CSR A */
        val = wtc_csra;
        break;

    case 11:                                            /* CSR B */
        val = wtc_csrb;
        break;

    case 12:                                            /* CSR C */
        val = wtc_csrc;
        break;

    case 13:                                            /* CSR D */
        val = wtc_csrd & WTC_CSRD_RD;
        break;
        }

sim_debug(DBG_REG, &wtc_dev, "wtc_rd(pa=0x%08X [%s], data=0x%X) ", pa, wtc_regs[rg], val);
sim_debug_bits(DBG_REG, &wtc_dev, wtc_bitdefs[rg], (uint32)val, (uint32)val, TRUE);

if (rg & 1)
    val = (val << 16);                                  /* word aligned? */

return val;
}

void wtc_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 1) & 0xF;
int32 new_val = val;

val = val & 0xFF;

switch(rg) {

    case 10:                                            /* CSR A */
        val = val & WTC_CSRA_WR;
        new_val = wtc_csra = (wtc_csra & ~WTC_CSRA_WR) | val;
        break;

    case 11:                                            /* CSR B */
        val = val & WTC_CSRB_WR;
        new_val = wtc_csrb = (wtc_csrb & ~WTC_CSRB_WR) | val;
        break;

    case 12:                                            /* CSR C */
        break;

    case 13:                                            /* CSR D */
        val = val & WTC_CSRD_WR;
        new_val = wtc_csrd = (wtc_csrd & ~WTC_CSRD_WR) | val;
        break;
        }

sim_debug(DBG_REG, &wtc_dev, "wtc_wr(pa=0x%08X [%s], data=0x%X) ", pa, wtc_regs[rg], val);
sim_debug_bits(DBG_REG, &wtc_dev, wtc_bitdefs[rg], (uint32)new_val, (uint32)new_val, TRUE);

}

t_stat wtc_reset (DEVICE *dptr)
{
if (sim_switches & SWMASK ('P')) {                      /* powerup? */
    wtc_csra = 0;
    wtc_csrb = 0;
    wtc_csrc = 0;
    wtc_csrd = 0;
    wtc_mode = WTC_MODE_VMS;
    }
return SCPE_OK;
}

t_stat wtc_set (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr != NULL)
    wtc_mode = ((strcmp(cptr, "STD") != 0) ? WTC_MODE_VMS : WTC_MODE_STD);
return SCPE_OK;
}

t_stat wtc_show (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf(st, "time=%s", (wtc_mode ? "vms" :"std"));
return SCPE_OK;
}

void wtc_set_valid (void)
{
wtc_csra |= (2 << WTC_CSRA_V_DV);
wtc_csrb |= (WTC_CSRB_DM | WTC_CSRB_2412);
wtc_csrd |= WTC_CSRD_VRT;
}

void wtc_set_invalid (void)
{
wtc_csrd &= ~WTC_CSRD_VRT;
}

t_stat wtc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Watch Chip (WTC)\n\n");
fprintf (st, "The WTC simulates the MC146818 watch chip.  It recognizes the following options:\n\n");
fprintf (st, "  SET WTC TIME=STD            standard time mode\n");
fprintf (st, "  SET WTC TIME=VMS            VMS time mode\n\n");
fprintf (st, "When running in standard mode the current year reported by the watch chip is\n");
fprintf (st, "determined by the date/time of the host system.  When running in VMS mode the\n");
fprintf (st, "year is fixed at 1982, which is one of the conditions VMS expects in order to\n");
fprintf (st, "verify that the time reported is valid.  The default mode is VMS.\n\n");
return SCPE_OK;
}

const char *wtc_description (DEVICE *dptr)
{
return "watch chip";
}
