/* nova_plt.c: NOVA plotter simulator

   Copyright (c) 2000-2004, Robert M. Supnik
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

   plt		plotter

   25-Apr-03	RMS	Revised for extended file support
   03-Oct-02	RMS	Added DIB
   30-May-02	RMS	Widened POS to 32b
   06-Jan-02	RMS	Revised enable/disable support
   26-Apr-01	RMS	Added device enable/disable support
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable;

int32 plt_stopioe = 0;					/* stop on error */

DEVICE plt_dev;
int32 plt (int32 pulse, int32 code, int32 AC);
t_stat plt_svc (UNIT *uptr);
t_stat plt_reset (DEVICE *dptr);

/* PLT data structures

   plt_dev	PLT device descriptor
   plt_unit	PLT unit descriptor
   plt_reg	PLT register list
*/

DIB plt_dib = { DEV_PLT, INT_PLT, PI_PLT, &plt };

UNIT plt_unit = {
	UDATA (&plt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG plt_reg[] = {
	{ ORDATA (BUF, plt_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_PLT) },
	{ FLDATA (DONE, dev_done, INT_V_PLT) },
	{ FLDATA (DISABLE, dev_disable, INT_V_PLT) },
	{ FLDATA (INT, int_req, INT_V_PLT) },
	{ DRDATA (POS, plt_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, plt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, plt_stopioe, 0) },
	{ NULL }  };

DEVICE plt_dev = {
	"PLT", &plt_unit, plt_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &plt_reset,
	NULL, NULL, NULL,
	&plt_dib, DEV_DISABLE };

/* plotter: IOT routine */

int32 plt (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA) plt_unit.buf = AC & 0377;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_PLT;			/* set busy */
	dev_done = dev_done & ~INT_PLT;			/* clear done, int */
	int_req = int_req & ~INT_PLT;
	sim_activate (&plt_unit, plt_unit.wait);	/* activate unit */
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_PLT;			/* clear busy */
	dev_done = dev_done & ~INT_PLT;			/* clear done, int */
	int_req = int_req & ~INT_PLT;
	sim_cancel (&plt_unit);				/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat plt_svc (UNIT *uptr)
{
dev_busy = dev_busy & ~INT_PLT;				/* clear busy */
dev_done = dev_done | INT_PLT;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
if ((plt_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (plt_stopioe, SCPE_UNATT);
if (putc (plt_unit.buf, plt_unit.fileref) == EOF) {
	perror ("PLT I/O error");
	clearerr (plt_unit.fileref);
	return SCPE_IOERR;  }
plt_unit.pos = plt_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat plt_reset (DEVICE *dptr)
{
plt_unit.buf = 0;
dev_busy = dev_busy & ~INT_PLT;				/* clear busy */
dev_done = dev_done & ~INT_PLT;				/* clear done, int */
int_req = int_req & ~INT_PLT;
sim_cancel (&plt_unit);					/* deactivate unit */
return SCPE_OK;
}
