/* nova_lp.c: NOVA line printer simulator

   Copyright (c) 1993-2003, Robert M. Supnik

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

   lpt		line printer

   25-Apr-03	RMS	Revised for extended file support
   30-May-02	RMS	Widened POS to 32b
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable;

int32 lpt_stopioe = 0;					/* stop on error */

int32 lpt (int32 pulse, int32 code, int32 AC);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);

/* LPT data structures

   lpt_dev	LPT device descriptor
   lpt_unit	LPT unit descriptor
   lpt_reg	LPT register list
*/

DIB lpt_dib = { DEV_LPT, INT_LPT, PI_LPT, &lpt };

UNIT lpt_unit = {
	UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG lpt_reg[] = {
	{ ORDATA (BUF, lpt_unit.buf, 8) },
	{ FLDATA (BUSY, dev_busy, INT_V_LPT) },
	{ FLDATA (DONE, dev_done, INT_V_LPT) },
	{ FLDATA (DISABLE, dev_disable, INT_V_LPT) },
	{ FLDATA (INT, int_req, INT_V_LPT) },
	{ DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
	{ DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, lpt_stopioe, 0) },
	{ NULL }  };

DEVICE lpt_dev = {
	"LPT", &lpt_unit, lpt_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &lpt_reset,
	NULL, NULL, NULL,
	&lpt_dib, DEV_DISABLE };

/* IOT routine */

int32 lpt (int32 pulse, int32 code, int32 AC)
{

if (code == ioDOA) lpt_unit.buf = AC & 0177;
switch (pulse) {					/* decode IR<8:9> */
case iopS: 						/* start */
	dev_busy = dev_busy | INT_LPT;			/* set busy */
	dev_done = dev_done & ~INT_LPT;			/* clear done, int */
	int_req = int_req & ~INT_LPT;
	if ((lpt_unit.buf != 015) && (lpt_unit.buf != 014) &&
	    (lpt_unit.buf != 012))
	    return (lpt_svc (&lpt_unit) << IOT_V_REASON);
	sim_activate (&lpt_unit, lpt_unit.wait);
	break;
case iopC:						/* clear */
	dev_busy = dev_busy & ~INT_LPT;			/* clear busy */
	dev_done = dev_done & ~INT_LPT;			/* clear done, int */
	int_req = int_req & ~INT_LPT;
	sim_cancel (&lpt_unit);				/* deactivate unit */
	break;  }					/* end switch */
return 0;
}

/* Unit service */

t_stat lpt_svc (UNIT *uptr)
{
dev_busy = dev_busy & ~INT_LPT;				/* clear busy */
dev_done = dev_done | INT_LPT;				/* set done */
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
if ((lpt_unit.flags & UNIT_ATT) == 0)			/* attached? */
	return IORETURN (lpt_stopioe, SCPE_UNATT);
if (putc (lpt_unit.buf, lpt_unit.fileref) == EOF) {
	perror ("LPT I/O error");
	clearerr (lpt_unit.fileref);
	return SCPE_IOERR;  }
lpt_unit.pos = lpt_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
lpt_unit.buf = 0;
dev_busy = dev_busy & ~INT_LPT;				/* clear busy */
dev_done = dev_done & ~INT_LPT;				/* clear done, int */
int_req = int_req & ~INT_LPT;
sim_cancel (&lpt_unit);					/* deactivate unit */
return SCPE_OK;
}
