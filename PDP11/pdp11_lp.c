/* pdp11_lp.c: PDP-11 line printer simulator

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

   lpt          LP11 line printer

   19-Jan-07    RMS     Added UNIT_TEXT flag
   07-Jul-05    RMS     Removed extraneous externs
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   29-Sep-02    RMS     Added vector change/display support
                        New data structures
   30-May-02    RMS     Widened POS to 32b
   06-Jan-02    RMS     Added enable/disable support
   09-Nov-01    RMS     Added VAX support
   07-Sep-01    RMS     Revised interrupt mechanism
   30-Oct-00    RMS     Standardized register naming
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "LP11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#define LPTCSR_IMP      (CSR_ERR + CSR_DONE + CSR_IE)   /* implemented */
#define LPTCSR_RW       (CSR_IE)                        /* read/write */

extern int32 int_req[IPL_HLVL];

int32 lpt_csr = 0;                                      /* control/status */
int32 lpt_stopioe = 0;                                  /* stop on error */

DEVICE lpt_dev;
t_stat lpt_rd (int32 *data, int32 PA, int32 access);
t_stat lpt_wr (int32 data, int32 PA, int32 access);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, char *ptr);
t_stat lpt_detach (UNIT *uptr);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = {
    IOBA_LPT, IOLN_LPT, &lpt_rd, &lpt_wr,
    1, IVCL (LPT), VEC_LPT, { NULL }
    };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG lpt_reg[] = {
    { GRDATA (BUF, lpt_unit.buf, DEV_RDX, 8, 0) },
    { GRDATA (CSR, lpt_csr, DEV_RDX, 16, 0) },
    { FLDATA (INT, IREQ (LPT), INT_V_LPT) },
    { FLDATA (ERR, lpt_csr, CSR_V_ERR) },
    { FLDATA (DONE, lpt_csr, CSR_V_DONE) },
    { FLDATA (IE, lpt_csr, CSR_V_IE) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lpt_stopioe, 0) },
    { GRDATA (DEVADDR, lpt_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, lpt_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB lpt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, DEV_RDX, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    &lpt_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS
    };

/* Line printer routines

   lpt_rd       I/O page read
   lpt_wr       I/O page write
   lpt_svc      process event (printer ready)
   lpt_reset    process reset
   lpt_attach   process attach
   lpt_detach   process detach
*/

t_stat lpt_rd (int32 *data, int32 PA, int32 access)
{
if ((PA & 02) == 0)                                     /* csr */
    *data = lpt_csr & LPTCSR_IMP;
else *data = lpt_unit.buf;                              /* buffer */
return SCPE_OK;
}

t_stat lpt_wr (int32 data, int32 PA, int32 access)
{
if ((PA & 02) == 0) {                                   /* csr */
    if (PA & 1)
        return SCPE_OK;
    if ((data & CSR_IE) == 0)
        CLR_INT (LPT);
    else if ((lpt_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
        SET_INT (LPT);
    lpt_csr = (lpt_csr & ~LPTCSR_RW) | (data & LPTCSR_RW);
    }
else {                                                  /* buffer */
    if ((PA & 1) == 0)
        lpt_unit.buf = data & 0177;
    lpt_csr = lpt_csr & ~CSR_DONE;
    CLR_INT (LPT);
    if ((lpt_unit.buf == 015) || (lpt_unit.buf == 014) ||
        (lpt_unit.buf == 012)) sim_activate (&lpt_unit, lpt_unit.wait);
    else sim_activate (&lpt_unit, 0);
    }
return SCPE_OK;
}

t_stat lpt_svc (UNIT *uptr)
{
lpt_csr = lpt_csr | CSR_ERR | CSR_DONE;
if (lpt_csr & CSR_IE)
    SET_INT (LPT);
if ((uptr->flags & UNIT_ATT) == 0)
    return IORETURN (lpt_stopioe, SCPE_UNATT);
fputc (uptr->buf & 0177, uptr->fileref);
uptr->pos = ftell (uptr->fileref);
if (ferror (uptr->fileref)) {
    perror ("LPT I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
lpt_csr = lpt_csr & ~CSR_ERR;
return SCPE_OK;
}

t_stat lpt_reset (DEVICE *dptr)
{
lpt_unit.buf = 0;
lpt_csr = CSR_DONE;
if ((lpt_unit.flags & UNIT_ATT) == 0)
    lpt_csr = lpt_csr | CSR_ERR;
CLR_INT (LPT);
sim_cancel (&lpt_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

lpt_csr = lpt_csr & ~CSR_ERR;
reason = attach_unit (uptr, cptr);
if ((lpt_unit.flags & UNIT_ATT) == 0)
    lpt_csr = lpt_csr | CSR_ERR;
return reason;
}

t_stat lpt_detach (UNIT *uptr)
{
lpt_csr = lpt_csr | CSR_ERR;
return detach_unit (uptr);
}
