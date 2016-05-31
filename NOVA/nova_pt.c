/* nova_pt.c: NOVA paper tape read/punch simulator

   Copyright (c) 1993-2016, Robert M. Supnik

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

   13-May-16    RMS     Lengthened wait time for DCC BASIC timing error
   28-Mar-15    RMS     Revised to use sim_printf
   04-Jul-07    BKR     added PTR and PTP device DISABLE capability,
                        added 7B/8B support PTR and PTP (default is 8B),
                        DEV_SET/CLR macros now used,
                        PTR and PTP can now be DISABLED
   25-Apr-03    RMS     Revised for extended file support
   03-Oct-02    RMS     Added DIBs
   30-May-02    RMS     Widened POS to 32b
   29-Nov-01    RMS     Added read only unit support


Notes:
    - data masked to 7- or 8- bits, based on 7B or 8B, default is 8-bits
    - register TIME is the delay between character read or write operations
    - register POS show the number of characters read from or sent to the PTR or PTP
    - register STOP_IOE determines return value issued if output to unattached PTR or PTP is attempted
*/

#include "nova_defs.h"

extern int32 int_req, dev_busy, dev_done, dev_disable ;
extern int32 SR ;

extern t_stat cpu_boot(int32 unitno, DEVICE * dptr ) ;


int32 ptr_stopioe = 0, ptp_stopioe = 0;                 /* stop on error */

int32 ptr (int32 pulse, int32 code, int32 AC);
int32 ptp (int32 pulse, int32 code, int32 AC);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);


/* 7 or 8 bit data mask support for either device  */

#define UNIT_V_8B   (UNIT_V_UF + 0)                     /* 8b output */
#define UNIT_8B     (1 << UNIT_V_8B)


/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
*/

DIB ptr_dib = { DEV_PTR, INT_PTR, PI_PTR, &ptr };

UNIT ptr_unit = {   /* 2007-May-30, bkr */
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE+UNIT_8B, 0), 300
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_PTR) },
    { FLDATA (DONE, dev_done, INT_V_PTR) },
    { FLDATA (DISABLE, dev_disable, INT_V_PTR) },
    { FLDATA (INT, int_req, INT_V_PTR) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB ptr_mod[] =    /* 2007-May-30, bkr */
    {
    { UNIT_8B,       0, "7b", "7B", NULL },
    { UNIT_8B, UNIT_8B, "8b", "8B", NULL },
    {       0,       0, NULL, NULL, NULL }
    } ;

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod /* 2007-May-30, bkr */,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, NULL, NULL,
    &ptr_dib, DEV_DISABLE   /* 2007-May-30, bkr */
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
*/

DIB ptp_dib = { DEV_PTP, INT_PTP, PI_PTP, &ptp };

UNIT ptp_unit =
    {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_8B, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_PTP) },
    { FLDATA (DONE, dev_done, INT_V_PTP) },
    { FLDATA (DISABLE, dev_disable, INT_V_PTP) },
    { FLDATA (INT, int_req, INT_V_PTP) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] =
    {
    { UNIT_8B,       0, "7b", "7B", NULL },
    { UNIT_8B, UNIT_8B, "8b", "8B", NULL },
    {       0,       0, NULL, NULL, NULL }
    } ;

DEVICE ptp_dev =
    {
    "PTP", &ptp_unit, ptp_reg, ptp_mod /* 2007-May-30, bkr */,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    &ptp_dib, DEV_DISABLE   /* 2007-May-30, bkr */
    };


/* Paper tape reader: IOT routine */

int32 ptr (int32 pulse, int32 code, int32 AC)
{
int32   iodata;

iodata = (code == ioDIA)?
              ptr_unit.buf & 0377
            : 0;
switch (pulse)
    {                                                   /* decode IR<8:9> */
  case iopS:                                            /* start */
    DEV_SET_BUSY( INT_PTR ) ;
    DEV_CLR_DONE( INT_PTR ) ;
    DEV_UPDATE_INTR ;
    sim_activate (&ptr_unit, ptr_unit.wait);            /* activate unit */
    break;

  case iopC:                                            /* clear */
    DEV_CLR_BUSY( INT_PTR ) ;
    DEV_CLR_DONE( INT_PTR ) ;
    DEV_UPDATE_INTR ;
    sim_cancel (&ptr_unit);                             /* deactivate unit */
    break;
    }                                                   /* end switch */

return iodata;
}


/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32   temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IORETURN (ptr_stopioe, SCPE_UNATT);
if ((temp = getc (ptr_unit.fileref)) == EOF) {          /* end of file? */
    if (feof (ptr_unit.fileref)) {
        if (ptr_stopioe)
            sim_printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else sim_perror ("PTR I/O error");
    clearerr (ptr_unit.fileref);
    return SCPE_IOERR;
    }

DEV_CLR_BUSY( INT_PTR ) ;
DEV_SET_DONE( INT_PTR ) ;
DEV_UPDATE_INTR ;
ptr_unit.buf = temp & ((ptr_unit.flags & UNIT_8B)? 0377: 0177);
++(ptr_unit.pos);
return SCPE_OK;
}


/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;                                       /* <not DG compatible> */
DEV_CLR_BUSY( INT_PTR ) ;
DEV_CLR_DONE( INT_PTR ) ;
DEV_UPDATE_INTR ;
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}


/* Boot routine */

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
ptr_reset( dptr ) ;
/*  set position to 0?  */
cpu_boot( unitno, dptr ) ;
SR = /* low-speed: no high-order bit set */ DEV_PTR ;
return ( SCPE_OK );
}    /*  end of 'ptr_boot'  */





/* Paper tape punch: IOT routine */

int32 ptp (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA)
    ptp_unit.buf = AC & 0377;

switch (pulse)
    {                                                   /* decode IR<8:9> */
  case iopS:                                            /* start */
    DEV_SET_BUSY( INT_PTP ) ;
    DEV_CLR_DONE( INT_PTP ) ;
    DEV_UPDATE_INTR ;
    sim_activate (&ptp_unit, ptp_unit.wait);            /* activate unit */
    break;

  case iopC:                                            /* clear */
    DEV_CLR_BUSY( INT_PTP ) ;
    DEV_CLR_DONE( INT_PTP ) ;
    DEV_UPDATE_INTR ;
    sim_cancel (&ptp_unit);                             /* deactivate unit */
    break;
    }                                                   /* end switch */

return 0;
}


/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
DEV_CLR_BUSY( INT_PTP ) ;
DEV_SET_DONE( INT_PTP ) ;
DEV_UPDATE_INTR ;
if ((ptp_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc ((ptp_unit.buf & ((ptp_unit.flags & UNIT_8B)? 0377: 0177)), ptp_unit.fileref) == EOF) {
    sim_perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
++(ptp_unit.pos);
return SCPE_OK;
}


/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;                                       /* <not DG compatible> */
DEV_CLR_BUSY( INT_PTP ) ;
DEV_CLR_DONE( INT_PTP ) ;
DEV_UPDATE_INTR ;
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}
