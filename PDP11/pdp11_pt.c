/* pdp11_pt.c: PC11 paper tape reader/punch simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   ptr          paper tape reader
   ptp          paper tape punch

   07-Jul-05    RMS     Removed extraneous externs
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   12-Sep-02    RMS     Split off from pdp11_stddev.c
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"
#define PT_DIS          DEV_DIS
extern int32 int_req;

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define PT_DIS          DEV_DIS
extern int32 int_req[IPL_HLVL];

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define PT_DIS          0
extern int32 int_req[IPL_HLVL];
#endif

#define PTRCSR_IMP      (CSR_ERR+CSR_BUSY+CSR_DONE+CSR_IE) /* paper tape reader */
#define PTRCSR_RW       (CSR_IE)
#define PTPCSR_IMP      (CSR_ERR + CSR_DONE + CSR_IE)   /* paper tape punch */
#define PTPCSR_RW       (CSR_IE)

int32 ptr_csr = 0;                                      /* control/status */
int32 ptr_stopioe = 0;                                  /* stop on error */
int32 ptp_csr = 0;                                      /* control/status */
int32 ptp_stopioe = 0;                                  /* stop on error */

DEVICE ptr_dev, ptp_dev;
t_stat ptr_rd (int32 *data, int32 PA, int32 access);
t_stat ptr_wr (int32 data, int32 PA, int32 access);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, char *ptr);
t_stat ptr_detach (UNIT *uptr);
t_stat ptp_rd (int32 *data, int32 PA, int32 access);
t_stat ptp_wr (int32 data, int32 PA, int32 access);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptp_attach (UNIT *uptr, char *ptr);
t_stat ptp_detach (UNIT *uptr);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
*/

DIB ptr_dib = {
    IOBA_PTR, IOLN_PTR, &ptr_rd, &ptr_wr,
    1, IVCL (PTR), VEC_PTR, { NULL }
    };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { GRDATA (BUF, ptr_unit.buf, DEV_RDX, 8, 0) },
    { GRDATA (CSR, ptr_csr, DEV_RDX, 16, 0) },
    { FLDATA (INT, int_req, INT_V_PTR) },
    { FLDATA (ERR, ptr_csr, CSR_V_ERR) },
    { FLDATA (BUSY, ptr_csr, CSR_V_BUSY) },
    { FLDATA (DONE, ptr_csr, CSR_V_DONE) },
    { FLDATA (IE, ptr_csr, CSR_V_IE) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { FLDATA (DEVDIS, ptr_dev.flags, DEV_V_DIS), REG_HRO },
    { NULL }
    };

MTAB ptr_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &ptr_reset,
    NULL, &ptr_attach, &ptr_detach,
    &ptr_dib, DEV_DISABLE | PT_DIS | DEV_UBUS | DEV_QBUS
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
*/

DIB ptp_dib = {
    IOBA_PTP, IOLN_PTP, &ptp_rd, &ptp_wr,
    1, IVCL (PTP), VEC_PTP, { NULL }
    };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { GRDATA (BUF, ptp_unit.buf, DEV_RDX, 8, 0) },
    { GRDATA (CSR, ptp_csr, DEV_RDX, 16, 0) },
    { FLDATA (INT, int_req, INT_V_PTP) },
    { FLDATA (ERR, ptp_csr, CSR_V_ERR) },
    { FLDATA (DONE, ptp_csr, CSR_V_DONE) },
    { FLDATA (IE, ptp_csr, CSR_V_IE) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &ptp_reset,
    NULL, &ptp_attach, &ptp_detach,
    &ptp_dib, DEV_DISABLE | PT_DIS | DEV_UBUS | DEV_QBUS
    };

/* Paper tape reader I/O address routines */

t_stat ptr_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* ptr csr */
        *data = ptr_csr & PTRCSR_IMP;
        return SCPE_OK;

    case 1:                                             /* ptr buf */
        ptr_csr = ptr_csr & ~CSR_DONE;
        CLR_INT (PTR);
        *data = ptr_unit.buf & 0377;
        return SCPE_OK;
        }

return SCPE_NXM;                                        /* can't get here */
}

t_stat ptr_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* ptr csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            CLR_INT (PTR);
        else if (((ptr_csr & CSR_IE) == 0) && (ptr_csr & (CSR_ERR | CSR_DONE)))
            SET_INT (PTR);
        if (data & CSR_GO) {
            ptr_csr = (ptr_csr & ~CSR_DONE) | CSR_BUSY;
            CLR_INT (PTR);
            if (ptr_unit.flags & UNIT_ATT)              /* data to read? */
                sim_activate (&ptr_unit, ptr_unit.wait);  
            else sim_activate (&ptr_unit, 0);           /* error if not */
            }
        ptr_csr = (ptr_csr & ~PTRCSR_RW) | (data & PTRCSR_RW);
        return SCPE_OK;

    case 1:                                             /* ptr buf */
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;                                        /* can't get here */
}

/* Paper tape reader service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

ptr_csr = (ptr_csr | CSR_ERR) & ~CSR_BUSY;
if (ptr_csr & CSR_IE) SET_INT (PTR);
if ((ptr_unit.flags & UNIT_ATT) == 0)
    return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {
    if (feof (ptr_unit.fileref)) {
        if (ptr_stopioe)
            printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else perror ("PTR I/O error");
    clearerr (ptr_unit.fileref);
    return SCPE_IOERR;
    }
ptr_csr = (ptr_csr | CSR_DONE) & ~CSR_ERR;
ptr_unit.buf = temp & 0377;
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

/* Paper tape reader support routines */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;
ptr_csr = 0;
if ((ptr_unit.flags & UNIT_ATT) == 0)
    ptr_csr = ptr_csr | CSR_ERR;
CLR_INT (PTR);
sim_cancel (&ptr_unit);
return SCPE_OK;
}

t_stat ptr_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptr_unit.flags & UNIT_ATT) == 0)
    ptr_csr = ptr_csr | CSR_ERR;
else ptr_csr = ptr_csr & ~CSR_ERR;
return reason;
}

t_stat ptr_detach (UNIT *uptr)
{
ptr_csr = ptr_csr | CSR_ERR;
return detach_unit (uptr);
}

/* Paper tape punch I/O address routines */

t_stat ptp_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* ptp csr */
        *data = ptp_csr & PTPCSR_IMP;
        return SCPE_OK;

    case 1:                                             /* ptp buf */
        *data = ptp_unit.buf;
        return SCPE_OK;
        }

return SCPE_NXM;                                        /* can't get here */
}

t_stat ptp_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* ptp csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            CLR_INT (PTP);
        else if (((ptp_csr & CSR_IE) == 0) && (ptp_csr & (CSR_ERR | CSR_DONE)))
            SET_INT (PTP);
        ptp_csr = (ptp_csr & ~PTPCSR_RW) | (data & PTPCSR_RW);
        return SCPE_OK;

    case 1:                                             /* ptp buf */
        if ((PA & 1) == 0)
            ptp_unit.buf = data & 0377;
        ptp_csr = ptp_csr & ~CSR_DONE;
        CLR_INT (PTP);
        if (ptp_unit.flags & UNIT_ATT)                  /* file to write? */
            sim_activate (&ptp_unit, ptp_unit.wait);
        else sim_activate (&ptp_unit, 0);               /* error if not */
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;                                        /* can't get here */
}

/* Paper tape punch service */

t_stat ptp_svc (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR | CSR_DONE;
if (ptp_csr & CSR_IE)
    SET_INT (PTP);
if ((ptp_unit.flags & UNIT_ATT) == 0)
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {
    perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_csr = ptp_csr & ~CSR_ERR;
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* Paper tape punch support routines */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
ptp_csr = CSR_DONE;
if ((ptp_unit.flags & UNIT_ATT) == 0)
    ptp_csr = ptp_csr | CSR_ERR;
CLR_INT (PTP);
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat ptp_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
if ((ptp_unit.flags & UNIT_ATT) == 0)
    ptp_csr = ptp_csr | CSR_ERR;
else ptp_csr = ptp_csr & ~CSR_ERR;
return reason;
}

t_stat ptp_detach (UNIT *uptr)
{
ptp_csr = ptp_csr | CSR_ERR;
return detach_unit (uptr);
}
