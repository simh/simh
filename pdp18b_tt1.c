/* pdp18b_tt1.c: 18b PDP's second Teletype

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

   tti1		keyboard
   tto1		teleprinter

   10-Jun-01	RMS	Cleaned up IOT decoding to reflect hardware
*/

#include "pdp18b_defs.h"
#include <ctype.h>

#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)

extern int32 int_req, saved_PC;
t_stat tti1_svc (UNIT *uptr);
t_stat tto1_svc (UNIT *uptr);
t_stat tti1_reset (DEVICE *dptr);
t_stat tto1_reset (DEVICE *dptr);
extern t_stat sim_poll_kbd (void);
extern t_stat sim_putchar (int32 out);
static uint8 tto1_consout[CONS_SIZE];

/* TTI1 data structures

   tti1_dev	TTI1 device descriptor
   tti1_unit	TTI1 unit
   tto1_mod	TTI1 modifier list
   tti1_reg	TTI1 register list
*/

UNIT tti1_unit = { UDATA (&tti1_svc, UNIT_UC, 0), KBD_POLL_WAIT };

REG tti1_reg[] = {
	{ ORDATA (BUF, tti1_unit.buf, 8) },
	{ FLDATA (INT, int_req, INT_V_TTI1) },
	{ FLDATA (DONE, int_req, INT_V_TTI1) },
	{ FLDATA (UC, tti1_unit.flags, UNIT_V_UC), REG_HRO },
	{ DRDATA (POS, tti1_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (CFLAG, tti1_unit.flags, UNIT_V_CONS), REG_HRO },
	{ NULL }  };

MTAB tti1_mod[] = {
	{ UNIT_CONS, 0, "inactive", NULL, NULL },
	{ UNIT_CONS, UNIT_CONS, "active console", "CONSOLE", &set_console },
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ 0 }  };

DEVICE tti1_dev = {
	"TTI1", &tti1_unit, tti1_reg, tti1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti1_reset,
	NULL, NULL, NULL };

/* TTO1 data structures

   tto1_dev	TTO1 device descriptor
   tto1_unit	TTO1 unit
   tto1_mod	TTO1 modifier list
   tto1_reg	TTO1 register list
*/

UNIT tto1_unit = { UDATA (&tto1_svc, UNIT_UC, 0), SERIAL_OUT_WAIT };

REG tto1_reg[] = {
	{ ORDATA (BUF, tto1_unit.buf, 8) },
	{ FLDATA (INT, int_req, INT_V_TTO1) },
	{ FLDATA (DONE, int_req, INT_V_TTO1) },
	{ DRDATA (POS, tto1_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
	{ BRDATA (CONSOUT, tto1_consout, 8, 8, CONS_SIZE), REG_HIDDEN },
	{ FLDATA (CFLAG, tto1_unit.flags, UNIT_V_CONS), REG_HRO },
	{ NULL }  };

MTAB tto1_mod[] = {
	{ UNIT_CONS, 0, "inactive", NULL, NULL },
	{ UNIT_CONS, UNIT_CONS, "active console", "CONSOLE", &set_console },
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ 0 }  };

DEVICE tto1_dev = {
	"TTO1", &tto1_unit, tto1_reg, tto1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto1_reset,
	NULL, NULL, NULL };

/* Terminal input: IOT routine */

int32 tti1 (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* KSF1 */
	if (int_req & INT_TTI1) AC = AC | IOT_SKP;  }
if (pulse & 002) {					/* KRB1 */
	int_req = int_req & ~INT_TTI1;			/* clear flag */
	AC= AC | tti1_unit.buf;  }			/* return buffer */
return AC;
}

/* Unit service */

t_stat tti1_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti1_unit, tti1_unit.wait);		/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
temp = temp & 0177;
if ((tti1_unit.flags & UNIT_UC) && islower (temp)) temp = toupper (temp);
tti1_unit.buf = temp | 0200;				/* got char */
int_req = int_req | INT_TTI1;				/* set flag */
tti1_unit.pos = tti1_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tti1_reset (DEVICE *dptr)
{
tti1_unit.buf = 0;					/* clear buffer */
int_req = int_req & ~INT_TTI;				/* clear flag */
if (tti1_unit.flags & UNIT_CONS)			/* if active console */
	sim_activate (&tti1_unit, tti1_unit.wait);	/* activate unit */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto1 (int32 pulse, int32 AC)
{
if (pulse & 001) {					/* TSF */
	if (int_req & INT_TTO1) AC = AC | IOT_SKP;  }
if (pulse & 002) int_req = int_req & ~INT_TTO1;		/* clear flag */
if (pulse & 004) {					/* load buffer */
	sim_activate (&tto1_unit, tto1_unit.wait);	/* activate unit */
	tto1_unit.buf = AC & 0377;  }			/* load buffer */
return AC;
}

/* Unit service */

t_stat tto1_svc (UNIT *uptr)
{
int32 out, temp;

int_req = int_req | INT_TTO1;				/* set flag */
out = tto1_unit.buf & 0177;
if (!(tto1_unit.flags & UNIT_UC) ||
	 ((out >= 007) && (out <= 0137))) {
	temp = sim_putcons (out, uptr);
	if (temp != SCPE_OK) return temp;
	tto1_unit.pos = tto1_unit.pos + 1;  }
return SCPE_OK;
}

/* Reset routine */

t_stat tto1_reset (DEVICE *dptr)
{
tto1_unit.buf = 0;					/* clear buffer */
int_req = int_req & ~INT_TTO1;				/* clear flag */
sim_cancel (&tto1_unit);				/* deactivate unit */
tto1_unit.filebuf = tto1_consout;			/* set buf pointer */
return SCPE_OK;
}
