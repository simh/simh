/* ka10_dkb.c:Stanford Microswitch scanner.

   Copyright (c) 2013-2017, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#ifndef NUM_DEVS_DKB
#define NUM_DEVS_DKB 0
#endif

#if NUM_DEVS_DKB > 0 

t_stat dkb_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dkb_description (DEVICE *dptr);

#define DKB_DEVNUM        0310

#define STATUS            u3
#define DATA              u4
#define PIA               u5

t_stat dkb_devio(uint32 dev, uint64 *data);

DIB dkb_dib = { DKB_DEVNUM, 1, dkb_devio, NULL};

UNIT dkb_unit[] = {
    { 0 }
    };


MTAB dkb_mod[] = {
    { 0 }
    };

DEVICE dkb_dev = {
    "DKB", dkb_unit, NULL, dkb_mod,
    2, 10, 31, 1, 8, 8,
    NULL, NULL, NULL,
    NULL, NULL, NULL, &dkb_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS, 0, dev_debug,
    NULL, NULL, &dkb_help, NULL, NULL, &dkb_description
    };

int status;
t_stat dkb_devio(uint32 dev, uint64 *data) {
/*   uint64     res; */
     switch(dev & 3) {
     case CONI:
        *data = status;
        sim_debug(DEBUG_CONI, &dkb_dev, "DKB %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         status = (int)(*data&7);
         sim_debug(DEBUG_CONO, &dkb_dev, "DKB %03o CONO %06o\n", dev, (uint32)*data);
         break;
     case DATAI:
         sim_debug(DEBUG_DATAIO, &dkb_dev, "DKB %03o DATAI %06o\n", dev, (uint32)*data);
         break;
    case DATAO:
         sim_debug(DEBUG_DATAIO, &dkb_dev, "DKB %03o DATAO %06o\n", dev, (uint32)*data);
         break;
    }
    return SCPE_OK;
}



t_stat dkb_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
return SCPE_OK;
}

const char *dkb_description (DEVICE *dptr)
{
    return "Console TTY Line";
}
#endif
