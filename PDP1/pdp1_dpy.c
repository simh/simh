/* pdp1_dpy.c: PDP-1 display simulator

   Copyright (c) 2004, Philip L. Budne
   Copyright (c) 1993-2003, Robert M. Supnik

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   dpy          Type 30 Display for the PDP-1
   02-Feb-04    PLB     Revamp intensity levels
   02-Jan-04    DAG     Provide dummy global when display not supported
   16-Sep-03    PLB     Update for SIMH 3.0-2
   12-Sep-03    PLB     Add spacewar switch support
   04-Sep-03    PLB     Start from pdp1_lp.c
*/

#ifdef USE_DISPLAY
#include "pdp1_defs.h"
#include "display/display.h"
#include "sim_video.h"

extern int32 ios, cpls, iosta, PF;
extern int32 stop_inst;

t_stat dpy_svc (UNIT *uptr);
t_stat dpy_reset (DEVICE *dptr);

/* DPY data structures

   dpy_dev      DPY device descriptor
   dpy_unit     DPY unit
   dpy_reg      DPY register list
*/

#define CYCLE_TIME 5                /* 5us memory cycle */
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
        NULL, DEV_DIS | DEV_DISABLE | DEV_DEBUG,
        0, dpy_deb};

/* Display IOT routine */

int32 dpy (int32 inst, int32 dev, int32 io, int32 ac)
{
int32 x, y;
int level;

if (dpy_dev.flags & DEV_DIS)                            /* disabled? */
        return (stop_inst << IOT_V_REASON) | io;        /* stop if requested */
if (GEN_CPLS (inst)) {                                  /* comp pulse? */
        ios = 0;                                        /* clear flop */
        cpls = cpls | CPLS_DPY;  }                      /* request completion */
else cpls = cpls & ~CPLS_DPY;

x = (ac >> 8) & 01777;                  /* high ten bits of ac */
y = (io >> 8) & 01777;                  /* high ten bits of io */
/*
 * convert one's complement -511..+511 center origin
 * to 0..1022 (lower left origin)
 */
if (x & 01000)
    x ^= 01000;
else
    x += 511;
if (y & 01000)
    y ^= 01000;
else
    y += 511;

/* intensity, from values seen in spacewar (40,00,01,02,03) */
switch ((inst >> 6) & 077) {
case 01: level = DISPLAY_INT_MAX-5; break;
case 02: level = DISPLAY_INT_MAX-4; break;
case 03: level = DISPLAY_INT_MAX-2; break;
case 040:                               /* super bright? */
default: level = DISPLAY_INT_MAX; break;
}

if (display_point(x,y,level,0)) {
    /* here with light pen hit */
    PF = PF | 010;                              /* set prog flag 3 */
    iosta |= IOS_TTI;                           /* set io status flag */
}
else
    iosta &= ~IOS_TTI;                          /* clear io status flag */
sim_activate (&dpy_unit, dpy_unit.wait);        /* activate */

return io;
}

/*
 * Unit service routine
 *
 */
t_stat dpy_svc (UNIT *uptr)
{
    if (cpls & CPLS_DPY) {              /* completion pulse? */
        ios = 1;                        /* restart */
        cpls = cpls & ~CPLS_DPY;  }     /* clr pulse pending */

    display_age(dpy_unit.wait*CYCLE_TIME, 0);
    sim_activate_after (&dpy_unit, dpy_unit.wait*CYCLE_TIME); /* requeue! */
    if (dpy_stop_flag) {
        dpy_stop_flag = FALSE;          /* reset flag after we notice it */
        return SCPE_STOP;
        }
    return SCPE_OK;
}

/* Reset routine */

t_stat dpy_reset (DEVICE *dptr)
{
    if (!(dptr->flags & DEV_DIS)) {
        display_init(DISPLAY_TYPE, PIX_SCALE, dptr);
        display_reset();
        vid_register_quit_callback (&dpy_quit_callback);
        cpls = cpls & ~CPLS_DPY;
        iosta = iosta & ~(IOS_PNT | IOS_SPC); /* clear flags */
        }
    sim_cancel (&dpy_unit);             /* deactivate unit */
    return SCPE_OK;
}

int32 spacewar (int32 inst, int32 dev, int32 io)
{
if (dpy_dev.flags & DEV_DIS)                            /* disabled? */
        return (stop_inst << IOT_V_REASON) | io;        /* stop if requested */
return spacewar_switches;
}
#else  /* USE_DISPLAY not defined */
char pdp1_dpy_unused;   /* sometimes empty object modules cause problems */
#endif /* USE_DISPLAY not defined */
