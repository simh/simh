/* pdp10_pt.c: PDP-10 Unibus paper tape reader/punch simulator

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

   ptr		paper tape reader
   ptp		paper tape punch

   07-Sep-01	RMS	Revised disable mechanism
*/

#include "pdp10_defs.h"

#define PTRCSR_IMP	(CSR_ERR+CSR_BUSY+CSR_DONE+CSR_IE) /* paper tape reader */
#define PTRCSR_RW	(CSR_IE)
#define PTPCSR_IMP	(CSR_ERR + CSR_DONE + CSR_IE)	/* paper tape punch */
#define PTPCSR_RW	(CSR_IE)

extern int32 int_req;
int32 ptr_csr = 0;					/* control/status */
int32 ptr_stopioe = 0;					/* stop on error */
int32 ptp_csr = 0;					/* control/status */
int32 ptp_stopioe = 0;					/* stop on error */
int32 pt_enb = 0;					/* device enable */
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, char *ptr);
t_stat ptr_detach (UNIT *uptr);
t_stat ptp_attach (UNIT *uptr, char *ptr);
t_stat ptp_detach (UNIT *uptr);

/* PTR data structures

   ptr_dev	PTR device descriptor
   ptr_unit	PTR unit descriptor
   ptr_reg	PTR register list
*/

UNIT ptr_unit = {
	UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_IN_WAIT };

REG ptr_reg[] = {
	{ ORDATA (CSR, ptr_csr, 16) },
	{ ORDATA (BUF, ptr_unit.buf, 8) },
	{ FLDATA (INT, int_req, INT_V_PTR) },
	{ FLDATA (ERR, ptr_csr, CSR_V_ERR) },
	{ FLDATA (BUSY, ptr_csr, CSR_V_BUSY) },
	{ FLDATA (DONE, ptr_csr, CSR_V_DONE) },
	{ FLDATA (IE, ptr_csr, CSR_V_IE) },
	{ DRDATA (POS, ptr_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptr_stopioe, 0) },
	{ FLDATA (*DEVENB, pt_enb, 0), REG_HRO },
	{ NULL }  };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	NULL, &ptr_attach, &ptr_detach };

/* PTP data structures

   ptp_dev	PTP device descriptor
   ptp_unit	PTP unit descriptor
   ptp_reg	PTP register list
*/

UNIT ptp_unit = {
	UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG ptp_reg[] = {
	{ ORDATA (BUF, ptp_unit.buf, 8) },
	{ ORDATA (CSR, ptp_csr, 16) },
	{ FLDATA (INT, int_req, INT_V_PTP) },
	{ FLDATA (ERR, ptp_csr, CSR_V_ERR) },
	{ FLDATA (DONE, ptp_csr, CSR_V_DONE) },
	{ FLDATA (IE, ptp_csr, CSR_V_IE) },
	{ DRDATA (POS, ptp_unit.pos, 31), PV_LEFT },
	{ DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ptp_stopioe, 0) },
	{ FLDATA (*DEVENB, pt_enb, 0), REG_HRO },
	{ NULL }  };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, &ptp_attach, &ptp_detach };

/* Standard I/O dispatch routine, I/O addresses 17777550-17777557

   17777550		ptr CSR
   17777552		ptr buffer
   17777554		ptp CSR
   17777556		ptp buffer

   Note: Word access routines filter out odd addresses.  Thus,
   an odd address implies an (odd) byte access.
*/

t_stat pt_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* decode PA<2:1> */
case 0:							/* ptr csr */
	*data = ptr_csr & PTRCSR_IMP;
	return SCPE_OK;
case 1:							/* ptr buf */
	ptr_csr = ptr_csr & ~CSR_DONE;
	int_req = int_req & ~INT_PTR;
	*data = ptr_unit.buf & 0377;
	return SCPE_OK;
case 2:							/* ptp csr */
	*data = ptp_csr & PTPCSR_IMP;
	return SCPE_OK;
case 3:							/* ptp buf */
	*data = ptp_unit.buf;
	return SCPE_OK;  }
return SCPE_NXM;					/* can't get here */
}

t_stat pt_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {				/* decode PA<2:1> */
case 0:							/* ptr csr */
	if (PA & 1) return SCPE_OK;
	if ((data & CSR_IE) == 0) int_req = int_req & ~INT_PTR;
	else if (((ptr_csr & CSR_IE) == 0) && (ptr_csr & (CSR_ERR | CSR_DONE)))
		int_req = int_req | INT_PTR;
	if (data & CSR_GO) {
		ptr_csr = (ptr_csr & ~CSR_DONE) | CSR_BUSY;
		int_req = int_req & ~INT_PTR;
		if (ptr_unit.flags & UNIT_ATT)		/* data to read? */
			sim_activate (&ptr_unit, ptr_unit.wait);  
		else sim_activate (&ptr_unit, 0);  }	/* error if not */
	ptr_csr = (ptr_csr & ~PTRCSR_RW) | (data & PTRCSR_RW);
	return SCPE_OK;
case 1:							/* ptr buf */
	return SCPE_OK;
case 2:							/* ptp csr */
	if (PA & 1) return SCPE_OK;
	if ((data & CSR_IE) == 0) int_req = int_req & ~INT_PTP;
	else if (((ptp_csr & CSR_IE) == 0) && (ptp_csr & (CSR_ERR | CSR_DONE)))
		int_req = int_req | INT_PTP;
	ptp_csr = (ptp_csr & ~PTPCSR_RW) | (data & PTPCSR_RW);
	return SCPE_OK;
case 3:							/* ptp buf */
	if ((PA & 1) == 0) ptp_unit.buf = data & 0377;
	ptp_csr = ptp_csr & ~CSR_DONE;
	int_req = int_req & ~INT_PTP;
	if (ptp_unit.flags & UNIT_ATT)			/* file to write? */
		sim_activate (&ptp_unit, ptp_unit.wait);
	else sim_activate (&ptp_unit, 0);		/* error if not */
	return SCPE_OK;  }				/* end switch PA */
return SCPE_NXM;					/* can't get here */
}

/* Paper tape reader routines

   ptr_svc	process event (character ready)
   ptr_reset	process reset
   ptr_attach	process attach
   ptr_detach	process detach
*/

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

ptr_csr = (ptr_csr | CSR_ERR) & ~CSR_BUSY;
if (ptr_csr & CSR_IE) int_req = int_req | INT_PTR;
if ((ptr_unit.flags & UNIT_ATT) == 0)
	return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {
	if (feof (ptr_unit.fileref)) {
		if (ptr_stopioe) printf ("PTR end of file\n");
		else return SCPE_OK;  }
	else perror ("PTR I/O error");
	clearerr (ptr_unit.fileref);
	return SCPE_IOERR;  }
ptr_csr = (ptr_csr | CSR_DONE) & ~CSR_ERR;
ptr_unit.buf = temp & 0377;
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;
ptr_csr = 0;
if ((ptr_unit.flags & UNIT_ATT) == 0) ptr_csr = ptr_csr | CSR_ERR;
int_req = int_req & ~INT_PTR;
sim_cancel (&ptr_unit);
return SCPE_OK;
}

t_stat ptr_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptr_unit.flags & UNIT_ATT) == 0) ptr_csr = ptr_csr | CSR_ERR;
else ptr_csr = ptr_csr & ~CSR_ERR;
return reason;
}

t_stat ptr_detach (UNIT *uptr)
{
ptr_csr = ptr_csr | CSR_ERR;
return detach_unit (uptr);
}

/* Paper tape punch routines

   ptp_svc	process event (character punched)
   ptp_reset	process reset
   ptp_attach	process attach
   ptp_detach	process detach
*/

t_stat ptp_svc (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR | CSR_DONE;
if (ptp_csr & CSR_IE) int_req = int_req | INT_PTP;
if ((ptp_unit.flags & UNIT_ATT) == 0)
	return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {
	perror ("PTP I/O error");
	clearerr (ptp_unit.fileref);
	return SCPE_IOERR;  }
ptp_csr = ptp_csr & ~CSR_ERR;
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
ptp_csr = CSR_DONE;
if ((ptp_unit.flags & UNIT_ATT) == 0) ptp_csr = ptp_csr | CSR_ERR;
int_req = int_req & ~INT_PTP;
sim_cancel (&ptp_unit);					/* deactivate unit */
return SCPE_OK;
}

t_stat ptp_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptp_unit.flags & UNIT_ATT) == 0) ptp_csr = ptp_csr | CSR_ERR;
else ptp_csr = ptp_csr & ~CSR_ERR;
return reason;
}

t_stat ptp_detach (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR;
return detach_unit (uptr);
}
