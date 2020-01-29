/* vax730_syslist.c: VAX 730 device list

   Copyright (c) 2010-2011, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   29-Mar-2011  MB      First Version
*/

#include "vax_defs.h"

char sim_name[] = "VAX 11/730";

void vax_init(void)
{
sim_savename = "VAX730";
}

extern DEVICE cpu_dev;
extern DEVICE tlb_dev;
extern DEVICE sysb_dev;
extern DEVICE mctl_dev;
extern DEVICE uba_dev;
extern DEVICE clk_dev;
extern DEVICE tmr_dev;
extern DEVICE tti_dev, tto_dev;
extern DEVICE dt_dev;
extern DEVICE td_dev;
extern DEVICE tdc_dev;
extern DEVICE cr_dev;
extern DEVICE lpt_dev;
extern DEVICE rq_dev, rqb_dev, rqc_dev, rqd_dev;
extern DEVICE rb_dev;
extern DEVICE rl_dev;
extern DEVICE hk_dev;
extern DEVICE rk_dev;
extern DEVICE ry_dev;
extern DEVICE ts_dev;
extern DEVICE tq_dev;
extern DEVICE dz_dev;
extern DEVICE vh_dev;
extern DEVICE xu_dev, xub_dev;
extern DEVICE dmc_dev;
extern DEVICE ch_dev;

DEVICE *sim_devices[] = { 
    &cpu_dev,
    &tlb_dev,
    &sysb_dev,
    &mctl_dev,
    &uba_dev,
    &clk_dev,
    &tmr_dev,
    &tti_dev,
    &tto_dev,
    &dt_dev,
    &td_dev,
    &tdc_dev,
    &dz_dev,
    &vh_dev,
    &cr_dev,
    &lpt_dev,
    &rl_dev,
    &hk_dev,
    &rk_dev,
    &rq_dev,
    &rqb_dev,
    &rqc_dev,
    &rqd_dev,
    &rb_dev,
    &ry_dev,
    &ts_dev,
    &tq_dev,
    &xu_dev,
    &xub_dev,
    &dmc_dev,
    &ch_dev,
    NULL
    };

/* Binary loader

   The binary loader handles absolute system images, that is, system
   images linked /SYSTEM.  These are simply a byte stream, with no
   origin or relocation information.

   -r           load ROM0
   -s           load ROM1
   -o           for memory, specify origin
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
t_stat r;
int32 val;
uint32 origin, limit;

if (flag)                                               /* dump? */
    return sim_messagef (SCPE_NOFNC, "Command Not Implemented\n");
origin = 0;                                             /* memory */
limit = (uint32) cpu_unit.capac;
if (sim_switches & SWMASK ('O')) {                      /* origin? */
    origin = (int32) get_uint (cptr, 16, 0xFFFFFFFF, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    }

while ((val = Fgetc (fileref)) != EOF) {                 /* read byte stream */
    if (sim_switches & SWMASK ('R')) {                  /* ROM0? */
        return SCPE_NXM;
        }
    else if (sim_switches & SWMASK ('S')) {             /* ROM1? */
        return SCPE_NXM;
        }
    else {
        if (origin >= limit)                            /* NXM? */
            return SCPE_NXM;
        WriteB (origin, val);                           /* memory */
        }
    origin = origin + 1;
    }
return SCPE_OK;
}
