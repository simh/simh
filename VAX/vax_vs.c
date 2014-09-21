/* vax_vs.c: DEC Mouse/Tablet (VSXXX)

   Copyright (c) 2011-2013, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   vs           VSXXX-nn pointing device

   09-Aug-2013  MB      Added definitions for self test report
   11-Jun-2013  MB      First version
*/

#if !defined(VAX_620)

#include "vax_defs.h"
#include "sim_video.h"

#define VSXXX_IDLE      0
#define VSXXX_RECV      1
#define VSXXX_SEND      2
#define VSXXX_TEST      3

#define VSXXX_PROMPT    0
#define VSXXX_INC       1

#define VSXXX_REV       0                               /* hardware revision */

/* Report bit definitions */

#define RPT_SYNC      0x80                              /* synchronise */
#define RPT_TABP      0x40                              /* tablet position */
#define RPT_TEST      0x20                              /* self test */
#define RPT_TAB       0x4                               /* tablet device */
#define RPT_MOU       0x2                               /* mouse device */
#define RPT_V_MFR     4                                 /* manufacturer location ID */
#define RPT_REV       0xF                               /* revision number */
#define RPT_BC        0x7                               /* button code */
#define RPT_EC        0x7F                              /* error code */

/* Debugging Bitmaps */

#define DBG_SERIAL      0x0001                          /* serial port data */
#define DBG_CMD         0x0002                          /* commands */

int32 vs_state = VSXXX_IDLE;
int32 vs_mode = VSXXX_PROMPT;
int32 vs_bptr = 0;
int32 vs_datalen = 0;
int32 vs_x = 0;                                         /* X-axis motion */
int32 vs_y = 0;                                         /* Y-axis motion */
t_bool vs_l = 0;                                        /* Left button state */
t_bool vs_m = 0;                                        /* Middle button state */
t_bool vs_r = 0;                                        /* Right button state */
uint8 vs_buf[10];

DEVICE vs_dev;
t_stat vs_wr (uint8 c);
t_stat vs_rd (uint8 *c);
t_stat vs_reset (DEVICE *dptr);
void vs_cmd (int32 c);
void vs_sendupd (void);
void vs_poll (void);


/* VS data structures

   vs_dev       VS device descriptor
   vs_unit      VS unit list
   vs_reg       VS register list
   vs_mod       VS modifier list
   vs_debug     VS debug list
*/

DEBTAB vs_debug[] = {
    {"SERIAL", DBG_SERIAL},
    {"CMD",    DBG_CMD},
    {0}
    };

UNIT vs_unit = { UDATA (NULL, 0, 0) };

REG vs_reg[] = {
    { NULL }
    };

MTAB vs_mod[] = {
    { 0 }
    };

DEVICE vs_dev = {
    "VS", &vs_unit, vs_reg, vs_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &vs_reset,
    NULL, NULL, NULL,
    NULL, DEV_DIS | DEV_DEBUG, 0,
    vs_debug
    };


t_stat vs_wr (uint8 c)
{
if (vs_state != VSXXX_TEST) {
    vs_bptr = 0;
    vs_state = VSXXX_IDLE;
    vs_datalen = 0;
    vs_cmd (c);
    }
return SCPE_OK;
}

t_stat vs_rd (uint8 *c)
{
if (vs_state == VSXXX_IDLE)
    vs_poll ();
switch (vs_state) {

    case VSXXX_IDLE:
        *c = 0;
        return SCPE_EOF;

    case VSXXX_SEND:
    case VSXXX_TEST:
        *c = vs_buf[vs_bptr++];
        sim_debug (DBG_SERIAL, &vs_dev, "mouse -> vax: %02X\n", *c);
        if (vs_bptr == vs_datalen) {
            vs_state = VSXXX_IDLE;
            }
        return SCPE_OK;
    }
return SCPE_EOF;
}

void vs_cmd (int32 c)
{
sim_debug (DBG_SERIAL, &vs_dev, "vax -> mouse: %c\n", c);
switch (c) {

    case 0x52:                                          /* R */
        sim_debug (DBG_CMD, &vs_dev, "set mode incremental\n", c);
        vs_mode = VSXXX_INC;
        break;

    case 0x44:                                          /* D */
        sim_debug (DBG_CMD, &vs_dev, "set mode prompt\n", c);
        vs_mode = VSXXX_PROMPT;
        break;

    case 0x50:                                          /* P */
        sim_debug (DBG_CMD, &vs_dev, "poll\n", c);
        vs_mode = VSXXX_PROMPT;
        vs_sendupd ();
        break;

    case 0x54:                                          /* T */
        sim_debug (DBG_CMD, &vs_dev, "test\n", c);
        vs_reset (&vs_dev);
        vs_state = VSXXX_TEST;                          /* send self test report */
        vs_buf[0] = RPT_TEST | RPT_SYNC | (VSXXX_REV & RPT_REV);
        vs_buf[1] = (1 << RPT_V_MFR) | RPT_MOU;         /* device type, build location */
        vs_buf[2] = 0;                                  /* error code <6:0> (0 = OK) */
        vs_buf[3] = 0;                                  /* button code <2:0> (0 = OK) */
        vs_bptr = 0;
        vs_state = VSXXX_SEND;
        vs_datalen = 4;
        break;
    }
}

t_stat vs_reset (DEVICE *dptr)
{
vs_bptr = 0;
vs_state = VSXXX_IDLE;
vs_datalen = 0;
vs_mode = VSXXX_PROMPT;
return SCPE_OK;
}

void vs_sendupd (void)
{
vs_buf[0] = RPT_SYNC;
vs_buf[0] |= (((vs_x >= 0) ? 1 : 0) << 4);              /* sign bits */
vs_buf[0] |= (((vs_y >= 0) ? 1 : 0) << 3);
vs_buf[0] |= (((vs_l) ? 1 : 0) << 2);                   /* button states */
vs_buf[0] |= (((vs_m) ? 1 : 0) << 1);
vs_buf[0] |= ((vs_r) ? 1 : 0);
vs_buf[1] = (abs(vs_x)) & 0x7F;                         /* motion */
vs_buf[2] = (abs(vs_y)) & 0x7F;
vs_bptr = 0;
vs_state = VSXXX_SEND;
vs_datalen = 3;
}

void vs_poll (void)
{
SIM_MOUSE_EVENT ev;

if (vid_poll_mouse (&ev) != SCPE_OK)
    return;
if (vs_state == VSXXX_IDLE) {
    vs_x = ev.x_rel;
    vs_y = ev.y_rel;
    vs_l = ev.b1_state;
    vs_m = ev.b2_state;
    vs_r = ev.b3_state;
    if (vs_mode == VSXXX_INC)
        vs_sendupd ();
    }
}

#endif /* !VAX_620 */
