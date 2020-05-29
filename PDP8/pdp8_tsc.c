/* pdp8_tsc.c: PDP-8 ETOS timesharing option board (TSC8-75)

   Copyright (c) 2003-2011, Robert M Supnik

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

   This module is based on Bernhard Baehr's description of the TSC8-75's
   operation. Many thanks to Bernhard for figuring out the behavior of this
   undocumented device.

   tsc          TSC8-75 option board
*/

#include "pdp8_defs.h"

extern int32 int_req;
extern int32 SF;
extern int32 tsc_ir;                                    /* "ERIOT" */
extern int32 tsc_pc;                                    /* "ERTB" */
extern int32 tsc_cdf;                                   /* "ECDF" */
extern int32 tsc_enb;                                   /* enable */

#define UNIT_V_SN699    (UNIT_V_UF + 0)                 /* SN 699 or above */
#define UNIT_SN699      (1 << UNIT_V_SN699)

int32 tsc (int32 IR, int32 AC);
t_stat tsc_reset (DEVICE *dptr);
const char *tsc_description (DEVICE *dptr);

/* TSC data structures

   tsc_dev      TSC device descriptor
   tsc_unit     TSC unit descriptor
   tsc_reg      TSC register list
*/

DIB tsc_dib = { DEV_TSC, 1, { &tsc } };

UNIT tsc_unit = { UDATA (NULL, UNIT_SN699, 0) };

REG tsc_reg[] = {
    { ORDATAD (IR, tsc_ir, 12, "most recently trapped instruction") },
    { ORDATAD (PC, tsc_pc, 12, "PC of most recently trapped instruction") },
    { FLDATAD (CDF, tsc_cdf, 0, "1 if trapped instruction is CDF, 0 otherwise") },
    { FLDATAD (ENB, tsc_enb, 0, "interrupt enable flag") },
    { FLDATAD (INT, int_req, INT_V_TSC, "interrupt pending flag") },
    { NULL }
    };

MTAB tsc_mod[] = {
    { UNIT_SN699, UNIT_SN699, "ESME", "ESME", NULL },
    { UNIT_SN699, 0, "no ESME", "NOESME", NULL },
    { 0 }
    };

DEVICE tsc_dev = {
    "TSC", &tsc_unit, tsc_reg, tsc_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tsc_reset,
    NULL, NULL, NULL,
    &tsc_dib, DEV_DISABLE | DEV_DIS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &tsc_description
    };

/* IOT routine */

int32 tsc (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* ETDS */
        tsc_enb = 0;                                    /* disable int req */
        int_req = int_req & ~INT_TSC;                   /* clear flag */
        break;

    case 1:                                             /* ESKP */
        return (int_req & INT_TSC)? IOT_SKP + AC: AC;   /* skip on int req */

    case 2:                                             /* ECTF */
        int_req = int_req & ~INT_TSC;                   /* clear int req */
        break;

    case 3:                                             /* ECDF */
        AC = AC | ((tsc_ir >> 3) & 07);                 /* read "ERIOT"<6:8> */
        if (tsc_cdf)                                    /* if cdf, skip */
            AC = AC | IOT_SKP;
        tsc_cdf = 0;
        break;

    case 4:                                             /* ERTB */
        return tsc_pc;

    case 5:                                             /* ESME */
        if (tsc_unit.flags & UNIT_SN699) {              /* enabled? */
            if (tsc_cdf && ((tsc_ir & 070) >> 3) == (SF & 07)) {
                AC = AC | IOT_SKP;
                tsc_cdf = 0;
                }
            }
        break;

    case 6:                                             /* ERIOT */
        return tsc_ir;

    case 7:                                             /* ETEN */
        tsc_enb = 1;
        break;
        }                                               /* end switch */

return AC;
}

/* Reset routine */

t_stat tsc_reset (DEVICE *dptr)
{
tsc_ir = 0;
tsc_pc = 0;
tsc_cdf = 0;
tsc_enb = 0;
int_req = int_req & ~INT_TSC;
return SCPE_OK;
}

const char *tsc_description (DEVICE *dptr)
{
return "TSC8-75 option board";
}
