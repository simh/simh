/* pdp8_tt.c: PDP-8 console terminal simulator

   Copyright (c) 1993-1999, Robert M Supnik

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

#include "pdp8_defs.h"
#include <ctype.h>

#define UNIT_V_UC	(UNIT_V_UF + 0)			/* UC only */
#define UNIT_UC		(1 << UNIT_V_UC)
extern int32 int_req, dev_done, dev_enable, stop_inst;
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
extern t_stat sim_activate (UNIT *uptr, int32 delay);
extern t_stat sim_cancel (UNIT *uptr);
extern t_stat sim_poll_kbd (void);
extern t_stat sim_putchar (int32 out);

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit descriptor
   tti_reg	TTI register list
   tti_mod	TTI modifiers list
*/

UNIT tti_unit = { UDATA (&tti_svc, UNIT_UC, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
	{ ORDATA (BUF, tti_unit.buf, 8) },
	{ FLDATA (DONE, dev_done, INT_V_TTI) },
	{ FLDATA (ENABLE, dev_enable, INT_V_TTI) },
	{ FLDATA (INT, int_req, INT_V_TTI) },
	{ DRDATA (POS, tti_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (UC, tti_unit.flags, UNIT_V_UC), REG_HRO },
	{ NULL }  };

MTAB tti_mod[] = {
	{ UNIT_UC, 0, "lower case", "LC", NULL },
	{ UNIT_UC, UNIT_UC, "upper case", "UC", NULL },
	{ 0 }  };

DEVICE tti_dev = {
	"TTI", &tti_unit, tti_reg, tti_mod,
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
	{ FLDATA (DONE, dev_done, INT_V_TTO) },
	{ FLDATA (ENABLE, dev_enable, INT_V_TTO) },
	{ FLDATA (INT, int_req, INT_V_TTO) },
	{ DRDATA (POS, tto_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
	{ NULL }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto_reset, 
	NULL, NULL, NULL };

/* Terminal input: IOT routine */

int32 tti (int32 pulse, int32 AC)
{
switch (pulse) {					/* decode IR<9:11> */
case 0: 						/* KCF */
	dev_done = dev_done & ~INT_TTI;			/* clear flag */
	int_req = int_req & ~INT_TTI;
	return AC;
case 1:							/* KSF */
	return (dev_done & INT_TTI)? IOT_SKP + AC: AC;
case 2:							/* KCC */
	dev_done = dev_done & ~INT_TTI;			/* clear flag */
	int_req = int_req & ~INT_TTI;
	return 0;					/* clear AC */
case 4:							/* KRS */
	return (AC | tti_unit.buf);			/* return buffer */
case 5:							/* KIE */
	if (AC & 1) dev_enable = dev_enable | (INT_TTI+INT_TTO);
	else dev_enable = dev_enable & ~(INT_TTI+INT_TTO);
	int_req = INT_UPDATE;				/* update interrupts */
	return AC;
case 6:							/* KRB */
	dev_done = dev_done & ~INT_TTI;			/* clear flag */
	int_req = int_req & ~INT_TTI;
	return (tti_unit.buf);				/* return buffer */
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);		/* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;	/* no char or error? */
temp = temp & 0177;
if ((tti_unit.flags & UNIT_UC) && islower (temp))
	temp = toupper (temp);
tti_unit.buf = temp | 0200;				/* got char */
dev_done = dev_done | INT_TTI;				/* set done */
int_req = INT_UPDATE;					/* update interrupts */
tti_unit.pos = tti_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tti_unit.buf = 0;
dev_done = dev_done & ~INT_TTI;				/* clear done, int */
int_req = int_req & ~INT_TTI;
dev_enable = dev_enable | INT_TTI;			/* set enable */
sim_activate (&tti_unit, tti_unit.wait);		/* activate unit */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 pulse, int32 AC)
{
switch (pulse) {					/* decode IR<9:11> */
case 0: 						/* TLF */
	dev_done = dev_done | INT_TTO;			/* set flag */
	int_req = INT_UPDATE;				/* update interrupts */
	return AC;
case 1:							/* TSF */
	return (dev_done & INT_TTO)? IOT_SKP + AC: AC;
case 2:							/* TCF */
	dev_done = dev_done & ~INT_TTO;			/* clear flag */
	int_req = int_req & ~INT_TTO;			/* clear int req */
	return AC;
case 5:							/* SPI */
	return (int_req & (INT_TTI+INT_TTO))? IOT_SKP + AC: AC;
case 6:							/* TLS */
	dev_done = dev_done & ~INT_TTO;			/* clear flag */
	int_req = int_req & ~INT_TTO;			/* clear int req */
case 4:							/* TPC */
	sim_activate (&tto_unit, tto_unit.wait);	/* activate unit */
	tto_unit.buf = AC;				/* load buffer */
	return AC;
default:
	return (stop_inst << IOT_V_REASON) + AC;  }	/* end switch */
}

/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32 temp;

dev_done = dev_done | INT_TTO;				/* set done */
int_req = INT_UPDATE;					/* update interrupts */
if ((temp = sim_putchar (tto_unit.buf & 0177)) != SCPE_OK) return temp;
tto_unit.pos = tto_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;
dev_done = dev_done & ~INT_TTO;				/* clear done, int */
int_req = int_req & ~INT_TTO;
dev_enable = dev_enable | INT_TTO;			/* set enable */
sim_cancel (&tto_unit);					/* deactivate unit */
return SCPE_OK;
}
