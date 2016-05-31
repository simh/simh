/*************************************************************************
 *                                                                       *
 * $Id: tx0_dpy.c 2060 2009-02-24 06:49:07Z hharte $                     *
 *                                                                       *
 * Copyright (c) 2009-2012, Howard M. Harte                              *
 * Copyright (c) 2004, Philip L. Budne                                   *
 * Copyright (c) 1993-2003, Robert M. Supnik                             *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     TX-0 display simulator                                            *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#ifdef USE_DISPLAY
#include "tx0_defs.h"
#include "display/display.h"
#include "sim_video.h"

extern int32 ios, iosta, PF;
extern int32 stop_inst;
extern int32 PEN_HIT;

t_stat dpy_svc (UNIT *uptr);
t_stat dpy_reset (DEVICE *dptr);

/* DPY data structures
   dpy_dev  DPY device descriptor
   dpy_unit DPY unit
   dpy_reg  DPY register list
*/

#define CYCLE_TIME 5            /* 5us memory cycle */
#define DPY_WAIT (50/CYCLE_TIME)    /* 50us */

UNIT dpy_unit = {
    UDATA (&dpy_svc, UNIT_ATTABLE, 0), DPY_WAIT };

static t_bool dpy_stop_flag = FALSE;

static void dpy_quit_callback (void)
{
dpy_stop_flag = TRUE;
}

#define DEB_VMOU      SIM_VID_DBG_MOUSE             /* Video mouse */
#define DEB_VKEY      SIM_VID_DBG_KEY               /* Video key */
#define DEB_VCUR      SIM_VID_DBG_CURSOR            /* Video cursor */
#define DEB_VVID      SIM_VID_DBG_VIDEO             /* Video */

DEBTAB dpy_deb[] = {
    { "VMOU",    DEB_VMOU, "Video Mouse" },
    { "VKEY",    DEB_VKEY, "Video Key" },
    { "VCUR",    DEB_VCUR, "Video Cursor" },
    { "VVID",    DEB_VVID, "Video Video" },
    { NULL, 0 }
    };

DEVICE dpy_dev = {
    "DPY", &dpy_unit, NULL, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &dpy_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dpy_deb };

/* Display Routine */
int32 dpy (int32 ac)
{
    int32 pen_hit;
    int32 x, y;
    int level;

    if (dpy_dev.flags & DEV_DIS)                /* disabled? */
        return SCPE_UDIS;

    x = (ac >> 9) & 0777;       /* X = high nine bits of AC */
    y = (ac & 0777);            /* Y = low nine bits of AC */

    /*
     * convert one's complement -255..+255 center origin
     * to 0..511 (lower left origin)
     */
    if (x & 0400)
        x ^= 0400;
    else
        x += 255;
    if (y & 0400)
        y ^= 0400;
    else
        y += 255;

    level = DISPLAY_INT_MAX;    /* Maximum intensity */

    if (display_point(x,y,level,0)) {
        /* here with light pen hit */
        PF = PF | 010;              /* set prog flag 3 */
        pen_hit = 1;

    } else {
        pen_hit = 0;
    }

    sim_activate (&dpy_unit, dpy_unit.wait);    /* activate */

    return pen_hit;
}

/*
 * Unit service routine
 *
 * Under X11 this includes polling for events, so it can't be
 * call TOO infrequently...
 */
t_stat dpy_svc (UNIT *uptr)
{
    display_age(dpy_unit.wait*CYCLE_TIME, 1);
    sim_activate (&dpy_unit, dpy_unit.wait); /* requeue! */
    if (dpy_stop_flag) {
        dpy_stop_flag = FALSE;          /* reset flag after we notice it */
        return SCPE_STOP;
        }
    return SCPE_OK;
}

/* Reset routine */

t_stat dpy_reset (DEVICE *dptr)
{
    sim_cancel (&dpy_unit);     /* deactivate unit */
    if (dpy_dev.flags & DEV_DIS)                /* disabled? */
        return SCPE_OK;
    display_init(DIS_TX0, RES_FULL, dptr);
    display_reset();
    vid_register_quit_callback (&dpy_quit_callback);
    iosta = iosta & ~(IOS_PNT | IOS_SPC); /* clear flags */
    return SCPE_OK;
}

#else  /* USE_DISPLAY not defined */
char tx0_dpy_unused;    /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
