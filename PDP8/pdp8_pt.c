/* pdp8_pt.c: PDP-8 paper tape reader/punch simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   ptr,ptp      PC8E paper tape reader/punch

   17-Mar-13    RMS     Modified to use central set_bootpc routine
   25-Apr-03    RMS     Revised for extended file support
   04-Oct-02    RMS     Added DIBs
   30-May-02    RMS     Widened POS to 32b
   30-Nov-01    RMS     Added read only unit support
   30-Mar-98    RMS     Added RIM loader as PTR bootstrap
*/

#include "pdp8_defs.h"

extern int32 int_req, int_enable, dev_done, stop_inst;

int32 ptr_stopioe = 0, ptp_stopioe = 0;                 /* stop on error */

int32 ptr (int32 IR, int32 AC);
int32 ptp (int32 IR, int32 AC);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
*/

DIB ptr_dib = { DEV_PTR, 1, { &ptr } };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 8) },
    { FLDATA (DONE, dev_done, INT_V_PTR) },
    { FLDATA (ENABLE, int_enable, INT_V_PTR) },
    { FLDATA (INT, int_req, INT_V_PTR) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB ptr_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_dev },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, NULL, NULL,
    &ptr_dib, 0 };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
*/

DIB ptp_dib = { DEV_PTP, 1, { &ptp } };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { FLDATA (DONE, dev_done, INT_V_PTP) },
    { FLDATA (ENABLE, int_enable, INT_V_PTP) },
    { FLDATA (INT, int_req, INT_V_PTP) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", NULL, NULL, &show_dev },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    &ptp_dib, 0
    };

/* Paper tape reader: IOT routine */

int32 ptr (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* RPE */
        int_enable = int_enable | (INT_PTR+INT_PTP);    /* set enable */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 1:                                             /* RSF */
        return (dev_done & INT_PTR)? IOT_SKP + AC: AC;  

    case 6:                                             /* RFC!RRB */
        sim_activate (&ptr_unit, ptr_unit.wait);
    case 2:                                             /* RRB */
        dev_done = dev_done & ~INT_PTR;                 /* clear flag */
        int_req = int_req & ~INT_PTR;                   /* clear int req */
        return (AC | ptr_unit.buf);                     /* or data to AC */

    case 4:                                             /* RFC */
        sim_activate (&ptr_unit, ptr_unit.wait);
        dev_done = dev_done & ~INT_PTR;                 /* clear flag */
        int_req = int_req & ~INT_PTR;                   /* clear int req */
        return AC;

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* attached? */
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
dev_done = dev_done | INT_PTR;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
ptr_unit.buf = temp & 0377;
ptr_unit.pos = ptr_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_unit.buf = 0;
dev_done = dev_done & ~INT_PTR;                         /* clear done, int */
int_req = int_req & ~INT_PTR;
int_enable = int_enable | INT_PTR;                      /* set enable */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Paper tape punch: IOT routine */

int32 ptp (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* PCE */
        int_enable = int_enable & ~(INT_PTR+INT_PTP);   /* clear enables */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 1:                                             /* PSF */
        return (dev_done & INT_PTP)? IOT_SKP + AC: AC;

    case 2:                                             /* PCF */
        dev_done = dev_done & ~INT_PTP;                 /* clear flag */
        int_req = int_req & ~INT_PTP;                   /* clear int req */
        return AC;

    case 6:                                             /* PLS */
        dev_done = dev_done & ~INT_PTP;                 /* clear flag */
        int_req = int_req & ~INT_PTP;                   /* clear int req */
    case 4:                                             /* PPC */
        ptp_unit.buf = AC & 0377;                       /* load punch buf */
        sim_activate (&ptp_unit, ptp_unit.wait);        /* activate unit */
        return AC;

	default:
		return (stop_inst << IOT_V_REASON) + AC;
		}                                               /* end switch */
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
dev_done = dev_done | INT_PTP;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
if ((ptp_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if (putc (ptp_unit.buf, ptp_unit.fileref) == EOF) {
    perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ptp_unit.pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;
dev_done = dev_done & ~INT_PTP;                         /* clear done, int */
int_req = int_req & ~INT_PTP;
int_enable = int_enable | INT_PTP;                      /* set enable */
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 07756
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    06014,                      /* 7756, RFC */
    06011,                      /* 7757, LOOP, RSF */
    05357,                      /* JMP .-1 */
    06016,                      /* RFC RRB */
    07106,                      /* CLL RTL*/
    07006,                      /* RTL */
    07510,                      /* SPA*/
    05374,                      /* JMP 7774 */
    07006,                      /* RTL */
    06011,                      /* RSF */
    05367,                      /* JMP .-1 */
    06016,                      /* RFC RRB */
    07420,                      /* SNL */
    03776,                      /* DCA I 7776 */
    03376,                      /* 7774, DCA 7776 */
    05357,                      /* JMP 7757 */
    00000,                      /* 7776, 0 */
    05301                       /* 7777, JMP 7701 */
    };

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 M[];

if (ptr_dib.dev != DEV_PTR)                             /* only std devno */
    return STOP_NOTSTD;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
cpu_set_bootpc (BOOT_START);
return SCPE_OK;
}
