/* vax_vs.c: DEC Mouse/Tablet (VSXXX)

   Copyright (c) 2013-2017, Matt Burke

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

   Related documents:

        EK-104AA-TM-001 - VCB02 Technical Manual (chapter C.6)
*/

#if !defined(VAX_620)

#include "vax_vs.h"

#define VSXXX_PROMPT    0
#define VSXXX_INC       1

#define VSXXX_REV       0                               /* hardware revision */

/* Debugging Bitmaps */

#define DBG_SERIAL      0x0001                          /* serial port data */
#define DBG_CMD         0x0002                          /* commands */

#define VS_BUF_LEN        100

typedef struct {
    int32 head;
    int32 tail;
    int32 count;
    uint8 buf[VS_BUF_LEN];
} VS_FIFO;

int32 vs_mode = VSXXX_PROMPT;
int32 vs_x = 0;                                         /* X-axis motion */
int32 vs_y = 0;                                         /* Y-axis motion */
t_bool vs_l = FALSE;                                    /* Left button state */
t_bool vs_m = FALSE;                                    /* Middle button state */
t_bool vs_r = FALSE;                                    /* Right button state */
VS_FIFO vs_sndf;                                        /* send FIFO */

t_stat vs_wr (uint8 c);
t_stat vs_rd (uint8 *c);
t_stat vs_reset (DEVICE *dptr);
void vs_cmd (int32 c);
void vs_sendupd (void);
const char *vs_description (DEVICE *dptr);
t_stat vs_put_fifo (VS_FIFO *fifo, uint8 data);
t_stat vs_get_fifo (VS_FIFO *fifo, uint8 *data);
void vs_clear_fifo (VS_FIFO *fifo);


/* VS data structures

   vs_dev       VS device descriptor
   vs_unit      VS unit list
   vs_reg       VS register list
   vs_mod       VS modifier list
   vs_debug     VS debug list
*/

DEBTAB vs_debug[] = {
    {"SERIAL", DBG_SERIAL,  "Serial port data"},
    {"CMD",    DBG_CMD,     "Commands"},
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
    vs_debug, NULL, NULL, NULL, NULL, NULL,
    &vs_description
    };


t_stat vs_wr (uint8 c)
{
vs_clear_fifo (&vs_sndf);
vs_cmd (c);
return SCPE_OK;
}

t_stat vs_rd (uint8 *c)
{
t_stat r;

r = vs_get_fifo (&vs_sndf, c);
if (r == SCPE_OK)
    sim_debug (DBG_SERIAL, &vs_dev, "mouse -> vax: 0x%02X\n", *c);
return r;
}

t_stat vs_put_fifo (VS_FIFO *fifo, uint8 data)
{
if (fifo->count < VS_BUF_LEN) {
    fifo->buf[fifo->head++] = data;
    if (fifo->head == VS_BUF_LEN)
        fifo->head = 0;
    fifo->count++;
    return SCPE_OK;
    }
else
    return SCPE_EOF;
}

t_stat vs_get_fifo (VS_FIFO *fifo, uint8 *data)
{
if (fifo->count > 0) {
    *data = fifo->buf[fifo->tail++];
    if (fifo->tail == VS_BUF_LEN)
        fifo->tail = 0;
    fifo->count--;
    return SCPE_OK;
    }
else
    return SCPE_EOF;
}

void vs_clear_fifo (VS_FIFO *fifo)
{
fifo->head = 0;
fifo->tail = 0;
fifo->count = 0;
}

void vs_cmd (int32 c)
{
uint8 data;

sim_debug (DBG_SERIAL, &vs_dev, "vax -> mouse: %c\n", c);
switch (c) {

    case VS_INCR:                                       /* R */
        sim_debug (DBG_CMD, &vs_dev, "set mode incremental(%c)\n", c);
        vs_mode = VSXXX_INC;
        break;

    case VS_PROMPT:                                     /* D */
        sim_debug (DBG_CMD, &vs_dev, "set mode prompt(%c)\n", c);
        vs_mode = VSXXX_PROMPT;
        break;

    case VS_POLL:                                       /* P */
        sim_debug (DBG_CMD, &vs_dev, "poll(%c)\n", c);
        vs_mode = VSXXX_PROMPT;
        vs_sendupd ();
        break;

    case VS_TEST:                                       /* T */
        sim_debug (DBG_CMD, &vs_dev, "test(%c)\n", c);
        vs_reset (&vs_dev);
        data = RPT_TEST | RPT_SYNC | (VSXXX_REV & RPT_REV);
        vs_put_fifo (&vs_sndf, data);
        data = (1 << RPT_V_MFR) | RPT_MOU;              /* device type, build location */
        vs_put_fifo (&vs_sndf, data);
        data = 0;                                       /* error code <6:0> (0 = OK) */
        vs_put_fifo (&vs_sndf, data);
        data = 0;                                       /* button code <2:0> (0 = OK) */
        vs_put_fifo (&vs_sndf, data);
        break;
    }
}

t_stat vs_reset (DEVICE *dptr)
{
vs_x = 0;
vs_y = 0;
vs_l = FALSE;
vs_m = FALSE;
vs_r = FALSE;
vs_clear_fifo (&vs_sndf);
vs_mode = VSXXX_PROMPT;
return SCPE_OK;
}

void vs_sendupd (void)
{
uint8 b0, b1, b2;

do {
    if (vs_sndf.count == VS_BUF_LEN)                    /* fifo full? */
        return;
    b0 = RPT_SYNC;
    b0 |= (((vs_x >  0) ? 1 : 0) << 4);                 /* sign bits */
    b0 |= (((vs_y >= 0) ? 0 : 1) << 3);
    b0 |= (((vs_l) ? 1 : 0) << 2);                      /* button states */
    b0 |= (((vs_m) ? 1 : 0) << 1);
    b0 |= ((vs_r) ? 1 : 0);
    vs_put_fifo (&vs_sndf, b0);
    b1 = (abs(vs_x) > 0x3F) ? 0x3F : abs(vs_x);         /* motion (limited to 63 pixels in any direction) */
    if (vs_x > 0)
        vs_x -= b1;
    else
        vs_x += b1;
    vs_put_fifo (&vs_sndf, b1);
    b2 = (abs(vs_y) > 0x3F) ? 0x3F : abs(vs_y);
    if (vs_y > 0)
        vs_y -= b2;
    else
        vs_y += b2;
    vs_put_fifo (&vs_sndf, b2);
    sim_debug (DBG_SERIAL, &vs_dev, "mouse motion queued for delivery: Motion:(%s%d,%s%d), Buttons:(%s,%s,%s) Remnant skipped:(%d,%d)\n",
               (b0 & 0x10) ? "s" : "", b1, (b0 & 0x08) ? "s" : "", b2, (b0 & 0x04) ? "L" : "l", (b0 & 0x02) ? "M" : "m", (b0 & 0x01) ? "R" : "r", vs_x, vs_y);
    } while ((vs_x != 0) && (vs_y != 0));
vs_x = vs_y = 0;
}

void vs_event (SIM_MOUSE_EVENT *ev)
{
if ((ev->x_rel == 0) && (ev->y_rel == 0) &&
    (vs_l == ev->b1_state) && (vs_m == ev->b2_state) && (vs_r == ev->b3_state))
    return;
vs_x += ev->x_rel;
vs_y += ev->y_rel;
vs_l = ev->b1_state;
vs_m = ev->b2_state;
vs_r = ev->b3_state;
if (vs_mode == VSXXX_INC)
    vs_sendupd ();
}

const char *vs_description (DEVICE *dptr)
{
return "  VCB01 - VS Mouse interface";
}

#else /* defined(VAX_620) */
static const char *dummy_declaration = "Something to compile";
#endif /* !defined(VAX_620) */
