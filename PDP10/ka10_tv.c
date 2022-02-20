/* ka10_tv.c: Stanford TV camera and Spacewar buttons.

   Copyright (c) 2021, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"
#include "sim_video.h"

#if NUM_DEVS_TV > 0

#define TV_DEVNUM 0404

#define JOY_MAX_UNITS     5
#define JOY_MAX_AXES      4
#define JOY_MAX_BUTTONS   4

#define JOY_TRIG   5000

#define ROTATE_AXIS       0
#define THRUSTER_AXIS     1
#define TORPEDO_BUTTON    0
#define HYPER_BUTTON      1

/* CONI bits. */
#define TPBIT   001LL  /* Fire torpedo. */
#define THRUBT  002LL  /* Thruster. */
#define ROTRBT  004LL  /* Rotate right. */
#define ROTLBT  010LL  /* Rotate left. */
#define HYPRBT  014LL  /* Hyperspace = right + left. */

static t_stat tv_devio(uint32 dev, uint64 *data);
static t_stat tv_reset (DEVICE *dptr);
static const char  *tv_description (DEVICE *dptr);

static int joy_axes[JOY_MAX_UNITS][JOY_MAX_AXES];
static int joy_buttons[JOY_MAX_UNITS][JOY_MAX_BUTTONS];

DIB tv_dib = { TV_DEVNUM, 1, &tv_devio, NULL };

DEVICE tv_dev = {
    "TV", NULL, NULL, NULL,
    0, 8, 0, 1, 8, 36,
    NULL, NULL, &tv_reset, NULL, NULL, NULL,
    &tv_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &tv_description
};

static void tv_joy_motion(int which, int axis, int value)
{
    if (which < JOY_MAX_UNITS && axis < JOY_MAX_AXES) {
        joy_axes[which][axis] = value;
        sim_debug (DEBUG_DETAIL, &tv_dev, "Joystick %d axid %d: value %d\n",
                   which, axis, value);
    }
}

static void tv_joy_button(int which, int button, int state)
{
    if (which < JOY_MAX_UNITS && button < JOY_MAX_BUTTONS) {
        joy_buttons[which][button] = state;
        sim_debug (DEBUG_DETAIL, &tv_dev, "Joystick %d button %d: state %d\n",
                   which, button, state);
    }
}

static uint64 tv_buttons (void)
{
    uint64 buttons = 2; /* Needed for unknown reason! */
    int i;

    for (i = 0; i < JOY_MAX_UNITS; i++) {
        if (joy_axes[i][ROTATE_AXIS] > JOY_TRIG)
            buttons ^= ROTRBT << (4 * i);
        else if (joy_axes[i][ROTATE_AXIS] < -JOY_TRIG)
            buttons ^= ROTLBT << (4 * i);
        if (joy_axes[i][THRUSTER_AXIS] < -JOY_TRIG)
            buttons ^= THRUBT << (4 * i);
        if (joy_buttons[i][TORPEDO_BUTTON])
            buttons ^= TPBIT << (4 * i);
        if (joy_buttons[i][HYPER_BUTTON])
            buttons ^= HYPRBT << (4 * i);
    }

    return buttons;
}

t_stat tv_devio(uint32 dev, uint64 *data)
{
    switch(dev & 07) {
    case CONI|4:
        *data = tv_buttons ();
        sim_debug (DEBUG_CONI, &tv_dev, "%07llo\n", *data);
        break;
    }

    return SCPE_OK;
}

static t_stat tv_reset (DEVICE *dptr)
{
    memset (joy_axes, 0, sizeof joy_axes);
    memset (joy_buttons, 0, sizeof joy_buttons);
    vid_register_gamepad_motion_callback (tv_joy_motion);
    vid_register_gamepad_button_callback (tv_joy_button);
    return SCPE_OK;
}

const char *tv_description (DEVICE *dptr)
{
    return "Stanford TV camera and Spacewar buttons";
}
#endif
