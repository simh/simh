/* pdp10_tim.c: PDP-10 tim subsystem simulator

   Copyright (c) 1993-2001, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#include "pdp10_defs.h"
#include <time.h>

#define TIM_N_HWRE	12				/* hwre bits */
#define TIM_HWRE	0000000010000			/* hwre incr */
#define TB_MASK		037777777777777777777;		/* 71 - 12 bits */
#define TPS		1001				/* ticks per sec */
#define UNIT_V_Y2K	(UNIT_V_UF)			/* Y2K compliant OS */
#define UNIT_Y2K	(1u << UNIT_V_Y2K)

extern int32 apr_flg, pi_act;
extern UNIT cpu_unit;
extern d10 pcst;
extern a10 pager_PC;
int64 timebase = 0;					/* 71b timebase */
d10 ttg = 0;						/* time to go */
d10 period = 0;						/* period */
d10 quant = 0;						/* ITS quantum */
int32 diagflg = 0;					/* diagnostics? */

t_stat tim_svc (UNIT *uptr);
t_stat tim_reset (DEVICE *dptr);
extern d10 Read (a10 ea, int32 prv);
extern d10 ReadM (a10 ea, int32 prv);
extern void Write (a10 ea, d10 val, int32 prv);
extern void WriteP (a10 ea, d10 val);
extern int32 pi_eval (void);
extern sim_rtc_calb (int32 tps);

/* TIM data structures

   tim_dev	TIM device descriptor
   tim_unit	TIM unit descriptor
   tim_reg	TIM register list
*/

UNIT tim_unit = { UDATA (&tim_svc, 0, 0), 500 };

REG tim_reg[] = {
	{ ORDATA (TIMEBASE, timebase, 71 - TIM_N_HWRE) },
	{ ORDATA (TTG, ttg, 36) },
	{ ORDATA (PERIOD, period, 36) },
	{ ORDATA (QUANT, quant, 36) },
	{ DRDATA (TIME, tim_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (DIAG, diagflg, 0) },
	{ FLDATA (Y2K, tim_unit.flags, UNIT_V_Y2K), REG_HRO },
	{ NULL }  };

MTAB tim_mod[] = {
	{ UNIT_Y2K, 0, "non Y2K OS", "NOY2K", NULL },
	{ UNIT_Y2K, UNIT_Y2K, "Y2K OS", "Y2K", NULL },
	{ 0 }  };

DEVICE tim_dev = {
	"TIM", &tim_unit, tim_reg, tim_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &tim_reset,
	NULL, NULL, NULL };

/* Timer instructions */

t_bool rdtim (a10 ea, int32 prv)
{
ReadM (INCA (ea), prv);
Write (ea, (timebase >> (35 - TIM_N_HWRE)) & DMASK, prv);
Write (INCA(ea), (timebase << TIM_N_HWRE) & MMASK, prv);
return FALSE;
}

t_bool wrtim (a10 ea, int32 prv)
{
timebase = (Read (ea, prv) << (35 - TIM_N_HWRE)) |
	(CLRS (Read (INCA (ea), prv)) >> TIM_N_HWRE);
return FALSE;
}

t_bool rdint (a10 ea, int32 prv)
{
Write (ea, period, prv);
return FALSE;
}

t_bool wrint (a10 ea, int32 prv)
{
period = Read (ea, prv);
ttg = period;
return FALSE;
}

/* Timer routines

   tim_svc	process event (timer tick)
   tim_reset	process reset
*/

t_stat tim_svc (UNIT *uptr)
{
sim_activate (&tim_unit,				/* reactivate unit */
	(diagflg? tim_unit.wait: sim_rtc_calb (TPS)));
timebase = (timebase + 1) & TB_MASK;			/* increment timebase */
ttg = ttg - TIM_HWRE;					/* decrement timer */
if (ttg <= 0) {						/* timeout? */
	ttg = period;					/* reload */
	apr_flg = apr_flg | APRF_TIM;  }		/* request interrupt */
if (ITS) {						/* ITS? */
	if (pi_act == 0) quant = (quant + TIM_HWRE) & DMASK;
	if (TSTS (pcst)) {				/* PC sampling? */
		pcst = AOB (pcst);			/* add 1,,1 */
		WriteP ((a10) pcst & AMASK, pager_PC);  }
	}						/* end ITS */
return SCPE_OK;
}

t_stat tim_reset (DEVICE *dptr)
{
period = ttg = 0;					/* clear timer */
apr_flg = apr_flg & ~APRF_TIM;				/* clear interrupt */
sim_activate (&tim_unit, tim_unit.wait);		/* activate unit */
return SCPE_OK;
}

/* Time of year clock */

t_stat tcu_rd (int32 *data, int32 PA, int32 access)
{
time_t curtim;
struct tm *tptr;

curtim = time (NULL);					/* get time */
tptr = localtime (&curtim);				/* decompose */
if (tptr == NULL) return SCPE_NXM;			/* Y2K prob? */
if ((tptr -> tm_year > 99) && !(tim_unit.flags & UNIT_Y2K))
	tptr -> tm_year = 99; 

switch ((PA >> 1) & 03) {				/* decode PA<3:1> */
case 0:							/* year/month/day */
	*data = (((tptr -> tm_year) & 0177) << 9) |
		(((tptr -> tm_mon + 1) & 017) << 5) |
		((tptr -> tm_mday) & 037);
	return SCPE_OK;
case 1:							/* hour/minute */
	*data = (((tptr -> tm_hour) & 037) << 8) |
		((tptr -> tm_min) & 077);
	return SCPE_OK;
case 2:							/* second */
	*data = (tptr -> tm_sec) & 077;
	return SCPE_OK;
case 3:							/* status */
	*data = CSR_DONE;
	return SCPE_OK;  }
return SCPE_NXM;					/* can't get here */
}
