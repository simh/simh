/* ka10_imx.c: Input multplexor for A/D.

   Copyright (c) 2018, Lars Brinkhoff

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

   This is a device which has 128 A/D channels.  It's specific to the MIT
   AI lab PDP-10.
*/

#include <time.h>
#include "kx10_defs.h"

#ifndef NUM_DEVS_IMX
#define NUM_DEVS_IMX 0
#endif

#if (NUM_DEVS_IMX > 0)

#define IMX_DEVNUM      0574

#define IMX_PIA         0000007
#define IMX_DONE        0000010
#define IMX_PACK        0000040
#define IMX_SEQUENCE    0000100
#define IMX_TEST        0000200
#define IMX_RATE        0377000
#define IMX_ASSIGNED    0400000000000LL

#define IMX_CONO        (IMX_PIA | IMX_PACK | IMX_SEQUENCE | IMX_RATE)
#define IMX_CONI        (IMX_PIA | IMX_DONE | IMX_PACK | IMX_SEQUENCE | IMX_TEST | IMX_ASSIGNED)

#define IMX_CHANNEL     0000177

t_stat         imx_devio(uint32 dev, uint64 *data);
const char     *imx_description (DEVICE *dptr);

static uint64 status = IMX_ASSIGNED;
static int initial_channel = 0;
static int current_channel = 0;

UNIT                imx_unit[] = {
    {UDATA(NULL, UNIT_DISABLE, 0)},  /* 0 */
};
DIB imx_dib = {IMX_DEVNUM, 1, &imx_devio, NULL};

MTAB imx_mod[] = {
    { 0 }
    };

DEVICE              imx_dev = {
    "IMX", imx_unit, NULL, imx_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &imx_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &imx_description
};

static int imx_sample (void)
{
    int sample = 2048;
    if (status & IMX_SEQUENCE)
        current_channel = (current_channel + 1) & IMX_CHANNEL;
    else
        current_channel = initial_channel;
    return sample;
}

t_stat imx_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &imx_dev;

    switch(dev & 07) {
    case CONO|4:
        status &= ~IMX_CONO;
        status |= *data & IMX_CONO;
        current_channel = initial_channel;
        break;
    case CONI|4:
        status |= IMX_DONE;
        *data = status & IMX_CONI;
        break;
    case DATAO|4:
        initial_channel = *data & IMX_CHANNEL;
        break;
    case DATAI|4:
        *data = imx_sample();
        if (status & IMX_PACK) {
            *data <<= 24;
            *data |= imx_sample() << 12;
            *data |= imx_sample();
        }
        break;
    }

    return SCPE_OK;
}

const char *imx_description (DEVICE *dptr)
{
    return "A/D input multiplexor";
}
#endif
