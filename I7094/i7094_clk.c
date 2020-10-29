/* i7094_clk.c: IBM 7094 clock

   Copyright (c) 2003-2011, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   clk          RPQ F89349 interval timer
                Chronolog calendar clock

   25-Mar-11    RMS     According to RPQ, clock clears on RESET
*/

#include "i7094_defs.h"
#include <time.h>

uint32 chtr_clk = 0;
extern t_uint64 *M;

t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
uint8 bcd_2d (uint32 n, uint8 *b2);

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit
   clk_reg      CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, 0, 0), 16000 };

REG clk_reg[] = {
    { FLDATA (TRAP, chtr_clk, 0) },
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE+DEV_DIS
    };

/* Clock unit service */

t_stat clk_svc (UNIT *uptr)
{
t_uint64 ctr;

if ((clk_dev.flags & DEV_DIS) == 0) {                   /* clock enabled? */
    ctr = ReadP (CLK_CTR);
    ctr = (ctr + 1) & MMASK;                            /* increment */
    WriteP (CLK_CTR, ctr);
    if (ctr == 0)                                       /* overflow? req trap */
        chtr_clk = 1;
    sim_rtcn_calb (CLK_TPS, TMR_CLK);                   /* calibrate clock */
    sim_activate_after (uptr, 1000000/CLK_TPS);         /* reactivate unit */
    }
return SCPE_OK;
}

/* Chronolog clock */

uint32 chrono_rd (uint8 *buf, uint32 bufsiz)
{
time_t curtim;
t_uint64 ctr;
struct tm *tptr;

if (bufsiz < 12)
    return 0;
curtim = sim_get_time (NULL);                           /* get time */
tptr = localtime (&curtim);                             /* decompose */
if (tptr == NULL)                                       /* error? */
    return 0;

buf[0] = bcd_2d (tptr->tm_mon + 1, buf + 1);
buf[2] = bcd_2d (tptr->tm_mday, buf + 3);
buf[4] = bcd_2d (tptr->tm_hour, buf + 5);
buf[6] = bcd_2d (tptr->tm_min, buf + 7);
buf[8] = bcd_2d (tptr->tm_sec, buf + 9);
ctr = ReadP (CLK_CTR);
buf[10] = bcd_2d ((uint32) (ctr % 60), buf + 11);
return 12;
}

/* Convert number (0-99) to BCD */

uint8 bcd_2d (uint32 n, uint8 *b2)
{
uint8 d1, d2;

d1 = n / 10;
d2 = n % 10;
if (d1 == 0)
    d1 = BCD_ZERO;
if (d2 == 0)
    d2 = BCD_ZERO;
if (b2 != NULL)
    *b2 = d2;
return d1;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
chtr_clk = 0;
if (clk_dev.flags & DEV_DIS)
    sim_cancel (&clk_unit);
else {
    sim_activate (&clk_unit, sim_rtcn_init (clk_unit.wait, TMR_CLK));
    WriteP (CLK_CTR, 0);
    }
return SCPE_OK;
}
