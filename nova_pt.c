/* nova_pt.c: NOVA paper tape read/punch simulator

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

   ptr		paper tape reader
   ptp		paper tape punch
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable;
int32 ptr_stopioe = 0, ptp_stopioe = 0;			/* stop on error */
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (BUF, ptr_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_PTR) },
	{ FLDATA (DONE, dev_done, INT_V_PTR) },
	{ FLDATA (DISABLE, dev_disable, INT_V_PTR) },
	{ FLDATA (INT, int_req, INT_V_PTR) },
	{ DRDATA (POS, ptr_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	NULL, NULL, NULL };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit descriptor
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_PTP) },
	{ FLDATA (DONE, dev_done, INT_V_PTP) },
	{ FLDATA (DISABLE, dev_disable, INT_V_PTP) },
	{ FLDATA (INT, int_req, INT_V_PTP) },
	{ DRDATA (POS, ptp_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ NULL }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL };

/* Paper tape reader: IOT routine */

int32 ptr (int32 pulse, int32 code, int32 AC)
{
int32 iodata;

iodata = (code == ioDIA)? ptr_unit.buf & 0377: 0;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_PTR;			/* set busy */
	dev_done = dev_done & ~INT_PTR;			/* clear done, int */
	int_req = int_req & ~INT_PTR;
	sim_activate (&ptr_unit, ptr_unit.wait);	/* activate unit */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_PTR;			/* clear busy */
	dev_done = dev_done & ~INT_PTR;			/* clear done, int */
	int_req = int_req & ~INT_PTR;
	sim_cancel (&ptr_unit);				/* deactivate unit */
	break;  }					/* end switch */
return iodata;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {		/* end of file? */
	if (feof (ptr_unit.fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
dev_busy = dev_busy & ~INT_PTR;				/* clear busy */
dev_done = dev_done | INT_PTR;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
ptr_unit.buf = temp & 0377;
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;
dev_busy = dev_busy & ~INT_PTR;				/* clear busy */
dev_done = dev_done & ~INT_PTR;				/* clear done, int */
int_req = int_req & ~INT_PTR;
sim_cancel (&ptr_unit);					/* deactivate unit */
return SCPE_OK;
}

/* Paper tape punch: IOT routine */

int32 ptp (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) ptp_unit.buf = AC & 0377;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_PTP;			/* set busy */
	dev_done = dev_done & ~INT_PTP;			/* clear done, int */
	int_req = int_req & ~INT_PTP;
	sim_activate (&ptp_unit, ptp_unit.wait);	/* activate unit */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_PTP;			/* clear busy */
	dev_done = dev_done & ~INT_PTP;			/* clear done, int */
	int_req = int_req & ~INT_PTP;
	sim_cancel (&ptp_unit);				/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
dev_busy = dev_busy & ~INT_PTP;				/* clear busy */
dev_done = dev_done | INT_PTP;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
if ((ptp_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
dev_busy = dev_busy & ~INT_PTP;				/* clear busy */
dev_done = dev_done & ~INT_PTP;				/* clear done, int */
int_req = int_req & ~INT_PTP;
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}
