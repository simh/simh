/* nova_tt1.c: NOVA second terminal simulator

   Copyright (c) 1993-2000, Robert M. Supnik
   Written by Bruce Ray and used with his gracious permission.

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

   tti1		second terminal input
   tto1		second terminal output
*/

#include "nova_defs.h"

#define	UNIT_V_DASHER	(UNIT_V_UF + 0)			/* Dasher mode */
#define UNIT_DASHER	(1 << UNIT_V_DASHER)

extern int32 int_req, dev_busy, dev_done, dev_disable;
int32 tti1_stopioe = 0, tto1_stopioe = 0;		/* stop on error */
t_stat tti1_svc (UNIT *uptr);
t_stat tto1_svc (UNIT *uptr);
t_stat tti1_reset (DEVICE *dptr);
t_stat tto1_reset (DEVICE *dptr);
t_stat tti1_attach (UNIT *uptr, char *ptr);
t_stat tti1_detach (UNIT *uptr);
t_stat ttx1_setmod (UNIT *uptr, int32 value);

/* TTI1 data structures

   tti1_dev	TTI1 device descriptor
   tti1_unit	TTI1 unit descriptor
   tti1_reg	TTI1 register list
   ttx1_mod	TTI1/TTO1 modifiers list
*/

UNIT tti1_unit = { UDATA (&tti1_svc, UNIT_SEQ+UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG tti1_reg[] = {
	{ ORDATA (BUF, tti1_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_TTI1) },
	{ FLDATA (DONE, dev_done, INT_V_TTI1) },
	{ FLDATA (DISABLE, dev_disable, INT_V_TTI1) },
	{ FLDATA (INT, int_req, INT_V_TTI1) },
	{ DRDATA (POS, tti1_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tti1_unit.wait, 24), REG_NZ + PV_LEFT },
	{ FLDATA (STOP_IOE, tti1_stopioe, 0) },
	{ FLDATA (MODE, tti1_unit.flags, UNIT_V_DASHER), REG_HRO },
	{ NULL }  };

MTAB ttx1_mod[] = {
	{ UNIT_DASHER, 0, "ANSI", "ANSI", &ttx1_setmod },
	{ UNIT_DASHER, UNIT_DASHER, "Dasher", "DASHER", &ttx1_setmod },
	{ 0 }  };

DEVICE tti1_dev = {
	"TTI1", &tti1_unit, tti1_reg, ttx1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti1_reset,
	NULL, &tti1_attach, &tti1_detach };

/* TTO1 data structures

   tto1_dev	TTO1 device descriptor
   tto1_unit	TTO1 unit descriptor
   tto1_reg	TTO1 register list
*/

UNIT tto1_unit = { UDATA (&tto1_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG tto1_reg[] = {
	{ ORDATA (BUF, tto1_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_TTO1) },
	{ FLDATA (DONE, dev_done, INT_V_TTO1) },
	{ FLDATA (DISABLE, dev_disable, INT_V_TTO1) },
	{ FLDATA (INT, int_req, INT_V_TTO1) },
	{ DRDATA (POS, tto1_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, tto1_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, tto1_stopioe, 0) },
	{ FLDATA (MODE, tto1_unit.flags, UNIT_V_DASHER), REG_HRO },
	{ NULL }  };

DEVICE tto1_dev = {
	"TTO1", &tto1_unit, tto1_reg, ttx1_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto1_reset,
	NULL, NULL, NULL };

/* Terminal input: IOT routine */

int32 tti1 (int32 pulse, int32 code, int32 AC)
{
int32 iodata;

iodata = (code == ioDIA)? tti1_unit.buf & 0377: 0;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_TTI1;			/* set busy */
	dev_done = dev_done & ~INT_TTI1;		/* clear done, int */
	int_req = int_req & ~INT_TTI1;
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_TTI1;		/* clear busy */
	dev_done = dev_done & ~INT_TTI1;		/* clear done, int */
	int_req = int_req & ~INT_TTI1;
	break;  }					/* end switch */
return iodata;
}

/* Unit service */

t_stat tti1_svc (UNIT *uptr)
{
int32 temp;

if ((tti1_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (tti1_stopioe, SCPE_UNATT);
sim_activate (&tti1_unit, tti1_unit.wait);		/* continue poll */
if ((temp = getc (tti1_unit.fileref)) == EOF) {		/* end of file? */
	if (feof (tti1_unit.fileref)) {
		if (tti1_stopioe) printf ("TTI1 end of file\n");
		else return SCPE_OK;  }
	else perror ("TTI1 input error");
	clearerr (tti1_unit.fileref);
	return SCPE_IOERR;  }
tti1_unit.buf = temp & 0177;
if ((tti1_unit.flags & UNIT_DASHER) && (tti1_unit.buf == '\r'))
	tti1_unit.buf = '\n';				/* Dasher: cr -> nl */
dev_busy = dev_busy & ~INT_TTI1;			/* clear busy */
dev_done = dev_done | INT_TTI1;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
tti1_unit.pos = tti1_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tti1_reset (DEVICE *dptr)
{
tti1_unit.buf = 0;
dev_busy = dev_busy & ~INT_TTI1;			/* clear busy */
dev_done = dev_done & ~INT_TTI1;			/* clear done, int */
int_req = int_req & ~INT_TTI1;
if (tti1_unit.flags & UNIT_ATT)				/* attached? */
	sim_activate (&tti1_unit, tti1_unit.wait);
else sim_cancel (&tti1_unit);
return SCPE_OK;
}

/* Attach routine */

t_stat tti1_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if (reason == SCPE_OK) sim_activate (uptr, uptr -> wait);
return reason;
}

/* Detach routine */

t_stat tti1_detach (UNIT *uptr)
{
sim_cancel (uptr);
return detach_unit (uptr);
}

/* Terminal output: IOT routine */

int32 tto1 (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) tto1_unit.buf = AC & 0377;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_TTO1;			/* set busy */
	dev_done = dev_done & ~INT_TTO1;		/* clear done, int */
	int_req = int_req & ~INT_TTO1;
	sim_activate (&tto1_unit, tto1_unit.wait);	/* activate unit */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_TTO1;		/* clear busy */
	dev_done = dev_done & ~INT_TTO1;		/* clear done, int */
	int_req = int_req & ~INT_TTO1;
	sim_cancel (&tto1_unit);			/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat tto1_svc (UNIT *uptr)
{
int32 c ;

dev_busy = dev_busy & ~INT_TTO1;			/* clear busy */
dev_done = dev_done | INT_TTO1;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
if ((tto1_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (tto1_stopioe, SCPE_UNATT);
c = tto1_unit.buf & 0177;
if ((tto1_unit.flags & UNIT_DASHER) && (c == 031)) c = '\b';
if (putc (c, tto1_unit.fileref) == EOF) {
	perror ("TTO1 output error");
	clearerr (tto1_unit.fileref);
	return SCPE_IOERR;  }
tto1_unit.pos = tto1_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tto1_reset (DEVICE *dptr)
{
tto1_unit.buf = 0;
dev_busy = dev_busy & ~INT_TTO1;			/* clear busy */
dev_done = dev_done & ~INT_TTO1;			/* clear done, int */
int_req = int_req & ~INT_TTO1;
sim_cancel (&tto1_unit);				/* deactivate unit */
return SCPE_OK;
}

t_stat ttx1_setmod (UNIT *uptr, int32 value)
{
tti1_unit.flags = (tti1_unit.flags & ~UNIT_DASHER) | value;
tto1_unit.flags = (tto1_unit.flags & ~UNIT_DASHER) | value;
return SCPE_OK;
}
