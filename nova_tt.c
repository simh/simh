/* nova_tt.c: NOVA console terminal simulator

   Copyright (c) 1993-2000, Robert M. Supnik

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

   tti		terminal input
   tto		terminal output
*/

#include "nova_defs.h"

#define	UNIT_V_DASHER	(UNIT_V_UF + 0)			/* Dasher mode */
#define UNIT_DASHER	(1 << UNIT_V_DASHER)
extern int32 int_req, dev_busy, dev_done, dev_disable;
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat ttx_setmod (UNIT *uptr, int32 value);
extern t_stat sim_poll_kbd (void);
extern t_stat sim_putchar (int32 out);

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit descriptor
   tti_reg	TTI register list
   ttx_mod	TTI/TTO modifiers list
*/

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
	{ ORDATA (BUF, tti_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_TTI) },
	{ FLDATA (DONE, dev_done, INT_V_TTI) },
	{ FLDATA (DISABLE, dev_disable, INT_V_TTI) },
	{ FLDATA (INT, int_req, INT_V_TTI) },
	{ DRDATA (POS, tti_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (MODE, tti_unit.flags, UNIT_V_DASHER), REG_HRO },
	{ NULL }  };

MTAB ttx_mod[] = {
	{ UNIT_DASHER, 0, "ANSI", "ANSI", &ttx_setmod },
	{ UNIT_DASHER, UNIT_DASHER, "Dasher", "DASHER", &ttx_setmod },
	{ 0 }  };

DEVICE tti_dev = {
	"TTI", &tti_unit, tti_reg, ttx_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti_reset,
	NULL, NULL, NULL };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit descriptor
   tto_reg	TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
	{ ORDATA (BUF, tto_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_TTO) },
	{ FLDATA (DONE, dev_done, INT_V_TTO) },
	{ FLDATA (DISABLE, dev_disable, INT_V_TTO) },
	{ FLDATA (INT, int_req, INT_V_TTO) },
	{ DRDATA (POS, tto_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
	{ FLDATA (MODE, tto_unit.flags, UNIT_V_DASHER), REG_HRO },
	{ NULL }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, ttx_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto_reset,
	NULL, NULL, NULL };

/* Terminal input: IOT routine */

int32 tti (int32 pulse, int32 code, int32 AC)
{
int32 iodata;

iodata = (code == ioDIA)? tti_unit.buf & 0377: 0;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_TTI;			/* set busy */
	dev_done = dev_done & ~INT_TTI;			/* clear done, int */
	int_req = int_req & ~INT_TTI;
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_TTI;			/* clear busy */
	dev_done = dev_done & ~INT_TTI;			/* clear done, int */
	int_req = int_req & ~INT_TTI;
	break;  }					/* end switch */
return iodata;
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);		/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
tti_unit.buf = temp & 0177;
if ((tti_unit.flags & UNIT_DASHER) && (tti_unit.buf == '\r'))
	tti_unit.buf = '\n';				/* Dasher: cr -> nl */
dev_busy = dev_busy & ~INT_TTI;				/* clear busy */
dev_done = dev_done | INT_TTI;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
tti_unit.pos = tti_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;
dev_busy = dev_busy & ~INT_TTI;				/* clear busy */
dev_done = dev_done & ~INT_TTI;				/* clear done, int */
int_req = int_req & ~INT_TTI;
sim_activate (&tti_unit, tti_unit.wait);		/* activate unit */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) tto_unit.buf = AC & 0377;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_TTO;			/* set busy */
	dev_done = dev_done & ~INT_TTO;			/* clear done, int */
	int_req = int_req & ~INT_TTO;
	sim_activate (&tto_unit, tto_unit.wait);	/* activate unit */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_TTO;			/* clear busy */
	dev_done = dev_done & ~INT_TTO;			/* clear done, int */
	int_req = int_req & ~INT_TTO;
	sim_cancel (&tto_unit);				/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 c, temp;

dev_busy = dev_busy & ~INT_TTO;				/* clear busy */
dev_done = dev_done | INT_TTO;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
c = tto_unit.buf & 0177;
if ((tto_unit.flags & UNIT_DASHER) && (c == 031)) c = '\b';
if ((temp = sim_putchar (c)) != SCPE_OK) return temp;
tto_unit.pos = tto_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
dev_busy = dev_busy & ~INT_TTO;				/* clear busy */
dev_done = dev_done & ~INT_TTO;				/* clear done, int */
int_req = int_req & ~INT_TTO;
sim_cancel (&tto_unit);					/* deactivate unit */
return SCPE_OK;
}

t_stat ttx_setmod (UNIT *uptr, int32 value)
{
tti_unit.flags = (tti_unit.flags & ~UNIT_DASHER) | value;
tto_unit.flags = (tto_unit.flags & ~UNIT_DASHER) | value;
return SCPE_OK;
}
