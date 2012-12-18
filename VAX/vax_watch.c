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

#define WTC_CSRA_RS     0x0F
#define WTC_CSRA_V_DV   4
#define WTC_CSRA_M_DV   0x7
#define WTC_CSRA_DV     (WTC_CSRA_M_DV << WTC_CSRA_V_DV)
#define WTC_CSRA_UIP    0x80                            /* update in progess */
#define WTC_CSRA_WR     (WTC_CSRA_RS | WTC_CSRA_DV)

#define WTC_CSRB_DSE    0x01                            /* daylight saving en */
#define WTC_CSRB_2412   0x02                            /* 24/12hr select */
#define WTC_CSRB_DM     0x04                            /* data mode */
#define WTC_CSRB_SET    0x80                            /* set time */
#define WTC_CSRB_WR     (WTC_CSRB_DSE | WTC_CSRB_2412 | WTC_CSRB_DM | WTC_CSRB_SET)

#define WTC_CSRD_VRT    0x80                            /* valid time */
#define WTC_CSRD_RD     (WTC_CSRD_VRT)

#define WTC_MODE_STD    0
#define WTC_MODE_VMS    1

extern int32 sim_switches;

int32 wtc_csra = 0;
int32 wtc_csrb = 0;
int32 wtc_csrc = 0;
int32 wtc_csrd = 0;
int32 wtc_mode = WTC_MODE_VMS;

t_stat wtc_set (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat wtc_show (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat wtc_reset (DEVICE *dptr);
void wtc_set_valid (void);
void wtc_set_invalid (void);

UNIT wtc_unit = { UDATA (NULL, 0, 0) };

REG wtc_reg[] = {
    { HRDATA (CSRA, wtc_csra, 8) },
    { HRDATA (CSRB, wtc_csrb, 8) },
    { HRDATA (CSRC, wtc_csrc, 8) },
    { HRDATA (CSRD, wtc_csrd, 8) },
    { NULL }
    };

MTAB wtc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "TIME", "TIME={VMS|STD}", &wtc_set, &wtc_show },
    { 0 }
    };

DEVICE wtc_dev = {
    "WTC", &wtc_unit, wtc_reg, wtc_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &wtc_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

int32 wtc_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0xF;
int32 val = 0;
time_t curr;
struct tm *ctm = NULL;

if (rg < 10) {                                          /* time reg? */
    curr = time (NULL);                                 /* get curr time */
    if (curr == (time_t) -1)                            /* error? */
        return 0;
    ctm = localtime (&curr);                            /* decompose */
    if (ctm == NULL)                                    /* error? */
        return 0;
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
        val = ctm->tm_mon;
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

return ((rg & 1) ? (val << 16) : val);                  /* word aligned? */
}

void wtc_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 1) & 0xF;
val = val & 0xFF;

switch(rg) {

    case 10:                                            /* CSR A */
        val = val & WTC_CSRA_WR;
        wtc_csra = (wtc_csra & ~WTC_CSRA_WR) | val;
        break;

    case 11:                                            /* CSR B */
        val = val & WTC_CSRB_WR;
        wtc_csrb = (wtc_csrb & ~WTC_CSRB_WR) | val;
        break;
        }
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

t_stat wtc_set (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr != NULL) wtc_mode = strcmp(cptr, "STD");
return SCPE_OK;
}

t_stat wtc_show (FILE *st, UNIT *uptr, int32 val, void *desc)
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
