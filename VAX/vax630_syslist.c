/* vax630_syslist.c: MicroVAX II device list

   Copyright (c) 2009-2012, Matt Burke
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

   08-Nov-2012  MB      First version
*/

#include "vax_defs.h"

#if defined(VAX_620)
char sim_name[] = "rtVAX1000 (KA620)";

void vax_init(void)
{
sim_savename = "rtVAX1000 (KA620)";
}
#else
char sim_name[] = "MicroVAX II (KA630)";

void vax_init(void)
{
sim_savename = "MicroVAX II (KA630)";
}
#endif

WEAK void (*sim_vm_init) (void) = &vax_init;

extern DEVICE cpu_dev;
extern DEVICE tlb_dev;
extern DEVICE rom_dev;
extern DEVICE nvr_dev;
extern DEVICE wtc_dev;
extern DEVICE sysd_dev;
extern DEVICE qba_dev;
extern DEVICE tti_dev, tto_dev;
extern DEVICE tdc_dev;
extern DEVICE cr_dev;
extern DEVICE lpt_dev;
extern DEVICE clk_dev;
extern DEVICE rq_dev, rqb_dev, rqc_dev, rqd_dev;
extern DEVICE rl_dev;
extern DEVICE ts_dev;
extern DEVICE tq_dev;
extern DEVICE dz_dev;
extern DEVICE xq_dev, xqb_dev;
extern DEVICE vh_dev;
extern DEVICE va_dev;
extern DEVICE vc_dev;
extern DEVICE lk_dev;
extern DEVICE vs_dev;

DEVICE *sim_devices[] = { 
    &cpu_dev,
    &tlb_dev,
    &rom_dev,
    &nvr_dev,
    &wtc_dev,
    &sysd_dev,
    &qba_dev,
    &clk_dev,
    &tti_dev,
    &tto_dev,
    &tdc_dev,
    &dz_dev,
    &vh_dev,
    &cr_dev,
    &lpt_dev,
#if defined(USE_SIM_VIDEO) && defined(HAVE_LIBSDL)
    &va_dev,
    &vc_dev,
    &lk_dev,
    &vs_dev,
#endif
    &rl_dev,
    &rq_dev,
    &rqb_dev,
    &rqc_dev,
    &rqd_dev,
    &ts_dev,
    &tq_dev,
    &xq_dev,
    &xqb_dev,
    NULL
    };

/* Binary loader

   The binary loader handles absolute system images, that is, system
   images linked /SYSTEM.  These are simply a byte stream, with no
   origin or relocation information.

   -r           load ROM
   -n           load NVR
   -o           for memory, specify origin
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
t_stat r;
int32 i;
uint32 origin, limit, step = 1;

if (flag)                                               /* dump? */
    return sim_messagef (SCPE_NOFNC, "Command Not Implemented\n");
if (sim_switches & SWMASK ('R')) {                      /* ROM? */
    origin = ROMBASE;
    limit = ROMBASE + ROMSIZE;
    }
else if (sim_switches & SWMASK ('N')) {                 /* NVR? */
    origin = NVRBASE;
    limit = NVRBASE + NVRASIZE;
    step = 2;
    }
else {
    origin = 0;                                         /* memory */
    limit = (uint32) cpu_unit.capac;
    if (sim_switches & SWMASK ('O')) {                  /* origin? */
        origin = (int32) get_uint (cptr, 16, 0xFFFFFFFF, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        }
    }
while ((i = Fgetc (fileref)) != EOF) {                   /* read byte stream */
    if (origin >= limit)                                /* NXM? */
        return SCPE_NXM;
    if (sim_switches & SWMASK ('R'))                    /* ROM? */
        rom_wr_B (origin, i);                           /* not writeable */
    else WriteB (origin, i);                            /* store byte */
    origin = origin + step;
    }
return SCPE_OK;
}
